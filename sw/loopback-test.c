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

