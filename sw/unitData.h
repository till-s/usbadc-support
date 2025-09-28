#pragma once

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UnitData UnitData;

unsigned unitDataGetVersion(const UnitData *);
unsigned unitDataGetNumChannels(const UnitData *);
double   unitDataGetScaleVolt(const UnitData *, unsigned ch);
double   unitDataGetScaleRelat(const UnitData *, unsigned ch);
double   unitDataGetOffsetVolt(const UnitData *, unsigned ch);

/* Create (empty) UnitData object */
UnitData *unitDataCreate(unsigned numChannels);
/* returns negative error status or 0 on success */
int      unitDataSetScaleVolt(UnitData *ud, unsigned ch, double value);
int      unitDataSetScaleRelat(UnitData *ud, unsigned ch, double value);
int      unitDataSetOffsetVolt(UnitData *ud, unsigned ch, double value);


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
