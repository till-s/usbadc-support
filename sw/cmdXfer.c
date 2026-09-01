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

#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>

#include "cmdXfer.h"

#define MAXLEN 500

#define COMMA  0xCA
#define ESCAP  0x55

struct CmdFifoRec {
	int       fd;
	int       dbg;
	int       ownFd;
};


typedef enum { RX, ESC, DONE } RxState;

/* Basic communication with the USB-FIFO (FT245), byte-stuffer/de-stuffer and command multiplexer in firmware */

int fifoTtyOpen(const char *devn, unsigned speed)
{
int                fd   = -1;
char               msg[256];
struct termios     atts;
size_t             i,put;
int                err  = 0;

	/* Special trick: open the TTY twice. If another program (minicom!)
	 * already has the port opened (but w/o TIOCEXCL) then our first
	 * open succeeds and the subsequent TIOCEXCL persists/sticks (since the
	 * TTY remains open [linux-5.4]) which causes the second open() to fail.
	 */

	i = 0;
	while ( 1 ) {

		if ( (fd = open(devn, O_RDWR)) < 0 ) {
			snprintf(msg, sizeof(msg), "unable to open device '%s'", devn);
			perror(msg);
			if ( EBUSY == errno ) {
				fprintf(stderr, "another application probably holds the port open? (%i)\n", (int)i);
			}
			goto bail;
		}

		/* Hack - if we are using the simulator/pty then the exclusive flag
		 * will survive an open-close-open cycle and so we skip the safety
		 * test...
		 */
		if ( 0 == ttyname_r( fd, msg, sizeof(msg) ) && strstr(msg, "/pts/") ) {
			i = 1;
		} else {
			if ( ioctl( fd, TIOCEXCL ) ) {
				snprintf(msg, sizeof(msg), "setting TIOCEXCL failed");
				perror(msg);
				goto bail;
			}
		}
		if ( 1 == i ) {
			break;
		}

		close( fd );
		fd = -1;
		i++;
	}

	if ( tcgetattr( fd, &atts ) ) {
		perror( "tcgetattr failed" );
		goto bail;
	}

	cfmakeraw( &atts );
	if ( cfsetspeed( &atts, speed ) ) {
		perror( "cfsetspeed failed" );
		goto bail;
	}

	if ( tcsetattr( fd, TCSAFLUSH, &atts ) ) {
		perror( "tcsetattr failed" );
		goto bail;
	}

/* Should not be required for pselect()
	if ( -1 == (flgs = fcntl( fd, F_GETFL )) ) {
		perror("fcntl(F_GETFL) failed");
		goto bail;
	}

	if ( -1 == fcntl( fd, F_SETFL, (flgs | O_NONBLOCK) ) ) {
		perror("fcntl(F_SETFL,O_NONBLOCK) failed");
		goto bail;
	}
*/

	for ( i = 0; i < 4; i++ ) {
		msg[i] = COMMA;
	}
	put = write( fd, msg, i );
	if ( i != put ) {
		perror("Writing syncing commas failed");
		err = put < 0 ? -errno : -EIO;
		goto bail;
	}

	return fd;

bail:
	if ( fd >= 0 ) {
		close( fd );
	}
	return err ? err : -errno;
}

int fifoOpen(CmdFifo *pfifo, const char *devn, unsigned speed)
{
	int st;
	int fd = fifoTtyOpen(devn, speed);
	if ( fd < 0 ) {
		return fd;
	}
	if ( (st = fifoOpenFd(pfifo, fd)) ) {
		close(fd);
	}
	(*pfifo)->ownFd = 1;
	return st;
}

int fifoOpenFd(CmdFifo *pfifo, int fd) {
	if ( fd < 0 ) {
		return fd;
	}
	if ( ! pfifo ) {
		return -EINVAL;
	}
	if ( ! (*pfifo = calloc(1, sizeof(*pfifo))) ) {
		return -ENOMEM;
	}
	(*pfifo)->fd = fd;
	return 0;
}

int
fifoClose(CmdFifo fifo)
{
	if ( fifo ) {
		if ( fifo->ownFd ) {
			close ( fifo->fd );
		}
		free( fifo );
	}
	return 0;
}

static void prb(const char * hdr, const uint8_t *b, size_t l)
{
	size_t k;

	printf("%s\n", hdr);
	for ( k = 0; k < l; k++ ) {
		printf("0x%02x ", b[k]);
		if ( 0xf == (k & 0xf) ) {
			printf("\n");
		}
	}
	if ( 0 != ( k & 0xf ) ) {
		printf("\n");
	}
}

typedef struct DestufferCtx {
	RxState        state;
	const uint8_t *src;
	size_t         srcIndex;
	size_t         srcSize;
	uint8_t       *dst;
	size_t         dstIndex;
	size_t         dstSize;
} DestufferCtx;

typedef struct StufferCtx {
	uint8_t       *dst;
	size_t         dstIndex;
	size_t         dstSize;
	const uint8_t *src;
	size_t         srcSize;
	size_t         srcIndex;
} StufferCtx;


static size_t
stuffByte(uint8_t *dbuf, ssize_t dbufsz, const uint8_t *buf)
{
size_t rval = 0;

	/* Stuff dbuf */
	if ( ( COMMA == *buf ) || ( ESCAP == *buf ) ) {
		if ( dbufsz <= 0 ) {
			fprintf(stderr, "Stuff buffer overrun\n");
			abort();
		}
		dbuf[rval] = ESCAP;
		rval++;
		dbufsz--;
	}
	if ( dbufsz <= 0 ) {
		fprintf(stderr, "Stuff buffer overrun\n");
		abort();
	}
	dbuf[rval] = *buf;
	rval++;
	return rval;
}

static int
stuff(StufferCtx *ctx)
{
	while ( ( ctx->srcSize > ctx->srcIndex ) ) {
		if ( ctx->dstIndex >= ctx->dstSize - 3 ) {
			return 0; 
		}
		/* Stuff tbuf */
		ctx->dstIndex += stuffByte( ctx->dst + ctx->dstIndex, ctx->dstSize - ctx->dstIndex, ctx->src + ctx->srcIndex );
		ctx->srcIndex++;
	}
	return 1;
}

static void
stuffInit(StufferCtx *ctx, uint8_t *tbufs, size_t tsize)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->dst     = tbufs;
	ctx->dstSize = tsize;
}

static void
destuffInit(DestufferCtx *ctx, const uint8_t *rbufs, size_t rsize)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->src        = rbufs;
	ctx->srcSize    = rsize;
	ctx->state      = RX;
}

/* Returns
 *  1 -> comma detected
 * -1 -> no progress
 *  0 -> other conditions
 */
static int
destuff(DestufferCtx *ctx)
{
size_t          j;
uint8_t        *dstp;
uint8_t        *dstend;
const uint8_t  *rbufs;
int             rv = 0;

	rbufs  = ctx->src;
	dstp   = ctx->dst + ctx->dstIndex;
	dstend = ctx->dst + ctx->dstSize;
	
	for ( j = ctx->srcIndex; j < ctx->srcSize; j++ ) {
		if ( ESC != ctx->state && COMMA == rbufs[j] ) {
			ctx->state = DONE;
			++j; /* consume source */
			rv = 1;
			break;
		} else if ( ESC != ctx->state && ESCAP == rbufs[j] ) {
			ctx->state = ESC;
		} else {
			ctx->state = RX;
			if ( dstp >= dstend ) {
				/* destination exhausted */
				break;
			} else {
				ctx->state = RX;
				*dstp++ = rbufs[j];
			}
		}
	}
	ctx->dstIndex = dstp - ctx->dst;
	if ( ctx->srcIndex == j ) {
		return -1;
	}
	ctx->srcIndex = j;
	return rv;
}

int
fifoSetDebug(CmdFifo fifo, int val)
{
int oldVal = fifo->dbg;
	if ( val >= 0 ) {
		fifo->dbg = val;
	}
	return oldVal;
}

int
fifoXferFrame(CmdFifo fifo, uint8_t *cmdp, const uint8_t *tbuf, size_t tlen, uint8_t *rbuf, size_t rlen)
{
tbufvec tvec[1];
rbufvec rvec[1];
	tvec[0].buf = tbuf;
	tvec[0].len = tlen;

	rvec[0].buf = rbuf;
	rvec[0].len = rlen;

	return fifoXferFrameVec(fifo, cmdp, tvec, tlen ? 1 : 0, rvec, rlen ? 1 : 0 );
}

int
fifoXferFrameVec(CmdFifo fifo, uint8_t *cmdp, const tbufvec *tbuf, size_t tcnt, const rbufvec *rbuf, size_t rcnt)
{
uint8_t         tbufs[MAXLEN];
uint8_t         rbufs[MAXLEN];
size_t          i, rlens, puts, tlens, tidx, tot, ridx;
fd_set          rfds, tfds;
int             eofSent     = 0;
struct timespec timeout;
StufferCtx      stuffCtx;
DestufferCtx    destuffCtx;
int             cmdReadback = 0;
int             warned      = 0;
int             progress;

	stuffInit( &stuffCtx, tbufs, sizeof(tbufs) );
	destuffInit( &destuffCtx, rbufs, sizeof(rbufs) );

	tot   = 0;
	tlens = 0;
	rlens = sizeof(rbufs);
	puts  = 0;
	ridx  = 0;

	while ( ridx < rcnt && 0 == rbuf[ridx].len ) {
		++ridx;
	}

	warned = (ridx < rcnt) ? 0 : 1;

	if ( cmdp ) {
		stuffCtx.src      = cmdp;
		stuffCtx.srcSize  = sizeof(*cmdp);
		stuffCtx.srcIndex = 0;
		stuff( &stuffCtx );

		cmdReadback       = 1;
	}

	/* *after* potentially stuffing the command byte */
	tlens = stuffCtx.dstIndex;

	tidx = 0;
	stuffCtx.srcIndex = 0;
	stuffCtx.srcSize  = 0;
	for ( tidx = 0; tidx < tcnt; ++tidx ) {
		stuffCtx.src     = tbuf[tidx].buf;
		stuffCtx.srcSize = tbuf[tidx].len;
		if ( stuffCtx.srcSize > 0 ) {
			break;
		}
	}

	while ( ( ! eofSent ) || ( tlens > 0 ) || (DONE != destuffCtx.state ) ) {
		FD_ZERO( &rfds );
		FD_ZERO( &tfds );

		if ( ( 0 == tlens ) ) {
			puts = 0;
			stuffCtx.dstIndex = 0;
			if ( ( stuffCtx.srcIndex < stuffCtx.srcSize ) ) {
				/* stuff() returns nonzero if the entire source has been consumeds */
				while ( tidx < tcnt && stuff(&stuffCtx) ) {
					while ( stuffCtx.srcIndex == stuffCtx.srcSize && ++tidx < tcnt ) {
						stuffCtx.srcIndex = 0;
						stuffCtx.srcSize  = tbuf[tidx].len;
						stuffCtx.src      = tbuf[tidx].buf;
					}
				}
			} else if ( ! eofSent ) {
				stuffCtx.dst[stuffCtx.dstIndex] = COMMA;
				stuffCtx.dstIndex++;
				eofSent                         = 1;
			}
			tlens = stuffCtx.dstIndex;
		}

		if ( tlens > 0 ) {
			FD_SET( fifo->fd, &tfds );
		}
		if ( DONE != destuffCtx.state ) {
			FD_SET( fifo->fd, &rfds );
		}

		timeout.tv_sec  = 1;
		timeout.tv_nsec = 0;
		i = pselect( fifo->fd + 1, &rfds, &tfds, 0, &timeout, 0 );

		if ( i <= 0 ) {
			if ( 0 == i ) {
				/* Timeout */
				return -ETIMEDOUT;
			}
			perror("select failure");
			goto bail;
		}

		if ( FD_ISSET( fifo->fd, &tfds ) ) {
			if ( fifo->dbg > 0 ) {
				prb( "Sending:", tbufs + puts, tlens );
			}
			if ( (i = write(fifo->fd, tbufs + puts, tlens)) <= 0 ) {
				perror("fifoXferFrame: writing FIFO failed");
				if ( 0 == i ) {
					errno = EIO;
				}
				goto bail;
			}
			puts  += i;
			tlens -= i;
		}
		if ( FD_ISSET( fifo->fd, &rfds ) ) {
			if ( (i = read(fifo->fd, rbufs, rlens)) <= 0 ) {
				perror("fifoXferFrame: reading FIFO failed");
				if ( 0 == i ) {
					errno = EIO;
				}
				goto bail;
			}
			if ( fifo->dbg > 0 ) {
				prb( "Received:", rbufs, i );
			}
			destuffCtx.srcIndex = 0;
			destuffCtx.srcSize  = i;
			if ( cmdReadback ) {
				destuffCtx.dst     = cmdp;
				destuffCtx.dstSize = sizeof(*cmdp);
				destuff( &destuffCtx );
				if ( destuffCtx.dstIndex != sizeof(*cmdp) ) {
					fprintf(stderr, "Internal error - readback of command failed!\n");
				}
				cmdReadback = 0;
				destuffCtx.dstSize  = 0;
				destuffCtx.dstIndex = 0;
			}
			while ( destuffCtx.srcIndex < destuffCtx.srcSize ) {
				if ( destuffCtx.dstIndex == destuffCtx.dstSize ) {
					tot += destuffCtx.dstSize;
					while ( ridx < rcnt && 0 == rbuf[ridx].len ) {
						ridx++;
					}
					destuffCtx.dstIndex = 0;
					if ( ridx < rcnt ) {
						destuffCtx.dstSize  = rbuf[ridx].len;
						destuffCtx.dst      = rbuf[ridx].buf;
						ridx++;
					} else {
						destuffCtx.dstSize = 0;
						/* still proceed to destuff; may still find EOF */
					}
				}
				if ( (progress = destuff( &destuffCtx )) ) {
					if ( progress > 0 ) {
						/* progress > 0 signals EOF detection */
						if ( destuffCtx.srcIndex < destuffCtx.srcSize ) {
							fprintf(stderr, "fifoXferFrame: WARNING -- received comma but there are extra data\n");
						}
					} else {
						/* no progress; no source consumed */
						if ( ! warned ) {
							fprintf(stderr, "Not enough buffers for received message - %zd bytes dropped\n", destuffCtx.srcSize - destuffCtx.srcIndex);
							warned = 1;
						}
					}
					// drop
					break;
				}
			}
		}
	}
	tot += destuffCtx.dstIndex;

	return tot;

bail:
	return -errno;
}
