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

typedef struct rbufvec {
	uint8_t *buf;
	size_t   len;
} rbufvec;

typedef struct tbufvec {
	const uint8_t *buf;
	size_t         len;
} tbufvec;


/* These routines return -2 on timeout */
int fifoXferFrame(int fd, uint8_t *cmdp, const uint8_t *tbuf, size_t tlen, uint8_t *rbuf, size_t rlen);

int fifoXferFrameVec(int fd, uint8_t *cmdp, const tbufvec *tbuf, size_t tcnt, const rbufvec *rbuf, size_t rcnt);

#ifdef __cplusplus
}
#endif

#endif
