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
max195xxReadReg( FWInfo *fw, unsigned reg, uint8_t *val );

int
max195xxWriteReg( FWInfo *fw, unsigned reg, uint8_t val );

int
max195xxReset( FWInfo *fw );

/* Return 0 if DLL is locked, -3 if unlocked, other negative value if 
 * read error occurs
 */
int
max195xxDLLLocked( FWInfo *fw );

int
max195xxSetTiming( FWInfo *fw, int dclkDelay, int dataDelay);

typedef enum Max195xxTestMode { NO_TEST, RAMP_TEST, AA55_TEST } Max195xxTestMode;

int
max195xxSetTestMode(FWInfo *fw, Max195xxTestMode m);

typedef enum Max195xxMuxMode { MUX_OFF, MUX_PORT_A, MUX_PORT_B } Max195xxMuxMode;

typedef enum Max195xxCMVolt { CM_0450mV = 7, CM_0600mV = 6, CM_0750mV = 5, CM_0900mV = 0, CM_1050mV = 1, CM_1200mV = 2, CM_1350mV = 3 } Max195xxCMVolt;

int
max195xxGetMuxMode(FWInfo *fw);

int
max195xxSetMuxMode(FWInfo *fw, Max195xxMuxMode mode);

int
max195xxSetCMVolt( FWInfo *fw, Max195xxCMVolt cmA, Max195xxCMVolt cmB );

/* RETURNS: 100Ohm termination of (diff.) clock input
 *  1: on
 *  0: off
 * <0: error
 */
int
max195xxGetClkTermination( FWInfo *fw );

int
max195xxEnableClkTermination( FWInfo *fw, int on );

#ifdef __cplusplus
}
#endif
