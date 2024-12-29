#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct UnitData;
struct FWInfo;

/* Store/Retrieve unit data to/from the last flash block */

/* RETURN 0 on success, negative error code on failure */
int
unitDataFromFlash(const struct UnitData **udp, struct FWInfo *fw);

/* RETURN 0 on success, negative error code on failure */
int
unitDataToFlash(const struct UnitData *udp, struct FWInfo *fw);


#ifdef __cplusplus
}
#endif
