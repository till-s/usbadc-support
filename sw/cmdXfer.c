#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <sys/select.h>
#include <sys/mman.h>
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

static int fifoDebug = 1;

typedef enum { RX, ESC, DONE } RxState;

int fifoOpen(const char *devn, unsigned speed)
{
int                rval = -1;
int                fd   = -1;
char               msg[256];
struct termios     atts;
int                flags;
size_t             i;

	if ( (fd = open(devn, O_RDWR)) < 0 ) {
		snprintf(msg, sizeof(msg), "unable open device '%s'", devn);
		perror(msg);
		goto bail;
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

	if ( tcsetattr( fd, TCSANOW, &atts ) ) {
		perror( "tcgetattr failed" );
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
	if ( i != write( fd, msg, i ) ) {
		perror("Writing syncing commas failed");
		goto bail;
	}

	rval = fd;
	fd   = -1;

bail:
	if ( fd >= 0 ) {
		close( fd );
	}
	return rval;
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
	if ( 0 != k & 0xf ) {
		printf("\n");
	}
}

static size_t
stuff(uint8_t *dbuf, const uint8_t *buf)
{
size_t rval = 0;

	/* Stuff dbuf */
	if ( ( COMMA == *buf ) || ( ESCAP == *buf ) ) {
		dbuf[rval] = ESCAP;	
		rval++;
	}
	dbuf[rval] = *buf;
	rval++;
	return rval;
}

int
xferFrame(int fd, uint8_t *cmdp, const uint8_t *tbuf, size_t tlen, uint8_t *rbuf, size_t rlen)
{
uint8_t tbufs[MAXLEN];
uint8_t rbufs[MAXLEN];
size_t  i, j, tlens, rlens, puts, put, got, chunksz, k;
fd_set  rfds, tfds;
RxState state       = RX;
int     warned      = (0 == rlen ? 1 : 0);
int     eofSent     = 0;
int     cmdReadback = 0;

	tlens = 0;
	rlens = sizeof(rbufs); 
	put   = got = 0;
	puts  = 0;

	if ( cmdp ) {
		tlens      += stuff( tbufs + tlens, cmdp );
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
					tlens += stuff( tbufs + tlens, tbuf + put );
					put++;
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

		i = pselect( fd + 1, &rfds, &tfds, 0, 0, 0 );

		if ( i <= 0 ) {
			if ( 0 == i ) {
				fprintf(stderr, "Hmm - select returned 0\n");
				sleep(1);
				continue;
			}
			perror("select failure");
			goto bail;
		}

		if ( FD_ISSET( fd, &tfds ) ) {
			if ( fifoDebug > 0 ) {
				prb( "Sending:", tbufs+puts, tlens );
			}
			if ( (i = write(fd, tbufs + puts, tlens)) <= 0 ) {
				perror("xferFrame: writing FIFO failed");
				goto bail;
			}
			puts  += i;
			tlens -= i;
		}
		if ( FD_ISSET( fd, &rfds ) ) {
			if ( (i = read(fd, rbufs, rlens)) <= 0 ) {
				perror("xferFrame: reading FIFO failed");
				goto bail;
			}
			if ( fifoDebug > 0 ) {
				prb( "Received:", rbufs, i );
			}
			for ( j = 0; j < i; j++ ) {
				if ( ESC != state && COMMA == rbufs[j] ) {
					state = DONE;
					if ( j + 1 < i ) {
						fprintf(stderr, "xferFrame: WARNING -- receved comma but there are extra data\n");
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
								fprintf(stderr, "xferFrame: RX buffer too small; truncating frame\n");
								warned = 1;
							}
						} else {
							rbuf[got] = rbufs[j];
							got++;
						}
					}
				}
			}
		}
	}

	return got;
	
bail:
	return -1;
}

#ifdef TEST

int
main(int argc, char **argv)
{
uint8_t rbuf[20];

int fd = fifoOpen("/dev/ttyUSB0", 230400/2);
int got,i;

	if ( fd < 0 ) {
		return 1;
	}

	got = xferFrame(fd, 0x10, 0, 0, rbuf, sizeof(rbuf));
	for ( i = 0; i < got; i++ ) {
		printf("0x%02x\n", rbuf[i]);
	}
	
}

#endif
