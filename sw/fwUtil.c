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

#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include "fwUtil.h"

int
fileMap(const char *fnam,  uint8_t **mapp, off_t *sizp, off_t creatsz, int ro)
{
int             progfd    = -1;
int             rval      = -1;
struct stat     sb;
int             flgs      = ( ro ? O_RDONLY  : (O_RDWR | (creatsz ? O_CREAT : 0)) );
int             prot      = ( ro ? PROT_READ : (PROT_WRITE | PROT_READ) );

	*mapp = 0;
	*sizp = 0;
	if ( fnam ) {

		if ( (progfd = open( fnam, flgs, 0664 )) < 0 ) {
			rval = -errno;
			perror("unable to open program file");
			goto bail;	
		}
		if ( creatsz && ftruncate( progfd, creatsz ) ) {
			rval = -errno;
			perror("fileMap(): ftruncate failed");
			goto bail;
		}
		if ( fstat( progfd, &sb ) ) {
			rval = -errno;
			perror("unable to fstat program file");
			goto bail;
		}
		*mapp = (uint8_t*) mmap( 0, sb.st_size, prot, MAP_SHARED, progfd, 0 );
		if ( MAP_FAILED == (void*) *mapp ) {
			*mapp = NULL;
			rval = -errno;
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
