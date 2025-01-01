#define _GNU_SOURCE
/* for exp10 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <getopt.h>

#include "fwComm.h"
#include "scopeSup.h"
#include "max195xxSup.h"
#include "unitData.h"
#include "unitDataFlash.h"

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
	return st;
}

static int
measure(ScopePvt *scp, size_t nSamples, int16_t *buf, double pgaAtt, double dacVolts, double *result)
{
unsigned nChannels = scope_get_num_channels( scp );
size_t   nElms     = nSamples * nChannels;
int      ch,n;
int      st;
uint16_t bufHdr;

	if ( isnan(pgaAtt) ) {
		pgaAtt = 40.0;
	}

	for ( ch = 0; ch < nChannels; ++ch ) {
		st = pgaSetAtt( scp, ch, pgaAtt );
		if ( st < 0 ) {
			fprintf( stderr, "Error; pgaSetAtt(%lg) failed: %s\n", pgaAtt, strerror(-st));
			goto bail;
		}

		st = dacSetVolts( scp, ch, dacVolts );
		if ( st < 0 ) {
			fprintf( stderr, "Error; dacSetVolts(%lg) failed: %s\n", dacVolts, strerror(-st));
			goto bail;
		}
	}

	st = buf_flush( scp );
	if ( st < 0 ) {
		fprintf( stderr, "Error; buf_flush() failed: %s\n", strerror(-st));
		goto bail;
	}

	if ( ! buf ) {
		return 0;
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
		/* not volts yet; just counts! */
		result[ch] = sum / (double)nSamples;
	}

bail:
	return st;
}

static void
usage(const char *name)
{
	printf("Usage: %s\n", name);
}

int
main(int argc, char **argv)
{
int16_t                *buf            = NULL;
FWInfo                 *fw             = NULL;
ScopePvt               *scp            = NULL;
int                     rv             = 1;
size_t                  nSamples       = 1024*1024;
const char             *devName        = "/dev/ttyACM0";
int                     allowPreInited = 0;
ScopeCalData           *calData        = NULL;
double                  pgaMinAttDb    = 0.0/0.0;
double                  pgaMaxAttDb    = 0.0/0.0;
double                 *dvals          = NULL;
UnitData               *unitData       = NULL;
double                  dacCalVolts    = 0.3;
/* full-scale volts at 0dB pga attenuation */
double                  fullScaleVolts = 0.0/0.0;
unsigned                nChannels      = 0;
int                     doWrite        = 0;
double                  dval;
double                  pgaMinAtt;
double                  pgaMaxAtt;
double                  minScale;
int                     st;
int                     ch;
AcqParams               acqParams;
int                     opt;
double                 *d_p;
size_t                 *z_p;

	while ( (opt = getopt( argc, argv, "a:A:d:D:F:hIn:w")) > 0 ) {
		d_p = 0;
		z_p = 0;
		switch ( opt ) {
			case 'a': d_p     = &pgaMinAttDb;             break;
			case 'A': d_p     = &pgaMaxAttDb;             break;
			case 'd': devName = optarg;                   break;
			case 'D': d_p     = &dacCalVolts;             break;
			case 'F': d_p     = &fullScaleVolts;          break;
			case 'I': allowPreInited = 1;                 break;
			case 'n': z_p     = &nSamples;                break;
			case 'w': doWrite = 1;                        break;
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
	if ( ! (scp = scope_open( fw )) ) {
		fprintf( stderr, "Error: unable to open Scope (wrong firmware?)\n");
		goto bail;
	}

	st = dacGetVolts( scp, 0, &dval );
	if ( st < 0 ) {
		fprintf( stderr, "Error with calibration DAC: %s\n", strerror(-st));
		goto bail;
	}
	if ( ! allowPreInited && (0 == max195xxDLLLocked( fw )) ) {
		fprintf( stderr, "Error: Scope seems already initialized; please power-cycle\n");
		goto bail;
	}
	
	st = scope_init( scp, 1 );
	if ( st < 0 ) {
		fprintf( stderr, "Error; scope_init failed: %s\n", strerror(-st));
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

	/* reset all calibration */
	st = scope_set_cal_data( scp, NULL, 0 );
	if ( st < 0 ) {
		fprintf( stderr, "Error; scope_set_cal_data(reset) failed: %s\n", strerror(-st));
		goto bail;
	}

	if ( isnan( pgaMinAttDb ) || isnan( pgaMaxAttDb ) ) {
		st = pgaGetAttRange( scp, (isnan(pgaMinAttDb) ? &pgaMinAttDb : NULL), (isnan(pgaMaxAttDb) ? &pgaMaxAttDb : NULL) );
		if ( st < 0 ) {
			fprintf( stderr, "Error; pgaGetAttRange failed: %s\n", strerror(-st));
			goto bail;
		}
	}

	for ( ch = 0; ch < nChannels; ++ch ) {
		st = fecSetACMode( scp, ch, 1 );
		if ( st < 0 ) {
			fprintf( stderr, "Error; fecSetACMode(1) failed: %s\n", strerror(-st));
			goto bail;
		}
		st = fecSetTermination( scp, ch, 1 );
		if ( st < 0 ) {
			fprintf( stderr, "Error; fecSetTermination(1) failed: %s\n", strerror(-st));
			goto bail;
		}
	}

	st = measure( scp, nSamples, buf, pgaMaxAttDb, 0.0, dvals );
	if ( st < 0 ) {
		goto bail;
	}
	for ( ch = 0; ch < nChannels; ++ch ) {
		printf("Offset[%u] (pgaAtt: %gdB): %lg clicks\n", ch, pgaMaxAttDb, dvals[ch]);
		/* not volts yet! */
		calData[ch].offsetVolts = dvals[ch];
	}

	st = measure( scp, nSamples, buf, pgaMaxAttDb, dacCalVolts, dvals );
	if ( st < 0 ) {
		goto bail;
	}
	for ( ch = 0; ch < nChannels; ++ch ) {
		printf("Cal scale[%u] (pgaAtt: %lgdB): %lg clicks @ DAC %lg Volts\n", ch, pgaMaxAttDb, dvals[ch], dacCalVolts);
		/* not volts yet! */
		calData[ch].scaleRelat = dacCalVolts / (dvals[ch] - calData[ch].offsetVolts);
		printf("Full scale[%u] (pgaAtt: %lgdB, dac: %lgV): %lg volts\n", ch, pgaMaxAttDb, dacCalVolts, ((double)INT16_MAX)*calData[ch].scaleRelat);
	}

	st = measure( scp, nSamples, buf, pgaMinAttDb, 0.0, dvals );
	if ( st < 0 ) {
		goto bail;
	}

	pgaMaxAtt = exp10( pgaMaxAttDb / 20.0 );
	pgaMinAtt = exp10( pgaMinAttDb / 20.0 );
	for ( ch = 0; ch < nChannels; ++ch ) {
		printf("Offset[%u] (pgaMin: %gdB): %lg clicks\n", ch, pgaMinAttDb, dvals[ch]);
		/* renormalize scale for att 0 dB */
		calData[ch].scaleRelat /= pgaMaxAtt;
		/* convert offset clicks into volts (min att may not be 0dB!) */
		calData[ch].offsetVolts = pgaMinAtt * dvals[ch] * calData[ch].scaleRelat;
		printf("Offset[%u] (pgaMin: %gdB): %lg volts\n", ch, pgaMinAttDb, calData[ch].offsetVolts);
		/* full-scale volts at 0dB */
		calData[ch].scaleRelat *= (double)(INT16_MAX);
	}

	minScale = calData[0].scaleRelat;
	for ( ch = 1; ch < nChannels; ++ch ) {
		if ( calData[ch].scaleRelat < minScale ) {
			minScale = calData[ch].scaleRelat;
		}
	}

	if ( isnan( fullScaleVolts ) ) {
		fullScaleVolts = minScale;
	}
	minScale /= fullScaleVolts;
	for ( ch = 0; ch < nChannels; ++ch ) {
		calData[ch].scaleRelat /= fullScaleVolts;
	}

	printf("Calibration Data:\n");
	printf("  Full Scale Volts: %lg\n", fullScaleVolts);
	printf("  Channel        | Relative Gain | Offset [V]\n");
	for ( ch = 0; ch < nChannels; ++ch ) {
		printf("  %-15u|%15.3lg|%15.3lg\n", ch, calData[ch].scaleRelat, calData[ch].offsetVolts);
	}


	if ( doWrite ) {
		if ( ! (unitData = unitDataCreate( nChannels )) ) {
			fprintf(stderr, "Error - unitDataCreate() failed\n");
			goto bail;
		}
		for ( ch = 0; ch < nChannels; ++ch ) {
			st = unitDataSetScaleVolts ( unitData, ch, fullScaleVolts );
			if ( st < 0 ) {
				fprintf( stderr, "Error; unitDataSetScaleVolts failed: %s\n", strerror(-st));
				goto bail;
			}
			st = unitDataSetScaleRelat ( unitData, ch, calData[ch].scaleRelat  );
			if ( st < 0 ) {
				fprintf( stderr, "Error; unitDataSetScaleRelat failed: %s\n", strerror(-st));
				goto bail;
			}
			st = unitDataSetOffsetVolts( unitData, ch, calData[ch].offsetVolts );
			if ( st < 0 ) {
				fprintf( stderr, "Error; unitDataSetOffsetVolts failed: %s\n", strerror(-st));
				goto bail;
			}
		}
		st = scope_write_unit_data_nonvolatile( scp, unitData );
		if ( st < 0 ) {
			fprintf( stderr, "Error; scope_write_unit_data_nonvolatile() failed: %s\n", strerror(-st));
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
