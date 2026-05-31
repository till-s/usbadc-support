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
lmh6882ReadReg(FWInfo *fw, uint8_t reg);

int
lmh6882WriteReg(FWInfo *fw, uint8_t reg, uint8_t val);

/* Attenuation in dB or negative value on error */
float
lmh6882GetAttDb(FWInfo *fw, unsigned channel);

int
lmh6882SetAttDb(FWInfo *fw, unsigned channel, float att);

/* RETURNS: previous power state or -1 on error.
 *          Sets new power state: off (state > 0), on (state == 0),
 *          unchanged (state < 0).
 *
 *          Note that all register bits are returned (see datasheet);
 *          nonzero: some stages off, zero: power on.
 */
int
lmh6882Power(FWInfo *fw, int state);

struct PGAOps;

extern struct PGAOps lmh6882PGAOps;

#ifdef __cplusplus
}
#endif
