/**LB-MIT
 *
 * MIT License
 *
 * Copyright (c) 2026 Till Straumann
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **LE-MIT*/

#pragma once

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
 * NOTE: The refclock output is apparently halted during VCO
 *       calibration!
 */
int
versaClkVCOCal(FWInfo *fw);

typedef enum VersaClkFODRoute {
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

int
versaClkGetFODRoute(FWInfo *fw, unsigned outp, VersaClkFODRoute *rte);

typedef enum VersaClkOutMode {
	OUT_CMOS = 1,
	OUT_LVDS = 3
} VersaClkOutMode;

typedef enum VersaClkOutSlew {
	SLEW_080 = 0,
	SLEW_085 = 1,
	SLEW_090 = 2,
	SLEW_100 = 3
} VersaClkOutSlew;

typedef enum VersaClkOutLevel {
	LEVEL_18 = 0,
	LEVEL_25 = 2,
	LEVEL_33 = 3
} VersaClkOutLevel;

int
versaClkSetOutCfg(FWInfo *fw, unsigned outp, VersaClkOutMode mode, VersaClkOutSlew slew, VersaClkOutLevel level);

#ifdef __cplusplus
}
#endif
