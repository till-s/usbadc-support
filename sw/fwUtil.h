#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* open and mmap a file;
 *
 * If 'readOnly' is nonzero the file is opened for reading only, otherwise
 * for read-write access.
 *
 * If 'creatsz' is nonzero and 'readOnly' is zero then the file is created
 * with the requested size.
 *
 * The file is mmapped and closed.
 *
 * RETURNS: zero on success or -errno of the failed operation.
 *
 * NOTE: the returned area must be 'munmap'ped by the caller.
 */
int
fileMap(const char *fnam,  uint8_t **mapp, off_t *sizp, off_t creatsz, int readOnly);

#ifdef __cplusplus
}
#endif
