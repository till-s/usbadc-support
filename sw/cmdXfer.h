#ifndef CMD_XFER_H
#define CMD_XFER_H

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

int fifoOpen(const char *devn, unsigned speed);

int fifoClose(int fd);

/* set new debug level and return the previous one; if 'val < 0' then
 * the current level is unchanged (and returned).
 */
int fifoSetDebug(int val);

int fifoXferFrame(int fd, uint8_t *cmdp, const uint8_t *tbuf, size_t tlen, uint8_t *rbuf, size_t rlen);

#ifdef __cplusplus
}
#endif

#endif
