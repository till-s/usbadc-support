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
versaClkSetOutDiv(FWInfo *fw, unsigned outp, unsigned idiv, unsigned long fdiv);

int
versaClkSetOutDivFlt(FWInfo *fw, unsigned outp, double div);

int
versaClkSetOutEna(FWInfo *fw, unsigned outp, int ena);

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
