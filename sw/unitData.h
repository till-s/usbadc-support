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
#include <sys/types.h>

#include <scopeCalData.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UnitData UnitData;

unsigned unitDataGetVersion(const UnitData *);
unsigned unitDataGetNumChannels(const UnitData *);

/* Array of all channels */
const ScopeCalData *
unitDataGetCalDataArray(const UnitData *);

/* For convenience: just one channel */

/* get pointer to const internal object */
const ScopeCalData *
unitDataGetCalData(const UnitData *, unsigned ch);

/* copy into user's memory */
int
unitDataCopyCalData(const UnitData *, unsigned ch, ScopeCalData *dest);

double
unitDataGetFullScaleVolt(const UnitData *, unsigned ch);

int
unitDataSetFullScaleVolt(UnitData *, unsigned ch, double volt);

double
unitDataGetOffsetVolt(const UnitData *, unsigned ch);

int
unitDataSetOffsetVolt(UnitData *, unsigned ch, double volt);

/* Create (empty) UnitData object */
UnitData *unitDataCreate(unsigned numChannels);

/* For convenience: just one channel */
int
unitDataSetCalData(const UnitData *, unsigned ch, const ScopeCalData *);

/* Parse serialized unitData into an (abstract) object;
 * RETURNS:
 *   - 0 on success, UnitData in *result
 *   - negative error status and NULL in *result on error
 */
int
unitDataParse(const UnitData **result, const uint8_t *buf, size_t bufSize);

/*
 * Free object created by unitDataParse()
 */
void
unitDataFree(const UnitData *ud);

size_t
unitDataGetSerializedSize(unsigned numChannels);

int
unitDataSerialize(const UnitData *ud, uint8_t *buf, size_t bufSize);

#ifdef __cplusplus
}
#endif
