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

struct FWInfo;

typedef struct FWInfo FWInfo;

int
dac47cxReset(FWInfo *fw);

int
dac47cxReadReg(FWInfo *fw, unsigned reg, uint16_t *val);

int
dac47cxWriteReg(FWInfo *fw, unsigned reg, uint16_t val);

int
dac47cxSet(FWInfo *fw, unsigned channel, int val);

int
dac47cxGet(FWInfo *fw, unsigned channel, uint16_t *val);

typedef enum DAC47CXRefSelection { DAC47XX_VREF_INTERNAL_X1 } DAC47CXRefSelection;

int
dac47cxSetRefSelection(FWInfo *fw, DAC47CXRefSelection sel);

/* Detect the max. range supported by the device;
 * NOTE: Uses a HARD-RESET; all settings are lost !!!
 *       The other settings are not modified, i.e.,
 *       the state of the DAC is identical to the one
 *       produced by dac47cxReset().
 *       In particular: the value is set to 1/2 FS.
 */
int
dac47cxDetectMax(FWInfo *fw);

#ifdef __cplusplus
}
#endif
