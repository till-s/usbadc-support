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
#include <sys/stat.h>
#include <getopt.h>
#include <unistd.h>


int main(int argc, char **argv)
{
char               buf[256]; // must be smaller than fifo buffer depth
const char        *devn    = "/dev/ttyUSB0";
int                fd      = -1;
int                rval    = 1;
unsigned           speed   = 115200;
struct termios     atts;
int                i,j;
int                opt;
int                nvals;
char              *p;

	while ( (opt = getopt(argc, argv, "d:")) > 0 ) {
		switch ( opt ) {
			case 'd': devn = optarg; break;
			default:
				fprintf(stderr, "unknown option -%c\n", opt);
				return 1;
		}
	}

	if ( (fd = open(devn, O_RDWR)) < 0 ) {
		snprintf(buf, sizeof(buf), "unable open device '%s'", devn);
		perror(buf);
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

	nvals = argc - optind;
	if ( nvals > (unsigned)sizeof(buf) ) {
		fprintf(stderr, "Too many values -- truncating to %d\n", (unsigned)sizeof(buf));
	}

	for ( i = 0; i < nvals; i++ ) {
		int val;

		if ( 1 != sscanf(argv[i + optind], "%i", &val) ) {
			fprintf(stderr, "Unable to scan value #%i\n", i);
			goto bail;
		}
		if ( val < 0 || val > 255 ) {
			fprintf(stderr, "value 0x%x out of range 0..255; skipping\n", val);
			continue;
		}

		buf[i] = val & 0xff;
	}


	for ( p = buf, i = nvals; i > 0; i -= j, p += j ) {
		if ( (j = write(fd, p, i)) <= 0 ) {
			perror( "writing fifo" );
			goto bail;
		}
	}

	for ( p = buf, i = nvals; i > 0; i -= j, p += j ) {
		if ( (j = read(fd, p, i)) <= 0 ) {
			perror( "reading fifo" );
			goto bail;
		}
	}

	for ( i = 0; i < nvals; i++ ) {
		printf("0x%02x ", buf[i]);
	}
	printf("\n");

	rval = 0;

bail:
	if ( fd >= 0 ) {
		close( fd );
	}
	return rval;
}

