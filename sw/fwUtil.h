#ifndef USBADC_FW_UTIL_H
#define USBADC_FW_UTIL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int
fileMap(const char *fnam,  uint8_t **mapp, off_t *sizp, off_t creatsz);

#ifdef __cplusplus
}
#endif

#endif
