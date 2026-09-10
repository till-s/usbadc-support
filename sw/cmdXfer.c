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
#include <time.h>

#include "cmdXfer.h"

#ifdef CONFIG_WITH_COBS
#include <cobsC.h>
#endif

/* stuff/destuff buffer size */
#define SBUFSZ 16384

#define COMMA  0xCA
#define ESCAP  0x55

typedef enum { RX, ESC, DONE } RxState;

typedef struct Codec Codec;

struct Codec {
	uint8_t             tbufs[SBUFSZ];
	uint8_t             rbufs[SBUFSZ];
	void               *stuffCtx;
	void               *destuffCtx;
	uint8_t           (*comma)(Codec *);
	void              (*stuffInitCtx)(Codec *);
	void              (*stuffRewind)(Codec *);
	void              (*stuffContinue)(Codec *);
	void              (*stuffSetSource)(Codec *, const uint8_t *src, size_t srcSize);
	/* returns 1 on success, 0 if not enough destination space
	 */
	int               (*stuff)(Codec *);
	void              (*stuffAddComma)(Codec *);
	size_t            (*stuffGetSize)(Codec *);

	void              (*destuffInitCtx)(Codec *);
	void              (*destuffRewind)(Codec *);
	size_t            (*destuffGetSourceRemaining)(Codec *);
	size_t            (*destuffGetDestinationMissing)(Codec *);
	void              (*destuffSetSourceSize)(Codec *, size_t);
	void              (*destuffSetDestination)(Codec *, uint8_t *, size_t);
	/* must return 1 on EOF, -1 if no progress made
	 */
	int               (*destuff)(Codec *);
};

typedef struct DestuffBytesCtx {
	RxState        state;
	const uint8_t *src;
	size_t         srcIndex;
	size_t         srcSize;
	uint8_t       *dst;
	size_t         dstIndex;
	size_t         dstSize;
} DestuffBytesCtx;

typedef struct StuffBytesCtx {
	uint8_t       *dst;
	size_t         dstIndex;
	size_t         dstSize;
	const uint8_t *src;
	size_t         srcSize;
	size_t         srcIndex;
} StuffBytesCtx;

#ifdef CONFIG_WITH_COBS
static uint8_t
cobsComma(Codec *cdc) { return COBSC_EOF; }

static void
cobsEncInitCtx(Codec *cdc)
{
CobsCEncoderCtx *ctx = (CobsCEncoderCtx*)cdc->stuffCtx;
	cobsCEncodeInit( ctx );
	ctx->src        = NULL;
	ctx->srcSize    = 0;
	ctx->dst        = cdc->tbufs;
	ctx->dstSize    = sizeof(cdc->tbufs);
}

static void
cobsEncRewind(Codec *cdc)
{
CobsCEncoderCtx *ctx = (CobsCEncoderCtx*)cdc->stuffCtx;
	cobsCEncodeRewind( ctx );
}

static void
cobsEncSetSource(Codec *cdc, const uint8_t *src, size_t srcSize)
{
CobsCEncoderCtx *ctx = (CobsCEncoderCtx*)cdc->stuffCtx;
	ctx->srcIndex = 0;
	ctx->src      = src;
	ctx->srcSize  = srcSize;
}

static void
cobsEncContinue(Codec *cdc)
{
CobsCEncoderCtx *ctx = (CobsCEncoderCtx*)cdc->stuffCtx;
	return cobsCEncodeContinue( ctx );
}

static int
cobsEnc(Codec *cdc)
{
CobsCEncoderCtx *ctx = (CobsCEncoderCtx*)cdc->stuffCtx;
	return cobsCEncode( ctx );
}

static void
cobsEncAddComma(Codec *cdc)
{
CobsCEncoderCtx *ctx = (CobsCEncoderCtx*)cdc->stuffCtx;
	cobsCEncodeAppendEOF( ctx );
}

static size_t
cobsEncGetSize(Codec *cdc)
{
CobsCEncoderCtx *ctx = (CobsCEncoderCtx*)cdc->stuffCtx;
	return ctx->dstIndex;
}

static void
cobsDecInitCtx(Codec *cdc)
{
CobsCDecoderCtx *ctx = (CobsCDecoderCtx*)cdc->destuffCtx;
	cobsCDecodeInit( ctx );
	ctx->dst      = NULL;
	ctx->dstSize  = 0;
	ctx->src      = cdc->rbufs;
	ctx->srcSize  = 0;
}

static void
cobsDecRewind(Codec *cdc)
{
CobsCDecoderCtx *ctx = (CobsCDecoderCtx*)cdc->destuffCtx;
	cobsCDecodeRewind( ctx );
}

static size_t
cobsDecGetSourceRemaining(Codec *cdc)
{
CobsCDecoderCtx *ctx = (CobsCDecoderCtx*)cdc->destuffCtx;
	return ctx->srcSize - ctx->srcIndex;
}

static size_t
cobsDecGetDestinationMissing(Codec *cdc)
{
CobsCDecoderCtx *ctx = (CobsCDecoderCtx*)cdc->destuffCtx;
	return ctx->dstSize - ctx->dstIndex;
}

static void
cobsDecSetSourceSize(Codec *cdc, size_t sz)
{
CobsCDecoderCtx *ctx = (CobsCDecoderCtx*)cdc->destuffCtx;
	ctx->srcIndex = 0;
	ctx->srcSize  = sz;
}

static void
cobsDecSetDestination(Codec *cdc, uint8_t *dst, size_t dstSize)

{
CobsCDecoderCtx *ctx = (CobsCDecoderCtx*)cdc->destuffCtx;
	ctx->dst      = dst;
	ctx->dstIndex = 0;
	ctx->dstSize  = dstSize;
}

static int
cobsDec(Codec *cdc)
{
CobsCDecoderCtx *ctx = (CobsCDecoderCtx*)cdc->destuffCtx;
	return cobsCDecode( ctx );
}
#endif

static void   stuffBytesInitCtx(Codec *cdc);
static void   stuffBytesRewind(Codec *cdc);
static int    stuffBytes(Codec *cdc);
static void   stuffBytesSetSource(Codec *cdc, const uint8_t *src, size_t srcSize);
static void   stuffBytesContinue(Codec *cdc);
static void   stuffBytesAddComma(Codec *cdc);
static size_t stuffBytesGetSize(Codec *cdc);

static void   destuffBytesInitCtx(Codec *cdc);
static void   destuffBytesRewind(Codec *cdc);
static int    destuffBytes(Codec *cdc);
static size_t destuffBytesGetSourceRemaining(Codec *cdc);
static size_t destuffBytesGetDestinationMissing(Codec *cdc);
static void   destuffBytesSetSourceSize(Codec *cdc, size_t sz);
static void   destuffBytesSetDestination(Codec *cdc, uint8_t *dst, size_t dstSize);

static uint8_t stuffBytesComma(Codec *cdc) { return COMMA; }

struct CmdFifoRec {
	int       fd;
	int       dbg;
	int       ownFd;
	size_t    winSize;
	unsigned  ttySpeed;
	Codec     codec;
};

/* Basic communication with the USB-FIFO (FT245), byte-stuffer/de-stuffer and command multiplexer in firmware */

int fifoTtyOpen(const char *devn, unsigned speed)
{
int                fd   = -1;
char               msg[256];
struct termios     atts;
size_t             i;

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

	return fd;

bail:
	if ( fd >= 0 ) {
		close( fd );
	}
	return -errno;
}

int fifoOpenConfig(CmdFifo *pfifo, const CmdFifoConfig *pcfg)
{
CmdFifo        fifo = NULL;
int            status;
int            i, put;
struct termios att;
int            fd = pcfg->ttyFd;
char           msg[4];

	/* special case; zero is treated as unset unless accompanied by
	 * flag.
	 */
	if ( 0 == fd && ! (pcfg->flags & CMD_FIFO_CFG_TTY_STDIN) ) {
		fd = -1;
	}

	if ( ! pcfg || ( ! pcfg->ttyName && fd < 0 ) ) {
		return -EINVAL;
	}

	if ( ! (fifo = calloc(1, sizeof(*fifo))) ) {
		return -ENOMEM;
	}

	fifo->fd = -1;

	if ( pcfg->ttyName ) {
		if ( !! (pcfg->flags & CMD_FIFO_CFG_TTY_SPEED) ) {
			fifo->ttySpeed = pcfg->ttySpeed;
		} else {
			fifo->ttySpeed = B115200;
		}
		status = fifoTtyOpen( pcfg->ttyName, fifo->ttySpeed );
		if ( status < 0 ) {
			goto bail;
		}
		fifo->fd    = status;
		fifo->ownFd = 1;
	} else {
		/* fd >= 0 checked above */
		if ( tcgetattr( fd, &att ) ) {
			status = -errno;
			goto bail;
		}
		fifo->ttySpeed = cfgetispeed( &att );
		fifo->fd       = fd;
		fd             = -1;
	}

	if ( !! (pcfg->flags & CMD_FIFO_CFG_WINSIZE) ) {
		fifo->winSize = pcfg->windowSize;
	} /* else defaults to 0 because of calloc */

	if ( 0 == fifo->winSize ) {
		fifo->winSize = ~ fifo->winSize; /* max */
	}

	if ( CMD_FIFO_CFG_CODEC_BYTESTUFF == pcfg->codec ) {
		fifo->codec.stuffCtx                     = calloc(1, sizeof(StuffBytesCtx));
		fifo->codec.destuffCtx                   = calloc(1, sizeof(DestuffBytesCtx));
		fifo->codec.comma                        = stuffBytesComma;
		fifo->codec.stuffInitCtx                 = stuffBytesInitCtx;
		fifo->codec.stuffRewind                  = stuffBytesRewind;
		fifo->codec.stuffContinue                = stuffBytesContinue;
		fifo->codec.stuffSetSource               = stuffBytesSetSource;
		fifo->codec.stuff                        = stuffBytes;
		fifo->codec.stuffAddComma                = stuffBytesAddComma;
		fifo->codec.stuffGetSize                 = stuffBytesGetSize;
		fifo->codec.destuffInitCtx               = destuffBytesInitCtx;
		fifo->codec.destuffRewind                = destuffBytesRewind;
		fifo->codec.destuff                      = destuffBytes;
		fifo->codec.destuffGetSourceRemaining    = destuffBytesGetSourceRemaining;
		fifo->codec.destuffGetDestinationMissing = destuffBytesGetDestinationMissing;
		fifo->codec.destuffSetSourceSize         = destuffBytesSetSourceSize;
		fifo->codec.destuffSetDestination        = destuffBytesSetDestination;
	} else if ( CMD_FIFO_CFG_CODEC_COBS == pcfg->codec ) {
#ifdef CONFIG_WITH_COBS
		fifo->codec.stuffCtx                     = calloc(1, sizeof(CobsCEncoderCtx));
		fifo->codec.destuffCtx                   = calloc(1, sizeof(CobsCDecoderCtx));
		fifo->codec.comma                        = cobsComma;
		fifo->codec.stuffInitCtx                 = cobsEncInitCtx;
		fifo->codec.stuffRewind                  = cobsEncRewind;
		fifo->codec.stuffContinue                = cobsEncContinue;
		fifo->codec.stuffSetSource               = cobsEncSetSource;
		fifo->codec.stuff                        = cobsEnc;
		fifo->codec.stuffAddComma                = cobsEncAddComma;
		fifo->codec.stuffGetSize                 = cobsEncGetSize;
		fifo->codec.destuffInitCtx               = cobsDecInitCtx;
		fifo->codec.destuffRewind                = cobsDecRewind;
		fifo->codec.destuff                      = cobsDec;
		fifo->codec.destuffGetSourceRemaining    = cobsDecGetSourceRemaining;
		fifo->codec.destuffGetDestinationMissing = cobsDecGetDestinationMissing;
		fifo->codec.destuffSetSourceSize         = cobsDecSetSourceSize;
		fifo->codec.destuffSetDestination        = cobsDecSetDestination;
#else
		status = -ENOTSUP;
		goto bail;
#endif
	} else {
		status = -EINVAL;
		goto bail;
	}

	if ( ! fifo->codec.stuffCtx || ! fifo->codec.destuffCtx ) {
		status = -ENOMEM;
		goto bail;
	}
	fifo->codec.stuffInitCtx( &fifo->codec );
	fifo->codec.destuffInitCtx( &fifo->codec );

	for ( i = 0; i < sizeof(msg)/sizeof(msg[0]); i++ ) {
		msg[i] = fifo->codec.comma(&fifo->codec);
	}
	put = write( fifo->fd, msg, i );
	if ( i != put ) {
		perror("Writing syncing commas failed");
		status = put < 0 ? -errno : -EIO;
		goto bail;
	}

	*pfifo = fifo;
	fifo   = NULL;
	status = 0;

bail:
	fifoClose( fifo );
	return status;
}

int fifoOpen(CmdFifo *pfifo, const char *devn, unsigned speed)
{
	CmdFifoConfig cfg;
	memset( &cfg, 0, sizeof(cfg) );
	cfg.ttyName  = devn;
	cfg.ttySpeed = speed;
	cfg.flags   |= CMD_FIFO_CFG_TTY_SPEED;
	return fifoOpenConfig( pfifo, &cfg );
}

int fifoOpenFd(CmdFifo *pfifo, int fd) {
	CmdFifoConfig cfg;
	memset( &cfg, 0, sizeof(cfg) );
	cfg.ttyFd    = fd;
	cfg.flags   |= CMD_FIFO_CFG_TTY_STDIN; /* in case fd == 0 */
	return fifoOpenConfig( pfifo, &cfg );
}

int fifoGetConfig(CmdFifo fifo, CmdFifoConfig *pcfg)
{
	memset(pcfg, 0, sizeof(*pcfg));
	pcfg->ttyFd      = fifo->fd;
	pcfg->ttySpeed   = fifo->ttySpeed;
	pcfg->windowSize = fifo->winSize;
	return 0;
}

int
fifoClose(CmdFifo fifo)
{
	if ( fifo ) {
		free( fifo->codec.stuffCtx   );
		free( fifo->codec.destuffCtx );
		if ( fifo->ownFd && fifo->fd >= 0 ) {
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
stuffBytes(Codec *cdc)
{
StuffBytesCtx *ctx = (StuffBytesCtx*)cdc->stuffCtx;
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
stuffBytesContinue(Codec *cdc)
{
StuffBytesCtx *ctx = (StuffBytesCtx*)cdc->stuffCtx;
	ctx->dstIndex = 0;
}

static void
stuffBytesAddComma(Codec *cdc)
{
StuffBytesCtx *ctx = (StuffBytesCtx*)cdc->stuffCtx;
	ctx->dst[ctx->dstIndex] = cdc->comma(cdc);
	ctx->dstIndex++;
}

static void
stuffBytesSetSource(Codec *cdc, const uint8_t *src, size_t srcSize)
{
StuffBytesCtx *ctx = (StuffBytesCtx*)cdc->stuffCtx;
	ctx->srcIndex = 0;
	ctx->src      = src;
	ctx->srcSize  = srcSize;
}

static void
stuffBytesInitCtx(Codec *cdc)
{
StuffBytesCtx *ctx = (StuffBytesCtx*)cdc->stuffCtx;
	memset(ctx, 0, sizeof(*ctx));
	ctx->dst        = cdc->tbufs;
	ctx->dstSize    = sizeof(cdc->tbufs);
}

static void
stuffBytesRewind(Codec *cdc)
{
StuffBytesCtx *ctx = (StuffBytesCtx*)cdc->stuffCtx;
	ctx->dstIndex   = 0;
	ctx->srcIndex   = 0;
	ctx->src        = NULL;
	ctx->srcSize    = 0;
}

static size_t
stuffBytesGetSize(Codec *cdc)
{
	StuffBytesCtx *ctx = (StuffBytesCtx*)cdc->stuffCtx;
	return ctx->dstIndex;
}

static void
destuffBytesInitCtx(Codec *cdc)
{
DestuffBytesCtx *ctx = (DestuffBytesCtx*)cdc->destuffCtx;
	memset(ctx, 0, sizeof(*ctx));
	ctx->state        = RX;
	ctx->src          = cdc->rbufs;
	ctx->srcSize      = 0;
	ctx->dst          = NULL;
	ctx->dstSize      = 0;
}

static void
destuffBytesRewind(Codec *cdc)
{
DestuffBytesCtx *ctx = (DestuffBytesCtx*)cdc->destuffCtx;
	ctx->state        = RX;
	ctx->dstIndex     = 0;
	ctx->srcIndex     = 0;
	ctx->srcSize      = 0;
	ctx->dstSize      = 0;
	ctx->dst          = NULL;
}

static size_t
destuffBytesGetSourceRemaining(Codec *cdc)
{
DestuffBytesCtx *ctx = (DestuffBytesCtx*)cdc->destuffCtx;
	return ctx->srcSize - ctx->srcIndex;
}

static size_t
destuffBytesGetDestinationMissing(Codec *cdc)
{
DestuffBytesCtx *ctx = (DestuffBytesCtx*)cdc->destuffCtx;
	return ctx->dstSize - ctx->dstIndex;
}

static void
destuffBytesSetSourceSize(Codec *cdc, size_t sz)
{
DestuffBytesCtx *ctx = (DestuffBytesCtx*)cdc->destuffCtx;
	ctx->srcIndex = 0;
	ctx->srcSize  = sz;
}

static void
destuffBytesSetDestination(Codec *cdc, uint8_t *dst, size_t dstSize)
{
DestuffBytesCtx *ctx = (DestuffBytesCtx*)cdc->destuffCtx;
	ctx->dst      = dst;
	ctx->dstIndex = 0;
	ctx->dstSize  = dstSize;
}


/* Returns
 *  1 -> comma detected
 * -1 -> no progress
 *  0 -> other conditions
 */
static int
destuffBytes(Codec *cdc)
{
DestuffBytesCtx *ctx = (DestuffBytesCtx*)cdc->destuffCtx;
size_t            j;
uint8_t          *dstp;
uint8_t          *dstend;
const uint8_t    *rbufs;
int               rv = 0;

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
size_t              i, rlens, puts, tlens, tot;
fd_set              rfds, tfds;
int                 eofSent     = 0;
struct timespec     timeout;
int                 warned      = 0;
int                 eof;
int                 progress;
size_t              winSize     = fifo->winSize;
Codec              *codec       = &fifo->codec;
ssize_t             tidx, tend, ridx, rend;

	codec->stuffRewind( codec );
	codec->destuffRewind( codec );

	tot                    = 0;
	tlens                  = 0;
	rlens                  = sizeof(fifo->codec.rbufs);
	puts                   = 0;

	eof                    = 0;

	tidx                   = 0;
	ridx                   = 0;
	tend                   = tcnt;
	rend                   = rcnt;
	if ( cmdp ) {
		fifo->codec.stuffSetSource( &fifo->codec, cmdp, sizeof(*cmdp) );
		fifo->codec.destuffSetDestination( &fifo->codec, cmdp, sizeof(*cmdp) );
		/* fictitious first tbuf/rbuf holding the cmdp */
		tidx              = -1;
		ridx              = -1;
	} else {
		if ( tidx < tend ) {
			fifo->codec.stuffSetSource( &fifo->codec, tbuf[tidx].buf, tbuf[tidx].len );
		} else {
			fifo->codec.stuffSetSource( &fifo->codec, NULL, 0 );
		}
		if ( ridx < rend ) {
			fifo->codec.destuffSetDestination( &fifo->codec, rbuf[ridx].buf, rbuf[ridx].len );
		} else {
			fifo->codec.destuffSetDestination( &fifo->codec, NULL, 0 );
		}
	}

	warned                 = (ridx < rend) ? 0 : 1;

	while ( ( ! eofSent ) || ( tlens > 0 ) || ! eof ) {
		FD_ZERO( &rfds );
		FD_ZERO( &tfds );

		if ( ( 0 == tlens ) && (tidx < tend) ) {
			puts = 0;
			/* 'continue' is called after 'stuff' returns 0 and the encoding
			 * buffer was flushed in order to continue stuffing from the
			 * same source buffer (or initially which does no harm).
			 * After flushing the frame 'rewind' should be called but because
			 * this routine returns this is not necessary.
			 *
			 * We can never get here with a fully consumed source (calling
			 * 'continue' in this case would be illegal) because of the
			 * (tidx < tend) test above. 0 == tlens indicates that the
			 * buffer was flushed but tidx < tend says the source is empty.
			 */
			codec->stuffContinue(codec);
			/* stuff() returns nonzero if the source has been consumed */
			while ( codec->stuff(codec) ) {
				/* this tbuf exhausted */
				if ( ++tidx >= tend ) {
					/* all tbufs stuffed; the stuffer ensures there is
					 * space for the comma
					 */
					codec->stuffAddComma( codec );
					eofSent                         = 1;
					break;
				}
				codec->stuffSetSource( codec, tbuf[tidx].buf, tbuf[tidx].len );
				/* if tbuf[tidx].len == 0 'stuff' will return '1' and lead to another loop iteration */
			}
			tlens = codec->stuffGetSize( codec );
		}

		assert( tlens > 0 || tidx >= tend );

		if ( tlens > 0 && winSize > 0 ) {
			FD_SET( fifo->fd, &tfds );
		}
		if ( ! eof ) {
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

		if ( FD_ISSET( fifo->fd, &rfds ) ) {
			if ( (i = read(fifo->fd, fifo->codec.rbufs, rlens)) <= 0 ) {
				perror("fifoXferFrame: reading FIFO failed");
				if ( 0 == i ) {
					errno = EIO;
				}
				goto bail;
			}
			winSize += i;
			if ( fifo->dbg > 0 ) {
				prb( "Received:", fifo->codec.rbufs, i );
			}
			codec->destuffSetSourceSize( codec, i );
			while ( codec->destuffGetSourceRemaining( codec ) > 0 ) {
				if ( codec->destuffGetDestinationMissing( codec ) == 0 ) {
					if ( ridx >= 0 && ridx < rend ) {
						/* command byte (ridx == -1) does not count towards tot */
						tot += rbuf[ridx].len;
					}
					if ( ++ridx < rend ) {
						/* empty destination (len==0) leads to another 'while' iteration */
						codec->destuffSetDestination( codec, rbuf[ridx].buf, rbuf[ridx].len );
					} else {
						/* still proceed to destuff; may still find EOF */
						codec->destuffSetDestination( codec, NULL, 0 );
					}
				}
				if ( (progress = codec->destuff( codec )) ) {
					if ( progress > 0 ) {
						eof = 1;
						/* progress > 0 signals EOF detection */
						if ( codec->destuffGetSourceRemaining( codec ) > 0 ) {
							fprintf(stderr, "fifoXferFrame: WARNING -- received comma but there are extra data\n");
						}
					} else {
						/* no progress; no source consumed */
						if ( ! warned ) {
							fprintf(stderr, "Not enough buffers for received message - %zd bytes dropped\n", codec->destuffGetSourceRemaining( codec ) );
							warned = 1;
						}
					}
					// drop
					break;
				}
			}
		}

		if ( FD_ISSET( fifo->fd, &tfds ) ) {
			if ( fifo->dbg > 0 ) {
				prb( "Sending:", fifo->codec.tbufs + puts, tlens > winSize ? winSize : tlens );
			}
			if ( (i = write(fifo->fd, fifo->codec.tbufs + puts, tlens > winSize ? winSize : tlens)) <= 0 ) {
				perror("fifoXferFrame: writing FIFO failed");
				if ( 0 == i ) {
					errno = EIO;
				}
				goto bail;
			}
			puts    += i;
			tlens   -= i;
			winSize -= i;
		}
	}
	if ( ridx >= 0 && ridx < rend ) {
		/* last rbuf may not be completely filled! */
		tot += rbuf[ridx].len - codec->destuffGetDestinationMissing( codec );
	}

	return tot;

bail:
	return -errno;
}

static int pollfor(int fd, struct timespec *pto)
{
fd_set rfds;
int    status;

	FD_ZERO( &rfds );
	FD_SET( fd, &rfds );
	status = pselect( fd + 1, &rfds, NULL, NULL, pto, NULL );
	if ( status <= 0 ) {
		if ( 0 == status ) {
			return -ETIMEDOUT;
		}
		return -errno;
	}
	return  FD_ISSET( fd, &rfds ) ? 0 : -ENODEV;
}

int fifoSync(CmdFifo fifo, int milliseconds)
{
struct timespec  now, deadline;
struct timespec  timeout, *pto;
int              status;
uint8_t          byte;
int              expired;

	if ( milliseconds >= 0 ) {
		timeout.tv_sec  = (milliseconds / 1000);
		timeout.tv_nsec = (milliseconds % 1000) * 1000000L;
		if ( clock_gettime( CLOCK_MONOTONIC, &now ) ) {
			return -errno;
		}
		deadline.tv_nsec = now.tv_nsec + timeout.tv_nsec;
		deadline.tv_sec  = now.tv_sec  + timeout.tv_sec;
		if ( deadline.tv_nsec >= 1000000000L ) {
			deadline.tv_nsec -= 1000000000L;
			deadline.tv_sec  += 1;
		}
		pto = &timeout;
	} else {
		pto = NULL;
	}
	expired = 0;
	do {
		status = pollfor( fifo->fd, pto );
		if ( status < 0 ) {
			return status;
		}
		status = read( fifo->fd, &byte, 1 );
		if ( 1 != status ) {
			if ( 0 == status ) {
				return -EIO;
			}
			return -errno;
		}
		if ( byte == fifo->codec.comma( &fifo->codec ) ) {
			return 0;
		}
		if ( pto ) {
			if ( clock_gettime( CLOCK_MONOTONIC, &now ) ) {
				return -errno;
			}
			if ( deadline.tv_sec >= now.tv_sec ) {
				timeout.tv_sec = (deadline.tv_sec - now.tv_sec);
				timeout.tv_nsec =(deadline.tv_nsec - now.tv_nsec);
				if ( now.tv_nsec > deadline.tv_nsec ) {
					if ( timeout.tv_sec > 0 ) {
						timeout.tv_sec--;
						timeout.tv_nsec = 1000000000L;
					} else {
						expired = 1;
					}
				}
			} else {
				expired = 1;
			}
		}
	} while ( ! expired );
	return -ETIMEDOUT;
}
