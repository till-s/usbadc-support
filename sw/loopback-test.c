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
char               buf[256];
const char        *devn    = "/dev/ttyUSB0";
int                fd      = -1;
int                rval    = 1;
unsigned           speed   = 115200;
struct termios     atts;

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

	while ( 1 ) {

		if ( 1 != read(0, buf, 1) ) {
			perror( "reading stdin" );
			goto bail;
		}

		if ( 1 != write(fd, buf, 1) ) {
			perror( "reading fifo" );
			goto bail;
		}

		if ( 1 != read(fd, buf, 1) ) {
			perror( "reading fifo" );
			goto bail;
		}

		if ( 1 != write(1, buf, 1) ) {
			perror( "reading stdout" );
			goto bail;
		}

	}

	rval = 0;

bail:
	if ( fd >= 0 ) {
		close( fd );
	}
	return rval;
}

