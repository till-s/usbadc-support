#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct FWInfo;

/* If 'force' is nonzero the board is initialized
 * even if it appears to have been initialized already.
 *
 * RETURNS: zero on success, negative errno on error.
 */
int
scopeInit(struct FWInfo *fw, int force);

#ifdef __cplusplus
}
#endif
