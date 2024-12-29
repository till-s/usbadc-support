#include "unitDataFlash.h"
#include "unitData.h"
#include "at25Sup.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>


static size_t
unitDataAddr( AT25Flash *flash )
{
	return at25_get_size_bytes( flash ) - at25_get_block_size( flash );
}

typedef int (*FlashCB)(AT25Flash *flash, const struct UnitData **udp, size_t flashAddr, uint8_t *buf, size_t bufsz);

static int flashOp(const struct UnitData **udp, FWInfo *fw, FlashCB cb)
{
AT25Flash      *flash = NULL;
int             st    = -ENODEV;
uint8_t        *buf   = NULL;
size_t          blksz;
size_t          addr;
	if ( ! (flash = at25_open( fw, 0 )) ) {
		return -ENODEV;
	}
	addr  = unitDataAddr( flash );
	blksz = at25_get_block_size( flash );
	if ( 0 == blksz ) {
		st = -ENOTSUP;
		goto bail;
	}
	if ( ! (buf = malloc( blksz )) ) {
		st = -ENOMEM;
		goto bail;
	}

	st = cb( flash, udp, addr, buf, blksz );

bail:
	if ( flash ) {
		at25_close( flash );
	}
	free( buf );
	return st;
}

static int
fromFlashCB(AT25Flash *flash, const struct UnitData **udp, size_t flashAddr, uint8_t *buf, size_t bufsz)
{
int st;
	if ( (st = at25_spi_read( flash, flashAddr, buf, bufsz )) < 0 ) {
		return st;
	}
	if ( (st = unitDataParse( udp, buf, bufsz )) < 0 ) {
		return st;
	}
	return 0;
}

static int
toFlashCB(AT25Flash *flash, const struct UnitData **udp, size_t flashAddr, uint8_t *buf, size_t bufsz)
{
int      st;
size_t   serializedSize;
unsigned cmd;

	if ( (st = unitDataSerialize( *udp, buf, bufsz )) < 0 ) {
		return st;
	}
	serializedSize = st;
	if ( (st = at25_write_ena( flash )) < 0 ) {
		return st;
	}

	if ( (st = at25_status( flash )) < 0 ) {
		fprintf(stderr, "Unable to erase flash; at25_status() failed\n");
		goto bail;
	}

	if ( 0 != ( st & AT25_ST_WEL ) ) {
		fprintf(stderr, "Unable to erase flash; write-protection still engaged?!\n");
		st = -EIO;
		goto bail;

	}

	if ( (st = at25_global_unlock( flash )) < 0 ) {
		fprintf(stderr, "at25_global_unlock() failed\n");
		goto bail;
	}

	if ( (st = at25_block_erase( flash, flashAddr, bufsz )) < 0 ) {
		fprintf(stderr, "at25_block_erase() failed\n");
		goto bail;
	}

	cmd = AT25_CHECK_ERASED | AT25_EXEC_PROG | AT25_CHECK_VERIFY;
	if ( (st = at25_prog( flash, flashAddr, buf, serializedSize, cmd )) < 0 ) {
		fprintf(stderr, "at25_prog() failed\n");
		goto bail;
	}

	st = 0;

bail:
	at25_write_dis( flash );

	return st;
}

int
unitDataFromFlash(const struct UnitData **udp, struct FWInfo *fw)
{
	return flashOp( udp, fw, fromFlashCB );
}

int
unitDataToFlash(const struct UnitData *udp, struct FWInfo *fw)
{
	return flashOp( &udp, fw, toFlashCB );
}


