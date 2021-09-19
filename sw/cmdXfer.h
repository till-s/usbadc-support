#ifndef CMD_XFER_H
#define CMD_XFER_H


#ifdef __cplusplus
extern "C" {
#endif

int fifoOpen(const char *devn, unsigned speed);

int fifoClose(int fd);

int xferFrame(int fd, uint8_t *cmdp, const uint8_t *tbuf, size_t tlen, uint8_t *rbuf, size_t rlen);

#ifdef __cplusplus
}
#endif

#endif
