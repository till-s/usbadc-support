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

/* Test program to exercise the transmission-window feature of 'cmdXfer' */

#define _GNU_SOURCE

#include <cmdXfer.h>

#include <pty.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <limits.h>
#include <time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <errno.h>
#include <inttypes.h>
#include <assert.h>

static double tdiff(struct timespec *a, struct timespec *b) {
	double diff = (a->tv_nsec - b->tv_nsec)*1.0E-9;
	diff += (a->tv_sec - b->tv_sec);
	return diff;
}

static void d2ts(struct timespec *ts, double t) {
	ts->tv_sec = t;
	ts->tv_nsec = (t - ts->tv_sec) * 1.0E9;
}

struct LoopbackParams {
	unsigned        chunkSz;   /* size of chunks we write back; one at a time */
	double          rateLimit; /* cap of transmission rate (bytes/s)          */
	unsigned        winSz;
	unsigned        ldBufSz;
	unsigned        numLoops;
	int             debug;
	int             cobs;
};

/* receive bytes and echo them back but at rate that can be throttled
 * to packets of 'chunkSz' at a max. rate of 'rateLimit' bytes/s
 */
static int
throttledLoopback(struct LoopbackParams *p)
{
	char            snam[PATH_MAX];
	int             mfd   = -1;
	int             sfd   = -1;
	char           *buf   = NULL;
	int             rv    = -1;
	int             got, put;
	size_t          wrp   = 0;
	size_t          rdp   = 0;
	size_t          ep;
	size_t          eidx, widx, ridx;
	size_t          tot     = 0;
	size_t          lastTot = 0;
	int             nrd, nwr;
	size_t          bufSz = (1<<p->ldBufSz);
	struct iovec    rdv[2];
	struct iovec    wrv[2];

	struct pollfd   pfds[1];
	struct timespec lastSend, now, lastPrint;
	struct timespec wai;
	struct timespec nowai;
	struct timespec *timeout;
	double          rate;
	int             status;
	if ( openpty(&mfd, &sfd, snam, NULL, NULL) ) {
		perror("openpty failed");
		return 1;
	}
	printf("Slave: %s (note that the number of transferred bytes include stuffing!)\n", snam);

	if ( ! (buf = malloc(bufSz)) ) {
		fprintf(stderr, "No memory\n");
		goto bail;
	}

	/* enable for raw testing of peer */
	while ( 0 ) {
		got = read(mfd, buf, bufSz);
		if ( got < 0 ) {
			perror("reading");
		}
		printf("copy %d\n", got);
		write(mfd, buf, got);
	}


	pfds[0].fd         = mfd;
	pfds[0].events     = POLLIN;

	put = 0;
	lastSend.tv_sec  = 0;
	lastSend.tv_nsec = 0;
	nowai.tv_sec     = 0;
	nowai.tv_nsec    = 0;
	d2ts(&wai, p->chunkSz/p->rateLimit);
	lastPrint        = lastSend;
	while ( 1 ) {
		if ( clock_gettime(CLOCK_MONOTONIC, &now) ) {
			perror("clock_gettime failed");
			goto bail;
		}
		rate = (double)put/tdiff(&now, &lastSend);
		if ( rate <= p->rateLimit ) {
			if ( wrp > rdp ) {
				if ( p->debug ) {
					printf("write immediately\n");
				}
				pfds[0].events |= POLLOUT;
				timeout         = &nowai;
			} else {
				/* nothing to send; dont' bother waking up */
				if ( p->debug ) {
					printf("no data; wait indefinitely\n");
				}
				pfds[0].events &= ~POLLOUT;
				timeout         = NULL;
				if ( tot != lastTot ) {
					/* still schedule a wakeup; just re-use 'wai' for sake of simplicity */
					timeout = &wai;
				}
			}
		} else {
			if ( p->debug ) {
				printf("no data; wake after timeout\n");
			}
			pfds[0].events &= ~POLLOUT;
			timeout         = &wai;
		}
		status = ppoll(pfds, sizeof(pfds)/sizeof(pfds[0]), timeout, NULL);
		if ( status <= 0 ) {
			if ( 0 == status ) {
				continue;
			}
			perror("poll error");
			goto bail;
		}
		if ( p->debug ) {
			printf("polled\n");
		}
		if ( !! (pfds[0].revents & (POLLERR | POLLHUP)) ) {
			fprintf(stderr, "Polled FD has errors\n");
			goto bail;
		}
		ridx = rdp & (bufSz - 1);
		widx = wrp & (bufSz - 1);
		if ( !! (pfds[0].revents & POLLIN) ) {
			if ( wrp - rdp >= bufSz ) {
				fprintf(stderr, "Error: Buffer overflow\n");
				goto bail;
			} else {
				if ( wrp - rdp > p->winSz ) {
					fprintf(stderr, "Error: Window overflow\n");
					goto bail;
				}
				nrd = 1;
				rdv[0].iov_base = buf + widx;
				if ( ridx > widx ) {
					rdv[0].iov_len = ridx - widx;
				} else {
					rdv[0].iov_len = bufSz - widx;
					if ( ridx > 0 ) {
						nrd             = 2;
						rdv[1].iov_base = buf;
						rdv[1].iov_len  = ridx;
					}
				}
				got = readv(mfd, rdv, nrd);
				if ( got <= 0 ) {
					perror("read error");
					goto bail;
				}
				if ( p->debug ) {
					printf("read %d\n",  got);
				}
				wrp += got;
				widx = wrp & (bufSz - 1);
			}
		}
		if ( !! (pfds[0].revents & POLLOUT) && (wrp > rdp) ) {
			/* limit amount of data to be written to chunksize */
			ep   = (wrp - rdp > p->chunkSz ? rdp + p->chunkSz : wrp);
			eidx = ep & (bufSz - 1);
			nwr  = 1;
			wrv[0].iov_base = buf + ridx;
			if ( ridx < eidx ) {
				wrv[0].iov_len  = eidx - ridx;
			} else {
				/* if ( eidx == ridx ) the buffer must be full due to the wrp > rdp check above (assuming p->chunkSz > 0) */
				wrv[0].iov_len  = bufSz - ridx;
				wrv[1].iov_base = buf;
				wrv[1].iov_len  = eidx;
				nwr = 2;
			}
			put = writev(mfd, wrv, nwr);
			if ( p->debug ) {
				printf("wrote %d\n",  put);
			}
			if ( put <= 0 ) {
				perror("write error");
				goto bail;
			}
			if ( clock_gettime( CLOCK_MONOTONIC, &lastSend ) ) {
				perror("clock_gettime error");
				goto bail;
			}
			tot += put;
			rdp += put;
			if ( tdiff(&lastSend, &lastPrint) > 0.99 ) {
				printf("%8zu\r", tot);
				fflush(stdout);
				lastPrint = lastSend;
				lastTot   = tot;
			}
		}
	}

	rv = 0;
bail:
	if ( mfd >= 0 ) {
		close(mfd);
	}
	if ( sfd >= 0 ) {
		close(sfd);
	}
	free( buf );
	return rv;
}

extern int fifoTtyOpen(const char *, unsigned);

static int
blaster(const char *ttyName, struct LoopbackParams *p)
{
	uint8_t       *tx  = NULL;
	uint8_t       *rx  = NULL;
	CmdFifo       fifo = NULL;
	int           fd   = -1;
	int           rv   = -1;
	int           i,k,j;
	CmdFifoConfig fifoCfg;
	size_t        blastSz = (1<<p->ldBufSz);
	int           errs = 0;

	if ( !(tx = malloc(blastSz)) ) {
		fprintf(stderr, "No memory\n");
		goto bail;
	}

	if ( !(rx = malloc(blastSz)) ) {
		fprintf(stderr, "No memory\n");
		goto bail;
	}

	memset(&fifoCfg, 0, sizeof(fifoCfg));
	fifoCfg.ttyName    = ttyName;
	fifoCfg.windowSize = p->winSz;
	fifoCfg.flags     |= CMD_FIFO_CFG_WINSIZE;
	if ( p->cobs ) {
		fifoCfg.codec = CMD_FIFO_CFG_CODEC_COBS;
	}


	if ( (fd = fifoTtyOpen( ttyName, 115200 )) < 0 ) {
		fprintf(stderr, "Error: opening fifo failed: %s\n", strerror(-fd));
		goto bail;
	}

	if ( ( i = fifoOpenConfig( &fifo, &fifoCfg) ) || fifoGetConfig(fifo, &fifoCfg ) ) {
		fprintf(stderr, "Error: opening fifo failed: %s\n", strerror(-i));
		goto bail;
	}

	fd = fifoCfg.ttyFd;

	/* Discard initial SYNC bytes */
	i = read(fd, rx, blastSz);
	if ( p->debug ) {
		printf("Initial sync dump: %d\n", i);
		fifoSetDebug(fifo, 1);
	}
	for ( k = 0; k < p->numLoops; ++k ) {
		for ( i = 0; i < sizeof(tx); ++i ) {
			tx[i] = lrand48();
		}
		i = fifoXferFrame(fifo, NULL, tx, blastSz, rx, blastSz);
		if ( i < 0 ) {
			fprintf(stderr, "Error: transferring data: %s\n", strerror(-i));
		}
		if ( p->debug ) {
			printf("xferFrame: %d\n", i);
			errs += (i != blastSz);
		} else {
			assert( i == blastSz );
		}

		if ( p->debug ) {
			printf("TX -> RX Mismatch\n");
			for ( j = 0; j < i; ++ j ) {
				errs += (tx[j] != rx[j]);
				printf("%02x -> %02x %s\n", tx[j], rx[j], tx[j] == rx[j] ? "" : "E");
			}
		} else {
			while ( --i >= 0 ) {
				assert(rx[i] == tx[i]);
			}
		}
		printf("%u frames %stested\n", k+1, errs ? "" : "successfully ");
	}

	rv = errs;;

bail:
	free(tx);
	free(rx);
	if ( fifo ) {
		fifoClose(fifo);
	}
	if ( fd >= 0 ) {
		close(fd);
	}
	return rv;
}

int
main(int argc, char **argv)
{
	int         opt;
	int         rv     = 1;
	const char *ttyNam = NULL;
	unsigned   *u_p;
	double     *d_p;
	struct LoopbackParams loopbackParams;

	loopbackParams.winSz     = 1024;
	loopbackParams.ldBufSz   = 0;
	loopbackParams.chunkSz   = 0;
	loopbackParams.rateLimit = 1000.0;
	loopbackParams.debug     = 0;
	loopbackParams.numLoops  = 1;
	loopbackParams.cobs      = 0;

	while ( (opt = getopt(argc, argv, "d:DCw:B:c:r:n:")) > 0 ) {
		u_p = NULL;
		d_p = NULL;
		switch ( opt ) {
			default : fprintf(stderr, "Unsupported option -%c\n", opt); return 1;
			case 'd': ttyNam=optarg;                   break;
			case 'D': loopbackParams.debug++;          break;
			case 'w': u_p = &loopbackParams.winSz;     break;
			case 'B': u_p = &loopbackParams.ldBufSz;   break;
			case 'c': u_p = &loopbackParams.chunkSz;   break;
			case 'r': d_p = &loopbackParams.rateLimit; break;
			case 'n': u_p = &loopbackParams.numLoops;  break;
			case 'C': loopbackParams.cobs = 1;         break;
		}
		if ( u_p && 1 != sscanf(optarg, "%u", u_p) ) {
			fprintf(stderr, "unable to scan arg to option -%c\n", opt);
			return 1;
		}
		if ( d_p && 1 != sscanf(optarg, "%lg", d_p) ) {
			fprintf(stderr, "unable to scan arg to option -%c\n", opt);
			return 1;
		}
	}
	if ( 0 == loopbackParams.ldBufSz ) {
		loopbackParams.ldBufSz = (ttyNam ? 20 : 16);
	}
	if ( 0 == loopbackParams.chunkSz ) {
		loopbackParams.chunkSz = 23;
	}
	if ( loopbackParams.winSz < loopbackParams.chunkSz ) {
		fprintf(stderr, "window size too small; increasing to chunk size %u\n", loopbackParams.chunkSz);
		loopbackParams.winSz = loopbackParams.chunkSz;
	}
	if ( loopbackParams.ldBufSz > 24 ) {
		fprintf(stderr, "buffer size too big; note that the we need a log2!\n");
		goto bail;
	}
	if ( (1<<loopbackParams.ldBufSz) < loopbackParams.winSz && ! ttyNam ) {
		fprintf(stderr, "buffer size smaller than window size (%u); please increase\n", loopbackParams.winSz);
		goto bail;
	}
	if ( ! ttyNam ) {
		printf("Running throttled loopback with parameters:\n");
		printf("  tx chunk size    : %u\n",      loopbackParams.chunkSz);
		printf("  window size      : %u\n",      loopbackParams.winSz);
		printf("  log2 buffer size : %u\n",      loopbackParams.ldBufSz);
		printf("  rate limit       : %g(B/s)\n", loopbackParams.rateLimit);
		throttledLoopback( &loopbackParams );
	} else {
		printf("Running blaster with frame length %u, window size %u\n", (1<<loopbackParams.ldBufSz), loopbackParams.winSz);
		
		if ( 0 == blaster( ttyNam, &loopbackParams ) ) {
			printf("Test Passed\n");
			rv = 0;
		} else {
			printf("Test Failed\n");
		}
	}
bail:
	return rv;
}
