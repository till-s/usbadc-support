#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int
fileMap(const char *fnam,  uint8_t **mapp, off_t *sizp, off_t creatsz, int readOnly);

#ifdef __cplusplus
}
#endif

#endif
