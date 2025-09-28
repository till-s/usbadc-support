#define _GNU_SOURCE
/* for exp10 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <getopt.h>
#include <unistd.h>

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

static int
measure(ScopePvt *scp, size_t nSamples, int16_t *buf, double pgaAtt, double dacVolt, double *result)
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
printCal(ScopeCalData *calData, unsigned nChannels, double fullScaleVolt)
{
unsigned ch;
	printf("Calibration Data:\n");
	printf("  Full Scale Volt: %lg\n", fullScaleVolt);
	printf("  Channel        | Relative Gain | Offset [V]\n");
	for ( ch = 0; ch < nChannels; ++ch ) {
		printf("  %-15u|%15.3lg|%15.3lg\n", ch, calData[ch].scaleRelat, calData[ch].offsetVolt);
	}
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
double                  dacCalVolt     = 0.3;
/* full-scale volt at 0dB pga attenuation */
double                  fullScaleVolt  = 0.0/0.0;
unsigned                nChannels      = 0;
int                     doWrite        = 0;
int                     doPrint        = 0;
int                     doErase        = 0;
double                  dval;
double                  pgaMinAtt;
double                  pgaMaxAtt;
double                  maxScale;
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

	if ( doPrint ) {
		double scl,lscl = 0.0/0.0;
		for ( ch = 0; ch < nChannels; ++ch ) {
			st = scope_get_full_scale_volt( scp, ch, &scl );
			if ( st < 0 ) {
				fprintf( stderr, "Error; scope_get_full_scale_volt() failed: %s\n", strerror(-st));
				goto bail;
			}
			if ( ch > 0 && scl != lscl ) {
				fprintf(stderr, "Warning: have different fullScaleVolt on channel[%u]!\n", ch);
			}
			lscl = scl;
		}
		printf("Current calibration parameters:\n");
	    printCal( calData, nChannels, scl );
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
		/* not volt yet! */
		calData[ch].offsetVolt = dvals[ch];
	}

	st = measure( scp, nSamples, buf, pgaMaxAttDb, dacCalVolt, dvals );
	if ( st < 0 ) {
		goto bail;
	}
	for ( ch = 0; ch < nChannels; ++ch ) {
		printf("Cal scale[%u] (pgaAtt: %lgdB): %lg clicks @ DAC %lg Volt\n", ch, pgaMaxAttDb, dvals[ch], dacCalVolt);
		/* not volt yet! */
		calData[ch].scaleRelat = dacCalVolt / (dvals[ch] - calData[ch].offsetVolt);
		printf("Full scale[%u] (pgaAtt: %lgdB, dac: %lgV): %lg volt\n", ch, pgaMaxAttDb, dacCalVolt, ((double)INT16_MAX)*calData[ch].scaleRelat);
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
		/* convert offset clicks into volt (min att may not be 0dB!) */
		calData[ch].offsetVolt  = pgaMinAtt * dvals[ch] * calData[ch].scaleRelat;
		printf("Offset[%u] (pgaMin: %gdB): %lg volt\n", ch, pgaMinAttDb, calData[ch].offsetVolt);
		/* full-scale volt at 0dB */
		calData[ch].scaleRelat *= (double)(INT16_MAX);
	}

	if ( isnan( fullScaleVolt ) ) {
		maxScale = 0.0;
		for ( ch = 0; ch < nChannels; ++ch ) {
			if ( fabs( calData[ch].scaleRelat ) > fabs( maxScale ) ) {
				maxScale = calData[ch].scaleRelat;
			}
		}

		fullScaleVolt = maxScale;
	}
	for ( ch = 0; ch < nChannels; ++ch ) {
		/* compute relative gain (of this channel) */
		calData[ch].scaleRelat = fullScaleVolt / calData[ch].scaleRelat;
	}

	printCal( calData, nChannels, fullScaleVolt );

	if ( doWrite ) {
		if ( ! (unitData = unitDataCreate( nChannels )) ) {
			fprintf(stderr, "Error - unitDataCreate() failed\n");
			goto bail;
		}
		for ( ch = 0; ch < nChannels; ++ch ) {
			st = unitDataSetScaleVolt( unitData, ch, fullScaleVolt );
			if ( st < 0 ) {
				fprintf( stderr, "Error; unitDataSetScaleVolt failed: %s\n", strerror(-st));
				goto bail;
			}
			st = unitDataSetScaleRelat( unitData, ch, calData[ch].scaleRelat  );
			if ( st < 0 ) {
				fprintf( stderr, "Error; unitDataSetScaleRelat failed: %s\n", strerror(-st));
				goto bail;
			}
			st = unitDataSetOffsetVolt( unitData, ch, calData[ch].offsetVolt );
			if ( st < 0 ) {
				fprintf( stderr, "Error; unitDataSetOffsetVolt failed: %s\n", strerror(-st));
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
