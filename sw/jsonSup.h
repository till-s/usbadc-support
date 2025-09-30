#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct ScopeParams;
struct ScopePvt;

/* Check if JSON support is available
 * RETURN: 0 if supported, -ENOTSUP if 
 *         unsupported.
 */
int
scope_json_supported();

/* Save current settings to JSON file.
 * RETURN 0 on success, negative error status on error.
 */
int
scope_json_save(ScopePvt *pvt, const char * filename, const ScopeParams *settings);

/* Load current settings from JSON file.
 * RETURN 0 on success, negative error status on error.
 */
int
scope_json_load(ScopePvt *pvt, const char * filename, ScopeParams *settings);

#ifdef __cplusplus
};
#endif


