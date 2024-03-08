#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdint>
#include <errno.h>
#include <termios.h>

int
simPtyCreate(void)
{
int                fd, flgs;
char               nam[200];

	if ( ( fd = posix_openpt( O_RDWR | O_NOCTTY ) ) < 0  ) {
		perror("simPtyCreate(): unable to open master PTY");
		return -1;
	}

	if ( grantpt( fd ) ) {
		perror("simPtyCreate(): unable to grant PTY");
		goto bail;
	}

	if ( -1 == (flgs = fcntl( fd, F_GETFL )) ) {
		perror("simPtyCreate(): unable to obtain fd flags");
		goto bail;
	}

	flgs |= O_NONBLOCK;

	if ( fcntl( fd, F_SETFL, flgs ) ) {
		perror("simPtyCreate(): unable to set fd flags (NONBLOCK)");
		goto bail;
	}


	if ( unlockpt( fd ) ) {
		perror("simPtyCreate(): unable to unlock PTY");
		goto bail;
	}

	if ( ptsname_r( fd, nam, sizeof(nam) ) ) {
		perror("simPtyCreate(): unable to retrieve name of PTY");
		goto bail;
	}
	nam[sizeof(nam)-1] = 0;
	printf("Opened PTY as: %s\n", nam);
	printf("My PID: %d, F_GETOWN %d\n", getpid(), fcntl( fd, F_GETOWN ));
	fcntl(fd, F_SETOWN, getpid());
	printf("My PID: %d, F_GETOWN %d\n", getpid(), fcntl( fd, F_GETOWN ));

	return fd;

bail:
	if ( fd >= 0 ) {
		close ( fd );
	}
	return -1;
}

#ifndef TEST

static int theFD = simPtyCreate();

extern "C" {

void readPtyPoll_C(int *valid, int *data)
{
uint8_t oct;
int     st;
	st = read( theFD, &oct, 1 );
	if ( 1 == st ) {
		*valid = 1;
		*data  = oct;
printf("READ %x\n", oct);
	} else if ( st < 0 && EAGAIN != errno  ) {
		*valid = -1;
		close( theFD );
		theFD = simPtyCreate();
//		*valid = 0;
// if communication is interrupted we get EIO; this is a flag
// permanently set (checked in linux' pty driver) until close/reopen :-(
//		perror("readPtyPoll");
	} else {
		*valid =  0;
	}
}

void writePty_C(int *valid, int data)
{
uint8_t oct = (uint8_t) data;
int     put;
	if ( 1 == (put = write( theFD, &oct, 1 )) ) {
		*valid = 1;
	} else {
		*valid = -1;
		perror("WARNING: writePty_C failed to write");
	}
}

void writePtyPoll_C(int *rdy)
{
fd_set         fds;
int            st;
struct timeval t;

	FD_ZERO( &fds );
	FD_SET ( theFD, &fds );

	t.tv_sec  = 0;
	t.tv_usec = 0;

	st = select( theFD + 1, 0, &fds, 0, &t );

	if ( st <= 0 ) {
		*rdy = 0;
		if ( st < 0 ) {
			perror("WARNING: writePtyPoll_C failed to select");
			*rdy = -1;
		}
	} else {
		*rdy = 1;
	}
}


}

#else
int main()
{
int     fd = simPtyCreate();
fd_set  rfds;
uint8_t c;

	if ( fd < 0 ) {
		return 1;
	}

	while ( 1 ) {
		FD_ZERO( &rfds );
		FD_SET ( fd, &rfds );
		if ( select( fd + 1, &rfds, 0, 0, 0 ) < 0 ) {
			perror("select failed");
			return 1;
		}
		if ( 1 != read( fd, &c, 1 ) ) {
			printf("Unable to read\n");
            return 1;
		} else {
			printf("Got 0x%02x\n", c);
		}
	}
	
}

#endif
