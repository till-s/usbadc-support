/**LB-MIT
 *
 * MIT License
 *
 * Copyright (c) 2026 Till Straumann
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **LE-MIT*/

#pragma once

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CmdFifoRec *CmdFifo;

typedef struct CmdFifoConfig {
	/* name of the device to open
	 */
	const char *ttyName;
	/* filedescriptor to use; if both, ttyFd and ttyName
	 * are set then ttyFd is ignored!
	 * Furthermore: when ttyFd is 0 (stdin) then it is
	 * treated as unset (equivalent to -1) *unless*
	 * the CMD_FIFO_TTY_STDIN flag is set. Set this in the unlikely
	 * case you really want to use stdin.
	 * Ignoring the value of zero allows you to simply memset
	 * the CmdFifoConfig to all-zero and have a blank configuration.
	 */
	int         ttyFd;
	/* number of bytes that may be outstanding (sent-received);
	 * useful if the transport has no flow-control and the
	 * peer has limited buffer space.
	 * Setting to zero is equivalent to selecting a very large
	 * window. (Default: 0)
     */
	size_t      windowSize;
	/* Speed; this must be one of the termios macros, e.g., B115200
	 * (Default: B115200)
	 */
	unsigned    ttySpeed;

	/* The codec to use (default: bytestuff)
	 *
	 * Note: The library must be built with COBS support
	 * to make this available; otherwise fifoOpen returns
	 * -ENOTSUP.
	 */
#define CMD_FIFO_CFG_CODEC_BYTESTUFF  0
#define CMD_FIFO_CFG_CODEC_COBS       1
	unsigned    codec;
	/* Only the settings for which the associated flag is
	 * set are used; for others a default is chosen.
	 * Note that one of 'ttyName' or 'ttyFd' must always
	 * be provided; they have no associated flag.
	 */
#define CMD_FIFO_CFG_WINSIZE     (1<<0)
#define CMD_FIFO_CFG_TTY_SPEED   (1<<1)
#define CMD_FIFO_CFG_TTY_STDIN   (1<<2)
	unsigned    flags;
} CmdFifoConfig;

int fifoOpenConfig(CmdFifo *pfifo, const CmdFifoConfig *pcfg);

/* Note that the tty name is never returned by this call
 * but the associated file-descriptor.
 *
 * RETURN: 0 on success negative error on failure.
 */
int fifoGetConfig(CmdFifo pfifo, CmdFifoConfig *pcfg);

int fifoOpen(CmdFifo *pfifo, const char *devn, unsigned speed);

int fifoOpenFd(CmdFifo *pfifo, int fd);

int fifoClose(CmdFifo fifo);

/* set new debug level and return the previous one; if 'val < 0' then
 * the current level is unchanged (and returned).
 */
int fifoSetDebug(CmdFifo fifo, int val);

typedef struct rbufvec {
	uint8_t *buf;
	size_t   len;
} rbufvec;

typedef struct tbufvec {
	const uint8_t *buf;
	size_t         len;
} tbufvec;


/* These routines return -ETIMEDOUT on timeout */
int fifoXferFrame(CmdFifo fifo, uint8_t *cmdp, const uint8_t *tbuf, size_t tlen, uint8_t *rbuf, size_t rlen);

int fifoXferFrameVec(CmdFifo fifo, uint8_t *cmdp, const tbufvec *tbuf, size_t tcnt, const rbufvec *rbuf, size_t rcnt);

#ifdef __cplusplus
}
#endif
