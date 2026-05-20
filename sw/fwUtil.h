#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NOTE: '*mapp' must be set to MAP_FAILED before calling this! */
int
fileMap(const char *fnam,  uint8_t **mapp, off_t *sizp, off_t creatsz, int readOnly);

#ifdef __cplusplus
}
#endif
