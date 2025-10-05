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
