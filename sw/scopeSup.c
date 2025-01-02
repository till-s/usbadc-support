#ifndef _GNU_SOURCE
#define _GNU_SOURCE
/* for exp10() */
#endif
#include "scopeSup.h"
#include "fwComm.h"
#include "versaClkSup.h"
#include "max195xxSup.h"
#include "dac47cxSup.h"
#include "unitData.h"
#include "unitDataFlash.h"
#include "tca6408FECSup.h"
#include "lmh6882Sup.h"
#include "ad8370Sup.h"


#include <math.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define FW_BUF_FLG_GET_SMPLSZ(flags) ( ( (flags) & FW_BUF_FLG_16B ) ? 9 + ( ( (flags) >> 1 ) & 7 ) : 8 )

#define BITS_FW_CMD_ACQ_MSK_SRC  7
#define BITS_FW_CMD_ACQ_SHF_SRC  0
#define BITS_FW_CMD_ACQ_SHF_EDG  3
#define BITS_FW_CMD_ACQ_SHF_TGO  4

#define BITS_FW_CMD_ACQ_IDX_MSK     0
#define BITS_FW_CMD_ACQ_LEN_MSK_V1  sizeof(uint8_t)
#define BITS_FW_CMD_ACQ_LEN_MSK_V2  sizeof(uint32_t)
#define BITS_FW_CMD_ACQ_LEN_SRC     1
#define BITS_FW_CMD_ACQ_LEN_LVL     2
#define BITS_FW_CMD_ACQ_LEN_NPT_V1  2
#define BITS_FW_CMD_ACQ_LEN_NPT_V2  3
#define BITS_FW_CMD_ACQ_LEN_NSM_V1  0
#define BITS_FW_CMD_ACQ_LEN_NSM_V2  3
#define BITS_FW_CMD_ACQ_LEN_AUT     2
#define BITS_FW_CMD_ACQ_LEN_DCM     3
#define BITS_FW_CMD_ACQ_LEN_SCL     4
#define BITS_FW_CMD_ACQ_LEN_HYS     2

#define BITS_FW_CMD_ACQ_TOT_LEN_V1 15
#define BITS_FW_CMD_ACQ_TOT_LEN_V2 24

#define BITS_FW_CMD_ACQ_DCM0_SHFT 20


typedef struct ScopePvt {
	FWInfo         *fw;
	size_t          memSize;
	unsigned        memFlags;
	AcqParams       acqParams;
	double          samplingFreq;
	unsigned        numChannels;
	int             sampleSize;
	double         *attOffset;
	double         *offsetVolts;
	double         *fullScaleVolts;
	PGAOps         *pga;
	FECOps         *fec;
	const UnitData *unitData;
} ScopePvt;


typedef struct ClkOutConfig {
	int              outIdx;
	VersaClkOutMode  iostd;
	VersaClkOutSlew  slew;
	VersaClkOutLevel level;
} ClkOutConfig;

/* output index is one based; use index zero to indicate an invalid output */
#define NUM_CLK_OUTPUTS 4

typedef ClkOutConfig ClkOutConfigArray[NUM_CLK_OUTPUTS + 1];

#define BRD_V1_TCA6408_SLA 0x20

static int
brdV1TCA6408Bits(FWInfo *fw, unsigned channel, I2CFECSupBitSelect which)
{
	switch ( which ) {
		case ATTENUATOR:   return channel ? (1<<2) : (1<<6);
		case TERMINATION:  return channel ? (1<<4) : (1<<5);
		case ACMODE:       return channel ? (1<<3) : (1<<7);
		case DACRANGE:     return channel ? (1<<0) : (1<<1);
		default:
		return -ENOTSUP;
	}
}

static void
mkCfg(ClkOutConfigArray cfg, int idx, VersaClkOutMode iostd, VersaClkOutSlew slew, VersaClkOutLevel lvl)
{
	cfg[idx].outIdx = idx;
	cfg[idx].iostd  = iostd;
	cfg[idx].slew   = slew;
	cfg[idx].level  = lvl;
}

static int
boardClkInit( ScopePvt *scp )
{
ClkOutConfigArray outCfg   = { {0}, };
int               OUT_EXT  = 0;
int               OUT_ADC  = 0;
int               OUT_FPGA = 0;
int               OUT_FOD1 = 0;
FWInfo           *fw       = scp->fw;
uint8_t           brdVers  = fw_get_board_version( fw );
double            fADC     = buf_get_sampling_freq( scp );
double            fRef     = scope_get_reference_freq( scp );
int               st, i;
double            fVCO, outDiv;

	if ( isnan( fADC ) || isnan( fRef ) ) {
		return -ENOTSUP;
	}
	switch ( brdVers ) {
		case 0:
			OUT_EXT   = 1;
			OUT_ADC   = 2;
			OUT_FPGA  = 4;
			mkCfg( outCfg, OUT_EXT,  OUT_CMOS, SLEW_100, LEVEL_18 );
			mkCfg( outCfg, OUT_ADC,  OUT_CMOS, SLEW_100, LEVEL_18 );
			mkCfg( outCfg, OUT_FPGA, OUT_CMOS, SLEW_100, LEVEL_18 );
			break;
		case 1:
			OUT_EXT   = 2;
			OUT_ADC   = 3;
			OUT_FPGA  = 1;
			OUT_FOD1  = 1;
			mkCfg( outCfg, OUT_FPGA, OUT_CMOS, SLEW_100, LEVEL_18 );
			mkCfg( outCfg, OUT_EXT,  OUT_CMOS, SLEW_100, LEVEL_18 );
			mkCfg( outCfg, OUT_ADC,  OUT_LVDS, SLEW_100, LEVEL_18 );
			break;
		case 2:
			OUT_EXT   = 2;
			OUT_ADC   = 3;
			OUT_FOD1  = 1;
			mkCfg( outCfg, OUT_FOD1, OUT_CMOS, SLEW_100, LEVEL_33 );
			mkCfg( outCfg, OUT_EXT,  OUT_CMOS, SLEW_100, LEVEL_33 );
			mkCfg( outCfg, OUT_ADC,  OUT_LVDS, SLEW_100, LEVEL_33 );
			break;
		default:
			return -ENOTSUP;
	}

	for ( i = 1; i < sizeof(outCfg)/sizeof(outCfg[0]); ++i ) {
		if ( outCfg[i].outIdx ) {
			st = versaClkSetOutCfg( fw, outCfg[i].outIdx, outCfg[i].iostd, outCfg[i].slew, outCfg[i].level );
			if ( st < 0 ) {
				return st;
			}
		}
	}

	if ( (st = versaClkGetFBDivFlt( fw, &fVCO )) < 0 ) {
		return st;
	}

	fVCO  *= fRef;
	outDiv = fVCO / fADC / 2.0; 
	if ( (st = versaClkSetOutDivFlt( fw, OUT_ADC, outDiv )) < 0 ) {
		return st;
	}
	if ( OUT_FOD1 ) {
		outDiv = fVCO / 1.0E6 / 2.0;
		if ( (st = versaClkSetOutDivFlt( fw, OUT_FOD1, outDiv )) < 0 ) {
			return st;
		}
	}
	if ( (st = versaClkSetOutDivFlt( fw, OUT_EXT, 1000.0/2.0 )) < 0 ) {
		return st;
	}
	if ( (st = versaClkSetFODRoute( fw, OUT_ADC, NORMAL )) < 0 ) {
		return st;
	}
	if ( (st = versaClkSetFODRoute( fw, OUT_EXT, CASC_FOD )) < 0 ) {
		return st;
	}
	return 0;
}

static void
computeAttOffset(double *attOffset, double *offsetVolts, double *scaleVolts, unsigned numChannels, const UnitData *ud)
{
int    i;

	for ( i = 0; i < numChannels; ++i ) {
		attOffset[i]   = 20.0*log10( unitDataGetScaleRelat( ud, i ) );
		offsetVolts[i] = unitDataGetOffsetVolts( ud, i ); 
		scaleVolts[i]  = unitDataGetScaleVolts( ud, i );
	}
}

int
scope_get_cal_data(ScopePvt *scp, ScopeCalData *calDataArray, unsigned nelms)
{
unsigned ch;
	if ( nelms > scope_get_num_channels( scp ) ) {
		return -EINVAL;
	}
	for ( ch = 0; ch < nelms; ++ch ) {
		calDataArray[ch].offsetVolts = scp->offsetVolts[ch];
		calDataArray[ch].scaleRelat  = exp10( scp->attOffset[ch]/20.0 );
	}
	return 0;
}

int
scope_set_cal_data(ScopePvt *scp, ScopeCalData *calDataArray, unsigned nelms)
{
unsigned ch;
	if ( nelms > scope_get_num_channels( scp ) ) {
		return -EINVAL;
	}
	if ( ! calDataArray ) {
		for ( ch = 0; ch < scope_get_num_channels( scp ); ++ch ) {
			scp->offsetVolts[ch]    = 0.0;
			scp->attOffset[ch]      = 0.0;
		}
	} else {
		for ( ch = 0; ch < nelms; ++ch ) {
			scp->offsetVolts[ch]    = calDataArray[ch].offsetVolts;
			scp->attOffset[ch]      = 20.0*log10( calDataArray[ch].scaleRelat );
		}
	}
	return 0;
}

int
scope_write_unit_data_nonvolatile(ScopePvt *scp, UnitData *unitData)
{
	return unitDataToFlash( unitData, scp->fw );
}

static int
fecInit(ScopePvt *scp)
{
int ch, st;

	for ( ch = 0; ch < scope_get_num_channels( scp ); ++ch ) {
		if ( (st = fecSetTermination( scp, ch, 0 )) && -ENOTSUP != st ) {
			return st;
		}
		if ( (st = fecSetAtt( scp, ch, 0 )) && -ENOTSUP != st ) {
			return st;
		}
		if ( (st = fecSetACMode( scp, ch, 0 )) && -ENOTSUP != st ) {
			return st;
		}
		if ( (st = fecSetDACRangeHi( scp, ch, 1 )) && -ENOTSUP != st ) {
			return st;
		}
	}

	return 0;
}

static int
dacInit(ScopePvt *scp)
{
int     st,ch;
FWInfo *fw = scp->fw;

	if ( (st = dac47cxReset( fw )) < 0 ) {
		return st;
	}
	if ( (st = dac47cxSetRefSelection( fw, DAC47XX_VREF_INTERNAL_X1 )) < 0 ) {
		return st;
	}
	for ( ch = 0; ch < scope_get_num_channels( scp ); ++ch ) {
		if ( (st = dac47cxSetVolt( fw, ch, 0.0 )) < 0 ) {
			return st;
		}
	}

	return 0;
}

static int
adcInit(ScopePvt *scp)
{
struct timespec per;
int             st,boardVers;
FWInfo         *fw = scp->fw;

	per.tv_sec  = 0;
	per.tv_nsec = 200*1000*1000;
	// sleep a little bit to let clock stabilize
	nanosleep( &per, NULL );

	if ( (st = max195xxReset( fw )) < 0 ) {
		return st;
	}

	// sleep a little bit to let the DLL lock
	nanosleep( &per, NULL );

	if ( 0 != max195xxDLLLocked( fw ) ) {
		return -EAGAIN;
	}

	boardVers = fw_get_board_version( fw );
	switch( boardVers ) {
		case 0:
			if ( (st = max195xxSetMuxMode( fw, MUX_PORT_B )) < 0 ) {
				return st;
			}
			/* Empirically found setting for the prototype board */
			if ( (st = max195xxSetTiming( fw, -1, 3 )) < 0 ) {
				return st;
			}
			/* set common-mode voltage (also important for PGA output)
			 *
			 * ADC: common mode input voltage range 0.4..1.4V
			 * ADC: controls common mode voltage of PGA
			 * PGA: output common mode voltage: 2*OCM
			 * Resistive divider 232/(232+178)
			 *
			 * PGA VOCM = 2*ADC_VCM
			 *
			 * Valid range for PGA: 2..3V (2.5V best)
			 *
			 * Common-mode register 8:
			 *   bit 6..4, 2..0:
			 *         000       -> 0.9 V
			 *         001       -> 1.05V
			 *         010       -> 1.2V
			 *
			 * With 1.2V -> VOCM of PGA becomes 2.4V   (near optimum)
			 *           -> VICM of ADC becomes 1.358V (close to max)
			 * With 1.05 -> VOCM of PGA becomes 2.1V   (close to min)
			 *           -> VICM of ADC becomes 1.188V (OK)
			 */
			if ( (st = max195xxSetCMVolt( fw, CM_1050mV, CM_1050mV )) < 0 ) {
				return st;
			}
			break;
		case 1:
			if ( (st = max195xxSetMuxMode( fw, MUX_PORT_B )) < 0 ) {
				return st;
			}
			/* Empirically found setting for the prototype board
			 * on artix board with constraints the 'nominal' settings
			 * seem much better
			 */
			if ( (st = max195xxSetTiming( fw, 0, 0 )) < 0 ) {
				return st;
			}
			if ( (st = max195xxEnableClkTermination( fw, 1 )) < 0 ) {
				return st;
			}
			break;
		case 2:
            /* Default timing seems fine */
			if ( (st = max195xxSetMuxMode( fw, MUX_PORT_A )) < 0 ) {
				return st;
			}
			if ( (st = max195xxEnableClkTermination( fw, 1 )) < 0 ) {
				return st;
			}
			break;
		default:
			fprintf(stderr,"WARNING: ADC not initialized; unsupported board version %d\n", boardVers);
			return -ENOTSUP;
	}

	return 0;
}

int
scope_init(ScopePvt *scp, int force)
{
int st;
	if ( ! force && 0 == max195xxDLLLocked( scp->fw ) ) {
		return 0;
	}
	if ( (st = boardClkInit( scp )) ) {
		return st;
	}
	if ( (st = fecInit( scp )) ) {
		return st;
	}
	if ( (st = dacInit( scp )) ) {
		return st;
	}
	if ( (st = adcInit( scp )) ) {
		return st;
	}
	return 0;
}

int
scope_get_full_scale_volts(ScopePvt *scp, unsigned channel, double *pVal)
{
	if ( channel >= scope_get_num_channels( scp ) ) {
		return -EINVAL;
	}
	*pVal = scp->fullScaleVolts[channel];
	return 0;
}

int
scope_set_full_scale_volts(ScopePvt *scp, unsigned channel, double fullScaleVolts)
{
	if ( channel >= scope_get_num_channels( scp ) ) {
		return -EINVAL;
	}
	scp->fullScaleVolts[channel] = fullScaleVolts;
	return 0;
}

unsigned
scope_get_num_channels(ScopePvt *scp)
{
	return scp->numChannels;
}

int
scope_get_current_scale(ScopePvt *scp, unsigned channel, double *pscl)
{
	if ( channel >= scope_get_num_channels( scp ) ) {
		return -EINVAL;
	}
	if ( pscl ) {
		double scl;
		double totAtt = 0.0;
		double att;
		int    st;
		// channel has already been checked; may ignore retval here
		scope_get_full_scale_volts( scp, channel, &scl );
		if ( 0.0 == scl ) {
			/* don't bother */
			return scl;
		}
		st = pgaGetAtt( scp, channel, &att );
		if ( st < 0 ) {
			if ( -ENOTSUP != st ) {
				return st;
			}
			/* NOTSUP means no additional attenuation */
		} else {
			totAtt += att;
		}
		st = fecGetAtt( scp, channel, &att );
		if ( st < 0 ) {
			if ( -ENOTSUP != st ) {
				return st;
			}
			/* NOTSUP means no additional attenuation */
		} else {
			totAtt += att;
		}
		*pscl = scl * exp10( totAtt/20.0 );
	}
	return 0;
}

double
scope_get_reference_freq(ScopePvt *scp)
{
	switch ( fw_get_board_version( scp->fw ) ) {
		case 0:
		case 2:
			return 25.0E6;
		break;
		case 1:
			return 26.0E6;
		break;
		default:
		break;
	}
	return 0.0/0.0;
}

ScopePvt *
scope_open(FWInfo *fw)
{
ScopePvt *sc;
int       i,st;
unsigned  boardVersion = fw_get_board_version( fw );
double    dfltScaleVolts = 1.0;

	if ( ! ( fw_get_features( fw ) & FW_FEATURE_ADC ) ) {
		fprintf(stderr, "scope_open: ERROR - FW has no ADC feature\n");
		return NULL;
	}

	if ( ! (sc = calloc( sizeof( *sc ), 1 )) ) {
		perror("scope_open(): no memory");
		goto bail;
	}
	sc->fw             = fw;

	if ( __fw_has_buf( fw, &sc->memSize, &sc->memFlags ) < 0 ) {
		fprintf(stderr, "scope_open: ERROR - unable to determine memory size\n");
		goto bail;
	}
	sc->sampleSize     = -ENOTSUP;
	sc->numChannels    = 2;

	sc->attOffset      = calloc( sizeof(*sc->attOffset), sc->numChannels );
	if ( ! sc->attOffset ) {
		perror("fw_open(): no memory");
		goto bail;
	}

	sc->offsetVolts    = calloc( sizeof(*sc->offsetVolts), sc->numChannels );
	if ( ! sc->offsetVolts ) {
		perror("fw_open(): no memory");
		goto bail;
	}

	sc->fullScaleVolts = calloc( sizeof(*sc->fullScaleVolts), sc->numChannels );
	if ( ! sc->fullScaleVolts ) {
		perror("fw_open(): no memory");
		goto bail;
	}


	sc->samplingFreq = 0.0/0.0;

	if ( (st = acq_set_params( sc, NULL, &sc->acqParams )) ) {
		fprintf(stderr, "Error %d: unable to read initial acquisition parameters\n", st);
	}

	sc->sampleSize = (sc->memFlags & FW_BUF_FLG_16B) ? 10 : 8;

	if ( fw_get_api_version( fw ) >= FW_API_VERSION_3 ) {
		if ( (st = __fw_get_sampling_freq_mhz( fw )) <= 0 ) {
			fprintf(stderr, "Error %d: unable to read sample frequency\n", st);
		} else {
			sc->samplingFreq = 1.0E6 * (double)st;
		}
		sc->sampleSize = FW_BUF_FLG_GET_SMPLSZ( sc->memFlags );
	} else {
		if ( 0 == boardVersion ) {
			sc->samplingFreq = 130.0E6;
		} else if ( 1 == fw_get_board_version( fw ) ) {
			sc->samplingFreq = 120.0E6;
		}
	}

	switch ( boardVersion ) {
		case 0:
			sc->pga            = &lmh6882PGAOps;
		break;

		case 2:
			/* fall through */
		case 1:
			sc->pga            = &ad8370PGAOps;
			sc->fec            = tca6408FECSupCreate( fw, sc->numChannels, BRD_V1_TCA6408_SLA, 0.0, 20.0, brdV1TCA6408Bits );
		break;

		default:
		break;
	}

	switch ( boardVersion ) {
		case 0:
			/* at full attenuation the gain is 6dB; final division by 10
			 * yields gain at 0dB.
			 */
			dfltScaleVolts = 0.75 / (2.0 * (232.0/(232.0+178.0))) / 10.0;
			break;
		case 1:
			/* at 40dB attenuation the PGA gain is -6dB * output load factor.
			 * Full scale of the ADC is 0.75V, translating to full-scale
			 * at the input by dividing by the PGA gain.
			 * Finally: divide by 100 to yield gain at attenuation 0
			 */
			dfltScaleVolts = 0.75 / (0.5 * 1.98/(1 + 98.0/200.0)) / 100.0;
			break;
		case 2:
			/* diff. load on this HW is 301 Ohm */
			dfltScaleVolts = 0.75 / (0.5 * 1.98/(1 + 98.0/301.0)) / 100.0;
			break;
		default:
		break;
	}
	for ( i = 0; i < sc->numChannels; ++i ) {
		sc->attOffset[i]      = 0.0;
		sc->offsetVolts[i]    = 0.0;
		sc->fullScaleVolts[i] = dfltScaleVolts;
	}

	st = unitDataFromFlash( &sc->unitData, fw );
	if ( st < 0 ) {
		if ( -ENODATA == st ) {
			UnitData *ud;
			fprintf(stderr, "WARNING: No calibration data found in flash; using defaults\n");
			ud = unitDataCreate( sc->numChannels );
			for ( i = 0; i < scope_get_num_channels( sc ); ++i ) {
				if ( (st = unitDataSetScaleVolts( ud, i, sc->fullScaleVolts[i] )) < 0 ) {
					goto bail;
				}
			}
			sc->unitData = ud;
		}
		if ( ! sc->unitData ) {
			goto bail;
		}
	} else {
		if ( unitDataGetNumChannels( sc->unitData ) != sc->numChannels ) {
			fprintf(stderr, "ERROR: # channels in calibration data does not match!\n");
			goto bail;
		}
	}

	computeAttOffset( sc->attOffset, sc->offsetVolts, sc->fullScaleVolts, sc->numChannels, sc->unitData );

	return sc;
bail:
	scope_close( sc );
	return NULL;
}

int
scope_set_calibration(ScopePvt *scp, unsigned channel, double fullScaleVolts, double offsetVolts);

void
scope_close(ScopePvt *scp)
{
	if ( scp ) {
		unitDataFree( scp->unitData );
		fecClose( scp );
		free( scp->attOffset );
		free( scp->offsetVolts );
		free( scp );
	}
}

unsigned long
buf_get_size(ScopePvt *scp)
{
	return scp->memSize;
}

unsigned
buf_get_flags(ScopePvt *scp)
{
	return scp->memFlags;
}

double
buf_get_sampling_freq(ScopePvt *scp)
{
	return scp->samplingFreq;
}

int
buf_get_sample_size(ScopePvt *scp)
{
	return scp->sampleSize;
}

int
buf_flush(ScopePvt *scp)
{
	return buf_read(scp, 0, 0, 0);
}

int
buf_read(ScopePvt *scp, uint16_t *hdr, uint8_t *buf, size_t len)
{
uint8_t h[2];
rbufvec v[2];
size_t  rcnt;
int     rv;
int     i;
const union {
	uint8_t  b[2];
	uint16_t s;
} isLE = { s : 1 };

	v[0].buf = h;
	v[0].len = sizeof(h);
	v[1].buf = buf;
	v[1].len = len;

	rcnt = (! hdr && 0 == len ? 0 : 2);

	uint8_t cmd = fw_get_cmd( 0 == len ? FW_CMD_ADC_FLUSH : FW_CMD_ADC_BUF );
	rv = fw_xfer_vec( scp->fw, cmd, 0, 0, v, rcnt );
	if ( hdr ) {
		*hdr = (h[1]<<8) | h[0];
	}
	if ( ! isLE.b[0] && !! (buf_get_flags(scp) & FW_BUF_FLG_16B) ) {
		for ( i = 0; i < (len & ~1); i+=2 ) {
			uint8_t tmp = buf[i];
			buf[i  ] = buf[i+1];
			buf[i+1] = tmp;
		}
	}
	if ( rv >= 2 ) {
		rv -= 2;
	}
	return rv;
}

int
buf_read_flt(ScopePvt *scp, uint16_t *hdr, float *buf, size_t nelms)
{
int       rv;
ssize_t   i;
int8_t   *i8_p  = (int8_t*)buf;
int16_t  *i16_p = (int16_t*)buf;
int       elsz  = ( (buf_get_flags( scp ) & FW_BUF_FLG_16B) ? 2 : 1 );


	rv = buf_read( scp, hdr, (uint8_t*)buf, nelms*elsz );
	if ( rv > 0 ) {
		if ( 2 == elsz ) {
			for ( i = nelms - 1; i >= 0; i-- ) {
				buf[i] = (float)(i16_p[i]);
			}
		} else {
			for ( i = nelms - 1; i >= 0; i-- ) {
				buf[i] = (float)(i8_p[i]);
			}
		}
	}
	return rv;
}

int
buf_read_int16(ScopePvt *scp, uint16_t *hdr, int16_t *buf, size_t nelms)
{
int       rv;
ssize_t   i;
int8_t   *i8_p  = (int8_t*)buf;
int       elsz  = ( (buf_get_flags( scp ) & FW_BUF_FLG_16B) ? 2 : 1 );


	rv = buf_read( scp, hdr, (uint8_t*)buf, nelms*elsz );
	if ( rv > 0 ) {
		if ( 2 == elsz ) {
			/* nothing to do :-) */
		} else {
			for ( i = nelms - 1; i >= 0; i-- ) {
				buf[i] = (((int16_t)(i8_p[i])) << 8);
			}
		}
	}
	return rv;
}

static void
putBuf(uint8_t **bufp, uint32_t val, int len)
{
int i;

	for ( i = 0; i < len; i++ ) {
		**bufp  = (val & 0xff);
		val   >>= 8;
		(*bufp)++;
	}
}

static uint32_t
getBuf(uint8_t **bufp, int len)
{
int      i;
uint32_t rv = 0;

	for ( i = len - 1; i >= 0; i-- ) {
		rv = (rv << 8 ) | (*bufp)[ i ];
	}
	(*bufp) += len;

	return rv;
}


/* Set new parameters and obtain previous parameters.
 * A new acquisition is started if any mask bit is set.
 *
 * Either 'set' or 'get' may be NULL with obvious semantics.
 */

int
acq_set_params(ScopePvt *scp, AcqParams *set, AcqParams *get)
{
uint8_t   cmd = fw_get_cmd( FW_CMD_ACQ_PARMS );
uint8_t   buf[BITS_FW_CMD_ACQ_TOT_LEN_V2];
uint8_t  *bufp;
uint8_t   v8;
uint32_t  v24;
uint32_t  v32;
uint32_t  nsamples;
int       got;
int       len;
uint32_t  smask = set ? set->mask : ACQ_PARAM_MSK_GET;
unsigned  apiVersion = fw_get_api_version( scp->fw );

	if ( ! scp ) {
		return -EINVAL;
	}

	if ( ! (fw_get_features( scp->fw ) & FW_FEATURE_ADC) ) {
		return -ENOTSUP;
	}

	if ( ! set || (ACQ_PARAM_MSK_GET == smask) ) {
		if ( get == &scp->acqParams ) {
			/* read the cache !! */
			if ( ! set ) {
				set   = get;
			}
		} else {
			if ( get ) {
				*get = scp->acqParams;
			}
			return 0;
		}
	}

	/* parameter validation and updating of cache */
	if ( ( smask & ACQ_PARAM_MSK_SRC ) ) {
		scp->acqParams.src    = set->src;
	}

	if ( ( smask & ACQ_PARAM_MSK_TGO ) ) {
		scp->acqParams.trigOutEn = set->trigOutEn;
	}

    if ( EXT == scp->acqParams.src && !! scp->acqParams.trigOutEn ) {
printf("Forcing ext trigger output OFF\n");
		scp->acqParams.trigOutEn = set->trigOutEn = 0;
		smask |= ACQ_PARAM_MSK_TGO;
	}

	if ( ( smask & ACQ_PARAM_MSK_EDG ) ) {
		scp->acqParams.rising = set->rising;
	}

	if ( ( smask & ACQ_PARAM_MSK_DCM ) ) {
        if ( 1 >= set->cic0Decimation ) {
printf("Forcing cic1 decimation to 1\n");
			set->cic1Decimation = 1;
        }
printf("Setting dcim %d x %d\n", set->cic0Decimation, set->cic1Decimation);
		/* If they change the decimation but not explicitly the scale
		 * then adjust the scale automatically
		 */
		if (  ! ( smask & ACQ_PARAM_MSK_SCL ) ) {
			smask         |= ACQ_PARAM_MSK_SCL;
			set->cic0Shift = 0;
			set->cic1Shift = 0;
			set->scale     = acq_default_cic1Scale( set->cic1Decimation );
		}
		scp->acqParams.cic0Decimation = set->cic0Decimation;
		scp->acqParams.cic1Decimation = set->cic1Decimation;
	}

	nsamples = scp->acqParams.nsamples;

	if ( ( smask & ACQ_PARAM_MSK_NSM ) ) {
		if ( apiVersion < FW_API_VERSION_2 ) {
			if ( set->nsamples != scp->memSize ) {
				return -ENOTSUP;
			}
			smask &= ~ACQ_PARAM_MSK_NSM;
		}
		if ( set->nsamples > scp->memSize ) {
			set->nsamples = scp->memSize;
		}
		if ( set->nsamples < 1 ) {
			set->nsamples = 1;
		}
		nsamples = set->nsamples;
        scp->acqParams.nsamples = set->nsamples;
	}

    bufp = buf + BITS_FW_CMD_ACQ_IDX_MSK;
	len  = (apiVersion >= FW_API_VERSION_2) ? BITS_FW_CMD_ACQ_LEN_MSK_V2 : BITS_FW_CMD_ACQ_LEN_MSK_V1;
    putBuf( &bufp, smask, len );

	v8  = (set->src & BITS_FW_CMD_ACQ_MSK_SRC)  << BITS_FW_CMD_ACQ_SHF_SRC;
	v8 |= (set->rising    ? 1 : 0)              << BITS_FW_CMD_ACQ_SHF_EDG;
	v8 |= (set->trigOutEn ? 1 : 0)              << BITS_FW_CMD_ACQ_SHF_TGO;
    putBuf( &bufp, v8, BITS_FW_CMD_ACQ_LEN_SRC );

	if ( ( smask & ACQ_PARAM_MSK_LVL ) ) {
		scp->acqParams.level      = set->level;
        scp->acqParams.hysteresis = set->hysteresis;
	}

	putBuf( &bufp, set->level, BITS_FW_CMD_ACQ_LEN_LVL );

	if ( ( smask & ACQ_PARAM_MSK_NPT ) ) {
		if ( (set->npts >= nsamples ) ) {
			set->npts = nsamples - 1;
			fprintf(stderr, "acq_set_params: WARNING npts >= nsamples requested; clipping to %" PRId32 "\n", set->npts);
		}
		scp->acqParams.npts = set->npts;
	}

	len  = (apiVersion >= FW_API_VERSION_2) ? BITS_FW_CMD_ACQ_LEN_NPT_V2 : BITS_FW_CMD_ACQ_LEN_NPT_V1;
	putBuf( &bufp, set->npts, len );

	/* this implicitly does nothing for V1 */
	len  = (apiVersion >= FW_API_VERSION_2) ? BITS_FW_CMD_ACQ_LEN_NSM_V2 : BITS_FW_CMD_ACQ_LEN_NSM_V1;
	/* firmware uses nsamples - 1 */
	putBuf( &bufp, nsamples - 1, len );

	if ( ( smask & ACQ_PARAM_MSK_AUT ) ) {
		if ( set->autoTimeoutMS > (1<<sizeof(uint16_t)*8) - 1 ) {
			set->autoTimeoutMS = (1<<sizeof(uint16_t)*8) - 1;
		}
		scp->acqParams.autoTimeoutMS = set->autoTimeoutMS;
	}

	putBuf( &bufp, set->autoTimeoutMS, BITS_FW_CMD_ACQ_LEN_AUT );

	if ( set->cic0Decimation > 16 ) {
		v24 = 15;
	} else if ( 0 == set->cic0Decimation ) {
		v24 = 1 - 1;
	} else {
		v24 = set->cic0Decimation - 1;
	}
    v24 <<= BITS_FW_CMD_ACQ_DCM0_SHFT;

	if ( 0 != v24 ) {
		if ( set->cic1Decimation > (1<<16) ) {
			v24 |= (1<<16) - 1;
		} else if ( 0 == set->cic1Decimation ) {
			v24 |= 1 - 1;
		} else {
			v24 |= set->cic1Decimation - 1;
		}
	}

	putBuf( &bufp, v24, BITS_FW_CMD_ACQ_LEN_DCM );

	v32 = set->cic0Shift;
    if ( v32 > 15 ) {
		v32 = 15;
	}
	v32 <<= 7;
	if ( set->cic1Shift > 16*4 - 1 ) {
		v32 |= 16*4 - 1;
	} else {
		v32 |= set->cic1Shift;
	}
	v32 <<= 20;

	v32 |= ( (set->scale >> (32 - 18)) & ( (1<<18) - 1 ) );

	if ( ( smask & ACQ_PARAM_MSK_SCL ) ) {
			scp->acqParams.cic0Shift = set->cic0Shift;
			scp->acqParams.cic1Shift = set->cic1Shift;
			scp->acqParams.scale     = set->scale;
	}

	putBuf( &bufp, v32, BITS_FW_CMD_ACQ_LEN_SCL );

	if ( apiVersion >= FW_API_VERSION_2 ) {
		putBuf( &bufp, set->hysteresis, BITS_FW_CMD_ACQ_LEN_HYS );
	}

	got = fw_xfer( scp->fw, cmd, buf, buf, sizeof(buf) );

	if ( got < 0 ) {
		fprintf(stderr, "Error: acq_set_params(); fifo transfer failed\n");
		return got;
	}

	len  = (apiVersion >= FW_API_VERSION_2) ? BITS_FW_CMD_ACQ_TOT_LEN_V2 : BITS_FW_CMD_ACQ_TOT_LEN_V1;

	if ( got < len ) {
		fprintf(stderr, "Error: acq_set_params(); fifo transfer short\n");
		return -ENODATA;
	}

	if ( ! get ) {
		return 0;
	}

	get->mask = ACQ_PARAM_MSK_ALL;

	len  = (apiVersion >= FW_API_VERSION_2) ? BITS_FW_CMD_ACQ_LEN_MSK_V2 : BITS_FW_CMD_ACQ_LEN_MSK_V1;
    bufp = buf + BITS_FW_CMD_ACQ_IDX_MSK + len;
    v8   = getBuf( &bufp, BITS_FW_CMD_ACQ_LEN_SRC );

	switch ( (v8 >> BITS_FW_CMD_ACQ_SHF_SRC) & BITS_FW_CMD_ACQ_MSK_SRC) {
		case 0:  get->src = CHA; break;
		case 1:  get->src = CHB; break;
		default: get->src = EXT; break;
	}

	get->rising         = !! ( (v8 >> BITS_FW_CMD_ACQ_SHF_EDG) & 1 );
	get->trigOutEn      = !! ( (v8 >> BITS_FW_CMD_ACQ_SHF_TGO) & 1 );

	get->level          = getBuf( &bufp, BITS_FW_CMD_ACQ_LEN_LVL );

	len  = (apiVersion >= FW_API_VERSION_2) ? BITS_FW_CMD_ACQ_LEN_NPT_V2 : BITS_FW_CMD_ACQ_LEN_NPT_V1;
	get->npts           = getBuf( &bufp, len );

	if ( (apiVersion < FW_API_VERSION_2) ) {
		get->nsamples   = scp->memSize;
	} else {
		/* firmware uses nsamples - 1 */
		get->nsamples   = getBuf( &bufp, BITS_FW_CMD_ACQ_LEN_NSM_V2 ) + 1;
	}

	get->autoTimeoutMS  = getBuf( &bufp, BITS_FW_CMD_ACQ_LEN_AUT );

	v32                 = getBuf( &bufp, BITS_FW_CMD_ACQ_LEN_DCM );

    get->cic0Decimation = ((v32 >> BITS_FW_CMD_ACQ_DCM0_SHFT) & 0xf   ) + 1; /* zero-based */
    get->cic1Decimation = ((v32 >>                         0) & 0xffff) + 1; /* zero-based */

	v32                 = getBuf( &bufp, BITS_FW_CMD_ACQ_LEN_SCL );

	get->cic0Shift      = ( v32 >> (20 + 7) ) & 0x1f;
	get->cic1Shift      = ( v32 >> (20    ) ) & 0x7f;
	get->scale          = ( v32 & ((1<<20) - 1)) << (32 - 18);

	if ( (apiVersion >= FW_API_VERSION_2) ) {
		get->hysteresis     = getBuf( &bufp, BITS_FW_CMD_ACQ_LEN_HYS );
	} else {
		get->hysteresis     = 0;
	}
	return 0;
}

/*
 * Helpers
 */
int
acq_manual(ScopePvt *scp)
{
	return acq_set_autoTimeoutMs(scp, 0);
}

int
acq_set_level(ScopePvt *scp, int16_t level, uint16_t hyst)
{
AcqParams p;
	p.mask          = ACQ_PARAM_MSK_LVL;
	p.level         = level;
    p.hysteresis    = hyst;
	return acq_set_params( scp, &p, 0 );
}

int
acq_set_npts(ScopePvt *scp, uint32_t npts)
{
AcqParams p;
	p.mask          = ACQ_PARAM_MSK_NPT;
	p.npts          = npts;
	return acq_set_params( scp, &p, 0 );
}

int
acq_set_nsamples(ScopePvt *scp, uint32_t nsamples)
{
AcqParams p;

	if ( (fw_get_api_version( scp->fw ) < FW_API_VERSION_2) || ! (fw_get_features( scp->fw ) & FW_FEATURE_ADC) ) {
		if ( nsamples == scp->memSize ) {
			return 0;
		}
		return -ENOTSUP;
	}
	p.mask          = ACQ_PARAM_MSK_NSM;
	p.nsamples      = nsamples;
	return acq_set_params( scp, &p, 0 );
}


#define CIC1_SHF_STRIDE  8
#define CIC1_STAGES      4
#define STRIDE_STAGS_RAT 2

int32_t
acq_default_cic1Scale(uint32_t cic1Decimation)
{
uint32_t nbits;
/* details based on the ration of shifter stride to number of CIC
 * stages being an integer number
 */
uint32_t shift;
double   scale;

	if ( cic1Decimation < 2 ) {
		shift = 0;
        nbits = 0;
	} else {
        nbits = (uint32_t)floor( log2( (double)(cic1Decimation - 1) ) ) + 1;
        /* implicit 'floor' in integer operation */
		shift = (nbits - 1) / STRIDE_STAGS_RAT;
	}
	/* Correct the CIC1 gain */

	scale = 1./pow((double)cic1Decimation, (double)CIC1_STAGES);

	/* Adjust for built-in shifter operation */
	scale /= exp2(-(double)(shift * CIC1_SHF_STRIDE));
	return (int32_t)floor( scale * (double)ACQ_SCALE_ONE );
}

int
acq_set_decimation(ScopePvt *scp, uint8_t cic0Decimation, uint32_t cic1Decimation)
{
AcqParams p;
	p.mask           = ACQ_PARAM_MSK_DCM;
	p.cic0Decimation = cic0Decimation;
	p.cic1Decimation = cic1Decimation;
	return acq_set_params( scp, &p, 0 );
}

int
acq_set_scale(ScopePvt *scp, uint8_t cic0RShift, uint8_t cic1RShift, int32_t scale)
{
AcqParams p;
	p.mask           = ACQ_PARAM_MSK_SCL;
	p.cic0Shift      = cic0RShift;
	p.cic1Shift      = cic1RShift;
	p.scale          = scale;
	return acq_set_params( scp, &p, 0 );
}


int
acq_set_source(ScopePvt *scp, TriggerSource src, int rising)
{
AcqParams p;
	p.mask          = ACQ_PARAM_MSK_SRC;
	p.src           = src;
	if ( rising ) {
		p.mask   |= ACQ_PARAM_MSK_EDG;
		p.rising  = rising > 0 ? 1 : 0;
	}
	if ( EXT == src ) {
		p.mask     |= ACQ_PARAM_MSK_TGO;
        p.trigOutEn = 0;
	}
	return acq_set_params( scp, &p, 0 );
}

int
acq_set_trig_out_en(ScopePvt *scp, int on)
{
AcqParams p;
	p.mask          = ACQ_PARAM_MSK_TGO;
	p.trigOutEn     = !!on;
	return acq_set_params( scp, &p, 0 );
}

int
acq_set_autoTimeoutMs(ScopePvt *scp, uint32_t timeout)
{
AcqParams p;
	p.mask          = ACQ_PARAM_MSK_AUT;
	p.autoTimeoutMS = timeout;
	return acq_set_params( scp, &p, 0 );
}

int
pgaReadReg(ScopePvt *scp, unsigned ch, unsigned reg)
{
	return scp && scp->pga && scp->pga->readReg ? scp->pga->readReg(scp->fw, ch, reg) : -ENOTSUP;
}

int
pgaWriteReg(ScopePvt *scp, unsigned ch, unsigned reg, unsigned val)
{
	return scp && scp->pga && scp->pga->writeReg ? scp->pga->writeReg(scp->fw, ch, reg, val) : -ENOTSUP;
}

int
pgaGetAttRange(ScopePvt*scp, double *min, double *max)
{
	return scp && scp->pga && scp->pga->getAttRange ? scp->pga->getAttRange(scp->fw, min, max) : -ENOTSUP;
}

int
pgaGetAtt(ScopePvt *scp, unsigned channel, double *attp)
{
int st;
double att;
	if ( channel >= scope_get_num_channels( scp ) ) {
		return -EINVAL;
	}
	
	st = scp && scp->pga && scp->pga->getAtt ? scp->pga->getAtt(scp->fw, channel, &att) : -ENOTSUP;
	if ( 0 == st ) {
		*attp = att - scp->attOffset[channel];
	}
	return st;
}

int
pgaSetAtt(ScopePvt *scp, unsigned channel, double att)
{
	if ( channel >= scope_get_num_channels( scp ) ) {
		return -EINVAL;
	}
	return scp && scp->pga && scp->pga->setAtt ? scp->pga->setAtt(scp->fw, channel, att + scp->attOffset[channel]) : -ENOTSUP;
}


int
fecGetAttRange(ScopePvt*scp, double *min, double *max)
{
	return scp && scp->fec && scp->fec->getAttRange ? scp->fec->getAttRange(scp->fec, min, max) : -ENOTSUP;
}

int
fecGetAtt(ScopePvt *scp, unsigned channel, double *att)
{
	if ( channel >= scope_get_num_channels( scp ) ) return -EINVAL;
	return scp && scp->fec && scp->fec->getAtt ? scp->fec->getAtt(scp->fec, channel, att) : -ENOTSUP;
}

int
fecSetAtt(ScopePvt *scp, unsigned channel, double att)
{
	if ( channel >= scope_get_num_channels( scp ) ) return -EINVAL;
	return scp && scp->fec && scp->fec->setAtt ? scp->fec->setAtt(scp->fec, channel, att) : -ENOTSUP;
}

int
fecGetACMode(ScopePvt *scp, unsigned channel)
{
	if ( channel >= scope_get_num_channels( scp ) ) return -EINVAL;
	return scp && scp->fec && scp->fec->getACMode ? scp->fec->getACMode(scp->fec, channel) : -ENOTSUP;
}

int
fecSetACMode(ScopePvt *scp, unsigned channel, unsigned val)
{
	if ( channel >= scope_get_num_channels( scp ) ) return -EINVAL;
	return scp && scp->fec && scp->fec->setACMode ? scp->fec->setACMode(scp->fec, channel, val) : -ENOTSUP;
}

int
fecGetTermination(ScopePvt *scp, unsigned channel)
{
	if ( channel >= scope_get_num_channels( scp ) ) return -EINVAL;
	return scp && scp->fec && scp->fec->getTermination ? scp->fec->getTermination(scp->fec, channel) : -ENOTSUP;
}

int
fecSetTermination(ScopePvt *scp, unsigned channel, unsigned	val)
{
	if ( channel >= scope_get_num_channels( scp ) ) return -EINVAL;
	return scp && scp->fec && scp->fec->setTermination ? scp->fec->setTermination(scp->fec, channel, val) : -ENOTSUP;
}

int
fecGetDACRangeHi(ScopePvt *scp, unsigned channel)
{
	if ( channel >= scope_get_num_channels( scp ) ) return -EINVAL;
	return scp && scp->fec && scp->fec->getDACRangeHi ? scp->fec->getDACRangeHi(scp->fec, channel) : -ENOTSUP;
}

int
fecSetDACRangeHi(ScopePvt *scp, unsigned channel, unsigned	val)
{
	if ( channel >= scope_get_num_channels( scp ) ) return -EINVAL;
	return scp && scp->fec && scp->fec->setDACRangeHi ? scp->fec->setDACRangeHi(scp->fec, channel, val) : -ENOTSUP;
}

void
fecClose(ScopePvt *scp)
{
	if ( scp && scp->fec && scp->fec->close ) {
		scp->fec->close( scp->fec );
	}
}

int
dacGetVolts(ScopePvt *scp, unsigned channel, double *pvolts)
{
float val;
int   st;
	if ( channel >= scope_get_num_channels( scp ) ) {
		return -EINVAL;
	}
	if ( (st = dac47cxGetVolt( scp->fw, channel, &val )) < 0 ) {
		return st;
	}
	*pvolts = (double)val + scp->offsetVolts[channel];
	return 0;
}

int
dacSetVolts(ScopePvt *scp, unsigned channel, double volts)
{
float val;
	if ( channel >= scope_get_num_channels( scp ) ) {
		return -EINVAL;
	}
	val = (float)volts - scp->offsetVolts[channel];
	return dac47cxSetVolt( scp->fw, channel, val );
}

int
dacGetVoltsRange(ScopePvt *scp, double *pvoltsMin, double *pvoltsMax)
{
float  voltMin, voltMax;
int    ch;
double maxOff = 0.0;
	for ( ch = 0; ch < scope_get_num_channels( scp ); ++ch ) {
		if ( maxOff < fabs( scp->offsetVolts[ch] ) ) {
			maxOff = fabs( scp->offsetVolts[ch] );
		}
	}
	dac47cxGetRange( scp->fw, NULL, NULL, &voltMin, &voltMax );
	if ( pvoltsMin ) {
		*pvoltsMin = (double)voltMin + maxOff;
	}
	if ( pvoltsMax ) {
		*pvoltsMax = (double)voltMax - maxOff;
	}
	return 0;
}
