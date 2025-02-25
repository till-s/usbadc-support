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

static int fifoDebug = 0;

typedef enum { RX, ESC, DONE } RxState;

/* Basic communication with the USB-FIFO (FT245), byte-stuffer/de-stuffer and command multiplexer in firmware */

int fifoOpen(const char *devn, unsigned speed)
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
				snprintf(msg, sizeof(msg), "settiong TIOCEXCL failed");
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

int
fifoClose( int fd )
{
	if ( fd >= 0 ) {
		close ( fd );
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
stuff(uint8_t *dbuf, ssize_t dbufsz, const uint8_t *buf)
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

int
fifoSetDebug(int val)
{
int oldVal = fifoDebug;
	if ( val >= 0 ) {
		fifoDebug = val;
	}
	return oldVal;
}

int
fifoXferFrame(int fd, uint8_t *cmdp, const uint8_t *tbuf, size_t tlen, uint8_t *rbuf, size_t rlen)
{
tbufvec tvec[1];
rbufvec rvec[1];
	tvec[0].buf = tbuf;
	tvec[0].len = tlen;

	rvec[0].buf = rbuf;
	rvec[0].len = rlen;

	return fifoXferFrameVec( fd, cmdp, tvec, tlen ? 1 : 0, rvec, rlen ? 1 : 0 );
}

int
fifoXferFrameVec(int fd, uint8_t *cmdp, const tbufvec *tbuf, size_t tcnt, const rbufvec *rbuf, size_t rcnt)
{
uint8_t         tbufs[MAXLEN];
uint8_t         rbufs[MAXLEN];
size_t          i, j, tlens, rlens, puts, put, got, tot, tidx, ridx, tlen, rlen;
fd_set          rfds, tfds;
RxState         state       = RX;
int             warned;
int             eofSent     = 0;
int             cmdReadback = 0;
struct timespec timeout;

	tlens = 0;
	rlens = sizeof(rbufs);
	put   = got  = tot = 0;
	puts  = 0;
	tidx  = ridx = 0;
    tlen  = 0;
    while ( tidx < tcnt && 0 == (tlen = tbuf[tidx].len) ) {
		tidx++;
	}
    while ( ridx < rcnt && 0 == (rlen = rbuf[ridx].len) ) {
		ridx++;
	}
    warned = (0 == rlen ? 1 : 0);

	if ( cmdp ) {
		tlens      += stuff( tbufs + tlens, sizeof(tbufs) - tlens, cmdp );
		cmdReadback = 1;
	}

	while ( ( ! eofSent ) || ( tlens > 0 ) || (DONE != state ) ) {
		FD_ZERO( &rfds );
		FD_ZERO( &tfds );

		if ( ( 0 == tlens ) ) {
			puts = 0;
			if ( ( tlen > put ) ) {
				while ( ( tlen > put ) && ( tlens < sizeof(tbufs) - 3 ) ) {
					/* Stuff tbuf */
					tlens += stuff( tbufs + tlens, sizeof(tbufs) - tlens, tbuf[tidx].buf + put );
					put++;
					while ( put == tlen && ++tidx < tcnt ) {
						put  = 0;
						tlen = tbuf[tidx].len;
					}
				}
			} else if ( ! eofSent ) {
				tbufs[tlens] = COMMA;
				tlens++;
				eofSent      = 1;
			}
		}

		if ( tlens > 0 ) {
			FD_SET( fd, &tfds );
		}
		if ( DONE != state ) {
			FD_SET( fd, &rfds );
		}

		timeout.tv_sec  = 1;
		timeout.tv_nsec = 0;
		i = pselect( fd + 1, &rfds, &tfds, 0, &timeout, 0 );

		if ( i <= 0 ) {
			if ( 0 == i ) {
				/* Timeout */
				return -ETIMEDOUT;
			}
			perror("select failure");
			goto bail;
		}

		if ( FD_ISSET( fd, &tfds ) ) {
			if ( fifoDebug > 0 ) {
				prb( "Sending:", tbufs+puts, tlens );
			}
			if ( (i = write(fd, tbufs + puts, tlens)) <= 0 ) {
				perror("fifoXferFrame: writing FIFO failed");
				if ( 0 == i ) {
					errno = EIO;
				}
				goto bail;
			}
			puts  += i;
			tlens -= i;
		}
		if ( FD_ISSET( fd, &rfds ) ) {
			if ( (i = read(fd, rbufs, rlens)) <= 0 ) {
				perror("fifoXferFrame: reading FIFO failed");
				if ( 0 == i ) {
					errno = EIO;
				}
				goto bail;
			}
			if ( fifoDebug > 0 ) {
				prb( "Received:", rbufs, i );
			}
			for ( j = 0; j < i; j++ ) {
				if ( ESC != state && COMMA == rbufs[j] ) {
					state = DONE;
					if ( j + 1 < i ) {
						fprintf(stderr, "fifoXferFrame: WARNING -- received comma but there are extra data\n");
						break;
					}
				} else if ( ESC != state && ESCAP == rbufs[j] ) {
					state = ESC;
				} else {
					state = RX;
					if ( cmdReadback ) {
						*cmdp = rbufs[j];
						cmdReadback = 0;
					} else {
						if ( got >= rlen ) {
							if ( ! warned ) {
								fprintf(stderr, "fifoXferFrame: RX buffer too small; truncating frame (got %zu >= rlen %zu)\n", got, rlen);
								warned = 1;
							}
						} else {
							rbuf[ridx].buf[got] = rbufs[j];
							got++;
							while ( got == rlen && ++ridx < rcnt ) {
                                tot += got;
								got  = 0;
								rlen = rbuf[ridx].len;
							}
						}
					}
				}
			}
		}
	}

	tot += got;

	return tot;

bail:
	return -errno;
}
