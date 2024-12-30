#include "scopeInit.h"
#include "fwComm.h"
#include "versaClkSup.h"
#include "max195xxSup.h"
#include "dac47cxSup.h"

#include <math.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>

typedef struct ClkOutConfig {
	int              outIdx;
	VersaClkOutMode  iostd;
	VersaClkOutSlew  slew;
	VersaClkOutLevel level;
} ClkOutConfig;

/* output index is one based; use index zero to indicate an invalid output */
#define NUM_CLK_OUTPUTS 4

typedef ClkOutConfig ClkOutConfigArray[NUM_CLK_OUTPUTS + 1];

static void
mkCfg(ClkOutConfigArray cfg, int idx, VersaClkOutMode iostd, VersaClkOutSlew slew, VersaClkOutLevel lvl)
{
	cfg[idx].outIdx = idx;
	cfg[idx].iostd  = iostd;
	cfg[idx].slew   = slew;
	cfg[idx].level  = lvl;
}

static int
boardClkInit( FWInfo *fw )
{
ClkOutConfigArray outCfg   = { {0}, };
int               OUT_EXT  = 0;
int               OUT_ADC  = 0;
int               OUT_FPGA = 0;
int               OUT_FOD1 = 0;
uint8_t           brdVers  = fw_get_board_version( fw );
double            fADC     = buf_get_sampling_freq( fw );
double            fRef     = fw_get_reference_freq( fw );
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

static int
fecInit(FWInfo *fw)
{
int ch, st;

	for ( ch = 0; ch < fw_get_num_channels( fw ); ++ch ) {
		if ( (st = fecSetTermination( fw, ch, 0 )) && -ENOTSUP != st ) {
			return st;
		}
		if ( (st = fecSetAtt( fw, ch, 0 )) && -ENOTSUP != st ) {
			return st;
		}
		if ( (st = fecSetACMode( fw, ch, 0 )) && -ENOTSUP != st ) {
			return st;
		}
		if ( (st = fecSetDACRangeHi( fw, ch, 1 )) && -ENOTSUP != st ) {
			return st;
		}
	}

	return 0;
}

static int
dacInit(FWInfo *fw)
{
int st,ch;

	if ( (st = dac47cxReset( fw )) < 0 ) {
		return st;
	}
	if ( (st = dac47cxSetRefSelection( fw, DAC47XX_VREF_INTERNAL_X1 )) < 0 ) {
		return st;
	}
	for ( ch = 0; ch < fw_get_num_channels( fw ); ++ch ) {
		if ( (st = dac47cxSetVolt( fw, ch, 0.0 )) < 0 ) {
			return st;
		}
	}

	return 0;
}

static int
adcInit(FWInfo *fw)
{
struct timespec per;
int             st,boardVers;

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
scopeInit(FWInfo *fw, int force)
{
int st;
	if ( ! force && 0 == max195xxDLLLocked( fw ) ) {
		return 0;
	}
	if ( (st = boardClkInit( fw )) ) {
		return st;
	}
	if ( (st = fecInit( fw )) ) {
		return st;
	}
	if ( (st = dacInit( fw )) ) {
		return st;
	}
	if ( (st = adcInit( fw )) ) {
		return st;
	}
	return 0;
}
