#define _GNU_SOURCE
/* for exp10 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>

#include "fwComm.h"
#include "scopeSup.h"
#include "max195xxSup.h"
#include "unitData.h"

static int
pollRead(ScopePvt *scp, uint16_t *hdr, int16_t *buf, size_t nElms)
{
int             st;
struct timespec wai;
	wai.tv_sec = 0;
	wai.tv_nsec = 100UL*1000UL*1000UL;
	while ( 0 == (st = buf_read_int16( scp, hdr, buf, nElms )) ) {
		clock_nanosleep( CLOCK_REALTIME, 0, &wai, NULL );
	}
	if ( st > 0 ) {
		unsigned ch;
		for ( ch = 0; ch < scope_get_num_channels( scp ); ++ch ) {
			if ( ( *hdr & FW_BUF_HDR_FLG_OVR(ch) ) ) {
				return -ERANGE;
			}
		}
	}
	return st;
}

static int
writeNonvolatileChecked( ScopePvt *scp, UnitData *unitData)
{
int st = scope_write_unit_data_nonvolatile( scp, unitData );
	if ( st < 0 ) {
			fprintf( stderr, "Error; scope_write_unit_data_nonvolatile() failed: %s\n", strerror(-st));
	}
	return st;
}

/*
 * 1. set PGA attenuation to 'pgaAtt' (40dB if nan)
 * 2. set dac to 'dacVolt'
 * 3. sleep(1)
 * 4. flush ADC buffer
 * 5. read data
 * 6. compute and return average of ADC ticks
 */

static int
measure(ScopePvt *scp, size_t nSamples, int16_t *buf, double pgaAttDb, double dacVolt, double *result)
{
unsigned nChannels = scope_get_num_channels( scp );
size_t   nElms     = nSamples * nChannels;
int      ch,n;
int      st;
uint16_t bufHdr;

	if ( isnan(pgaAttDb) ) {
		pgaAttDb = 40.0;
	}

	for ( ch = 0; ch < nChannels; ++ch ) {
		st = pgaSetAttDb( scp, ch, pgaAttDb );
		if ( st < 0 ) {
			fprintf( stderr, "Error; pgaSetAttDb(%lg) failed: %s\n", pgaAttDb, strerror(-st));
			goto bail;
		}

		st = dacSetVolt( scp, ch, dacVolt );
		if ( st < 0 ) {
			fprintf( stderr, "Error; dacSetVolt(%lg) failed: %s\n", dacVolt, strerror(-st));
			goto bail;
		}
	}

	if ( ! buf ) {
		return 0;
	}

	/* Let things settle */
	sleep(1);

	st = buf_flush( scp );
	if ( st < 0 ) {
		fprintf( stderr, "Error; buf_flush() failed: %s\n", strerror(-st));
		goto bail;
	}

	st = pollRead( scp, &bufHdr, buf, nElms );
	if ( st < 0 ) {
		fprintf( stderr, "Error; buf_read() failed: %s\n", strerror(-st));
		goto bail;
	}

	for ( ch = 0; ch < nChannels; ++ch ) {
		double sum = 0;
		for ( n = ch; n < nElms; n += nChannels ) {
			sum += (double)buf[n];
		}
		/* not volt yet; just counts! */
		result[ch] = sum / (double)nSamples;
	}

bail:
	return st;
}

static void
printCal(ScopeCalData *calData, unsigned nChannels)
{
unsigned ch;
	printf("Calibration Data:\n");
	printf("  Full Scale Volt (ch 0): %lg\n", calData[0].fullScaleVolt);
	printf("  Channel        | Relative Gain | Input Offset [V] | post-gain Offset (ticks)\n");
	for ( ch = 0; ch < nChannels; ++ch ) {
		printf("  %-15u|%15.3lg|%18.3lg|%26.3lg\n",
			ch,
			calData[0].fullScaleVolt/calData[ch].fullScaleVolt,
			calData[ch].offsetVolt,
			calData[ch].postGainOffsetTick
			);
	}
}

static const char  *DFLT_DEV = "/dev/ttyACM0";
static const double DFLT_DAC = 1.0;
static const size_t DFLT_NSM = 1024*1024;


static void
usage(const char *name)
{
	printf("Usage: %s [-EhIpw] [-a <pgaMinAttDb>] [-A <pgaMaxAttDb>] [-d <device>] [-D <calVolt>] [-F <fullScaleVolt>] [-n <nsamples>]\n", name);
	printf("       -h                   : Print this message.\n");
	printf("       -E                   : Erase existing calibration from non-volatile memory.\n");
	printf("                              Note: program exits after erase; no other operations performed.\n");
	printf("       -I                   : Allow calibration of a device that appears to have been\n");
	printf("                              already. Normally, this is prohibited as the device could be\n");
	printf("                              in an unknown state.\n");
	printf("       -p                   : Print initial calibration (before running calibration).\n");
	printf("       -w                   : Write to non-volatile memory after running calilbration.\n");
	printf("       -a <pgaMinAttDb>     : Set PGA min. attenuation in dB (default: read from device).\n");
	printf("       -a <pgaMaxAttDb>     : Set PGA max. attenuation in dB (default: read from device).\n");
	printf("       -d <device>          : Select tty <device> (default: %s).\n", DFLT_DEV);
	printf("       -D <calVolt>         : Run calibratiokn at <calVolt> (default: %gV).\n", DFLT_DAC);
	printf("       -F <fullScaleVolt>   : Set Volts at full scale (default: read from device).\n");
	printf("       -n <num_samples>     : Use <num_samples (default: %zd).\n", DFLT_NSM);
}

int
main(int argc, char **argv)
{
int16_t                *buf            = NULL;
FWInfo                 *fw             = NULL;
ScopePvt               *scp            = NULL;
int                     rv             = 1;
size_t                  nSamples       = DFLT_NSM;
const char             *devName        = DFLT_DEV;
int                     allowPreInited = 0;
ScopeCalData           *calData        = NULL;
double                  pgaMinAttDb    = 0.0/0.0;
double                  pgaMaxAttDb    = 0.0/0.0;
double                 *dvals          = NULL;
UnitData               *unitData       = NULL;
double                  dacCalVolt     = DFLT_DAC;
const double            dacCalZeroVolt = 0.0;
int                     maxADCTicks;

/* full-scale volt at 0dB pga attenuation */
double                  fullScaleVolt  = 0.0/0.0;
unsigned                nChannels      = 0;
int                     doWrite        = 0;
int                     doPrint        = 0;
int                     doErase        = 0;
double                  dval;
double                  pgaMinAtt;
double                  pgaMaxAtt;
int                     st;
int                     ch;
AcqParams               acqParams;
int                     opt;
double                 *d_p;
size_t                 *z_p;

	while ( (opt = getopt( argc, argv, "a:A:d:ED:F:hIn:pw")) > 0 ) {
		d_p = 0;
		z_p = 0;
		switch ( opt ) {
			case 'a': d_p     = &pgaMinAttDb;             break;
			case 'A': d_p     = &pgaMaxAttDb;             break;
			case 'd': devName = optarg;                   break;
			case 'D': d_p     = &dacCalVolt;              break;
			case 'E': doErase = 1;                        break;
			case 'F': d_p     = &fullScaleVolt;           break;
			case 'I': allowPreInited = 1;                 break;
			case 'n': z_p     = &nSamples;                break;
			case 'w': doWrite = 1;                        break;
			case 'p': ++doPrint;                          break;
			case 'h':
				rv = 0;
				/* fall thru */
			default:
				usage( argv[0] );
				return rv;
		}
		if ( d_p && 1 != sscanf( optarg, "%lg", d_p ) ) {
			fprintf( stderr, "Error: scanning arg to option '-%c' failed\n", opt);
			return rv;
		}
		if ( z_p && 1 != sscanf( optarg, "%zi", z_p ) ) {
			fprintf( stderr, "Error: scanning arg to option '-%c' failed\n", opt);
			return rv;
		}
	}

	if ( ! (fw = fw_open( devName, 115200 )) ) {
		fprintf( stderr, "Error: unable to open firmware (wrong tty device?)\n");
		goto bail;
	}

	if ( ! allowPreInited && scope_is_initialized( fw ) ) {
		fprintf( stderr, "Error: Scope seems already initialized; please power-cycle\n");
		goto bail;
	}

	if ( ! (scp = scope_open( fw )) ) {
		fprintf( stderr, "Error: unable to open Scope (wrong firmware?)\n");
		goto bail;
	}

	if ( doErase ) {
		printf("Erasing non-volatile data\n");
		st = writeNonvolatileChecked( scp, NULL );
		goto bail; /* OK exit if st == 0 */
	}

	st = dacGetVolt( scp, 0, &dval );
	if ( st < 0 ) {
		fprintf( stderr, "Error with calibration DAC: %s\n", strerror(-st));
		goto bail;
	}
	
	nChannels = scope_get_num_channels( scp );

	if ( ! (dvals = malloc( sizeof(*dvals) * nChannels ) ) ) {
		fprintf( stderr, "Error: no memory\n");
		goto bail;
	}

	st = acq_set_params( scp, NULL, &acqParams );
	if ( st < 0 ) {
		fprintf( stderr, "Error; unable to get acquisition Params: %s\n", strerror(-st));
		goto bail;
	}
	if ( nSamples > acqParams.nsamples ) {
		nSamples = acqParams.nsamples;
		fprintf( stderr, "Warning: nSamples reduced to max. supported: %zu\n", nSamples );
	}
	acqParams.mask     = ACQ_PARAM_MSK_NSM;
	acqParams.nsamples = nSamples;
	st = acq_set_params( scp, &acqParams, NULL );
	if ( st < 0 ) {
		fprintf( stderr, "Error; unable to set acquisition Params: %s\n", strerror(-st));
		goto bail;
	}

	if ( ! (buf = malloc( sizeof(*buf) * nChannels * nSamples )) ) {
		fprintf( stderr, "Error; no memory for acquisition buffer\n" );
		goto bail;
	}

	if ( ! (calData = malloc( sizeof(*calData) * nChannels )) ) {
		fprintf( stderr, "Error; no memory for calibration data\n" );
		goto bail;
	}

	st = scope_get_cal_data( scp, calData, nChannels );
	if ( st < 0 ) {
		fprintf( stderr, "Error; scope_get_cal_data failed: %s\n", strerror(-st));
		goto bail;
	}

	if ( doPrint ) {
		printf("Current calibration parameters:\n");
		printCal( calData, nChannels );
		st = 0;
		goto bail;
	}

	/* reset all calibration */
	st = scope_set_cal_data( scp, NULL, 0 );
	if ( st < 0 ) {
		fprintf( stderr, "Error; scope_set_cal_data(reset) failed: %s\n", strerror(-st));
		goto bail;
	}

	if ( isnan( pgaMinAttDb ) || isnan( pgaMaxAttDb ) ) {
		st = pgaGetAttRangeDb( scp, (isnan(pgaMinAttDb) ? &pgaMinAttDb : NULL), (isnan(pgaMaxAttDb) ? &pgaMaxAttDb : NULL) );
		if ( st < 0 ) {
			fprintf( stderr, "Error; pgaGetAttRangeDb failed: %s\n", strerror(-st));
			goto bail;
		}
	}

	for ( ch = 0; ch < nChannels; ++ch ) {
		st = fecSetACMode( scp, ch, 0 );
		if ( st < 0 ) {
			fprintf( stderr, "Error; fecSetACMode(1) failed: %s\n", strerror(-st));
			goto bail;
		}
		st = fecSetTermination( scp, ch, 1 );
		if ( st < 0 ) {
			fprintf( stderr, "Error; fecSetTermination(1) failed: %s\n", strerror(-st));
			goto bail;
		}
		st = fecSetAttDb( scp, ch, 20.0 );
		if ( st < 0 ) {
			fprintf( stderr, "Error; fecSetAttDb(20.0) failed: %s\n", strerror(-st));
			goto bail;
		}
	}

	/* number of bits per sample */
	if ( ( maxADCTicks = buf_get_sample_size( scp ) ) < 0 ) {
		fprintf( stderr, "Unable to determine sample size\n");
		goto bail;
	}
	/* max ticks */
	maxADCTicks  = buf_get_full_scale_ticks( scp );
	pgaMaxAtt    = exp10( pgaMaxAttDb / 20.0 );
	pgaMinAtt    = exp10( pgaMinAttDb / 20.0 );

	st = measure( scp, nSamples, buf, pgaMaxAttDb, dacCalZeroVolt, dvals );
	if ( st < 0 ) {
		goto bail;
	}
	for ( ch = 0; ch < nChannels; ++ch ) {
		printf("Offset[%u] (pgaAtt: %gdB): %lg clicks\n", ch, pgaMaxAttDb, dvals[ch]);
		/* not volt yet! */
		calData[ch].offsetVolt = dvals[ch];
	}

	st = measure( scp, nSamples, buf, pgaMaxAttDb, dacCalVolt, dvals );
	if ( st < 0 ) {
		goto bail;
	}

	/* compute scale */
	for ( ch = 0; ch < nChannels; ++ch ) {
		calData[ch].fullScaleVolt = (dacCalVolt - dacCalZeroVolt) / (dvals[ch] - calData[ch].offsetVolt);
		printf("Scale[%u] %lgV/click\n", ch, calData[ch].fullScaleVolt);
		/* scale at 0dB */
		calData[ch].fullScaleVolt /= pgaMaxAtt;
	}

	st = measure( scp, nSamples, buf, pgaMinAttDb, dacCalZeroVolt, dvals );
	if ( st < 0 ) {
		goto bail;
	}

	/* compute offsets
	 *   measured_offset = (input_offset/att) + adc_offset
	 *   input_offset    = (measured_offset(lo_att) - measured_offset(hi_att))/(1/lo_att - 1/hi_att)
	 *   adc_offset      = measured_offset(hi_att) - input_offset/hi_att
	 */
	for ( ch = 0; ch < nChannels; ++ch ) {
		printf("Offset[%u] (pgaAtt: %gdB): %lg clicks\n", ch, pgaMinAttDb, dvals[ch]);
		/* fullScaleVolt is still volt/click -- at max. attenuation */
		calData[ch].offsetVolt          = (calData[ch].offsetVolt - dvals[ch])/(1.0/pgaMaxAtt - 1.0/pgaMinAtt);
		calData[ch].postGainOffsetTick  = dvals[ch] - calData[ch].offsetVolt / pgaMinAtt;
		/* convert input offset to volts by applying the scale; since this offset */
		calData[ch].offsetVolt         *= calData[ch].fullScaleVolt;

		/* full-scale volt at 0dB */
		calData[ch].fullScaleVolt *= (double)(maxADCTicks);
	}

	if ( ! isnan( fullScaleVolt ) ) {
		printf("Overriding full-scale with user/cmdline setting %gV\n", fullScaleVolt);
		for ( ch = 0; ch < nChannels; ++ch ) {
			calData[ch].fullScaleVolt = fullScaleVolt;
		}
	}

	printCal( calData, nChannels );

	if ( doWrite ) {
		if ( ! (unitData = unitDataCreate( nChannels )) ) {
			fprintf(stderr, "Error - unitDataCreate() failed\n");
			goto bail;
		}
		for ( ch = 0; ch < nChannels; ++ch ) {
			st = unitDataSetCalData( unitData, ch, calData + ch );
			if ( st < 0 ) {
				fprintf( stderr, "Error; unitDataSetCalData failed: %s\n", strerror(-st));
				goto bail;
			}
		}
		if ( (st = writeNonvolatileChecked( scp, unitData )) < 0 ) {
			goto bail;
		}
	}

	rv = 0;

bail:
	if ( scp ) {
		if ( isnan( pgaMaxAttDb ) ) {
			pgaMaxAttDb = 40.0;
		}
		for ( ch = 0; ch < nChannels; ++ch ) {
			fecSetTermination( scp, ch, 0 );
		}
		measure( scp, 0, NULL, pgaMaxAttDb, 0.0, NULL );
	}
	scope_close( scp );
	fw_close( fw );
	unitDataFree( unitData );
	free( dvals );
	free( calData );
	free( buf );
	return rv;
}
