#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include "fwUtil.h"

int
fileMap(const char *fnam,  uint8_t **mapp, off_t *sizp, off_t creatsz)
{
int             progfd    = -1;
int             rval      = -1;
struct stat     sb;

	if ( fnam && ( MAP_FAILED == (void*)*mapp ) ) {

		if ( (progfd = open( fnam, (O_RDWR | O_CREAT), 0664 )) < 0 ) {
			perror("unable to open program file");
			goto bail;	
		}
		if ( creatsz && ftruncate( progfd, creatsz ) ) {
			perror("fileMap(): ftruncate failed");
			goto bail;
		}
		if ( fstat( progfd, &sb ) ) {
			perror("unable to fstat program file");
			goto bail;
		}
		*mapp = (uint8_t*) mmap( 0, sb.st_size, ( PROT_WRITE | PROT_READ ), MAP_SHARED, progfd, 0 );
		if ( MAP_FAILED == (void*) *mapp ) {
			perror("unable to map program file");
			goto bail;
		}
		*sizp = sb.st_size;
	}

	rval = 0;

bail:
	if ( progfd >= 0 ) {
		close( progfd );
	}
	return rval;
}
