#ifndef USBADC_VERSACLK_SUP_H
#define USBADC_VERSACLK_SUP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FWInfo FWInfo;

int
versaClkSetFBDiv(FWInfo *fw, unsigned idiv, unsigned fdiv);

int
versaClkSetFBDivFlt(FWInfo *fw, double div);

int
versaClkGetFBDivFlt(FWInfo *fw, double *div);

int
versaClkSetOutDiv(FWInfo *fw, unsigned outp, unsigned idiv, unsigned long fdiv);

int
versaClkSetOutDivFlt(FWInfo *fw, unsigned outp, double div);

int
versaClkGetOutDivFlt(FWInfo *fw, unsigned outp, double *div);

/* recalibrate the VCO; seems necessary when loop parameters
 * are changed. We do this internally from versaClkSetFBDiv() & friends.
 */
int
versaClkVCOCal(FWInfo *fw);

typedef enum {
  NORMAL   = 0, /* PLL          -> FOD -> OUT */
  CASC_FOD = 1, /* PREVIOUS_OUT -> FOD -> OUT */
  CASC_OUT = 2, /* PREVIOUS_OUT --------> OUT */
  OFF      = 3  /* FOD and OUT disabled       */
} VersaClkFODRoute;

int
versaClkReadReg(FWInfo *fw, unsigned reg);

int
versaClkWriteReg(FWInfo *fw, unsigned reg, uint8_t val);

int
versaClkSetFODRoute(FWInfo *fw, unsigned outp, VersaClkFODRoute rte);

typedef enum {
	OUT_CMOS = 1
} VersaClkOutMode;

typedef enum {
	SLEW_080 = 0,
	SLEW_085 = 1,
	SLEW_090 = 2,
	SLEW_100 = 3
} VersaClkOutSlew;

typedef enum {
	LEVEL_18 = 0,
	LEVEL_25 = 2,
	LEVEL_33 = 3
} VersaClkOutLevel;

int
versaClkSetOutCfg(FWInfo *fw, unsigned outp, VersaClkOutMode mode, VersaClkOutSlew slew, VersaClkOutLevel level);

#ifdef __cplusplus
}
#endif

#endif
