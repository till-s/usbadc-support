
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "fwComm.h"
#include "at24EepromSup.h"

typedef struct AT24EEPROM {
	struct FWInfo *fw;
	uint8_t        sla;    /* 7-bit address (e.g., 0x50) */
	unsigned       size;   /* size in bytes              */
    unsigned       pgSize; /* page size                  */
} AT24EEPROM;

struct AT24EEPROM *at24EepromCreate(
	struct FWInfo *fw,
	uint8_t        sla,    /* 7-bit address (e.g., 0x50) */
	unsigned       size,   /* size in bytes              */
    unsigned       pgSize  /* page size                  */
) {
	if ( sla > 0x7F ) {
		fprintf(stderr, "at24EepromCreate: invalid slave address\n");
		return 0;
	}
	if ( !! (size & (size - 1)) ) {
		fprintf(stderr, "at24EepromCreate: size must be a power of two\n");
		return 0;
	}
	if ( size >= 0x800 ) {
		fprintf(stderr, "at24EepromCreate: size must be < 0x800\n");
		return 0;
	}
	if ( !! (pgSize & (pgSize - 1)) ) {
		fprintf(stderr, "at24EepromCreate: page size must be a power of two\n");
		return 0;
	}

	AT24EEPROM *rval = malloc( sizeof(*rval) );
	if ( ! rval ) {
		fprintf(stderr, "at24EepromCreate: no memory\n");
		return 0;
	}
	rval->fw     = fw;
	rval->sla    = sla << 1;
	rval->size   = size;
	rval->pgSize = pgSize;
	return rval;
}

void
at24EepromDestroy(struct AT24EEPROM *eeprom)
{
	free( eeprom );
}

static int check(AT24EEPROM *eeprom, unsigned off, size_t len)
{
	if ( off >= eeprom->size || len >= eeprom->size || (off + len) >= eeprom->size ) {
		return -EINVAL;
	}
	return 0;
}

/* return bytes read or negative status on error */
int
at24EepromRead(struct AT24EEPROM *eeprom, unsigned off, uint8_t *buf, size_t len) {
	if ( check( eeprom, off, len ) ) {
		return -EINVAL;
	}
	uint8_t sla = eeprom->sla | I2C_READ | (((off >> 8) & 0x7) << 1 );
	return bb_i2c_rw_a8(eeprom->fw, sla, (off & 0xff), buf, len);
}

int
at24EepromGetSize(struct AT24EEPROM *eeprom)
{
	return eeprom->size;	
}

/* return bytes written or negative status on error */
int
at24EepromWrite(struct AT24EEPROM *eeprom, unsigned off, uint8_t *buf, size_t len)
{
unsigned        end;
unsigned        msk = eeprom->pgSize - 1;
unsigned        put;
struct timespec timo, now;
int             st;
uint8_t         sla;
unsigned        timeout_sec = 1;
unsigned        tot         = 0;

	if ( check( eeprom, off, len ) ) {
		return -EINVAL;
	}

	while ( len ) {
		end = (off + eeprom->pgSize) & ~msk;
		put = end - off;
		if ( put > len ) {
			put = len;
		}
		if ( clock_gettime( CLOCK_REALTIME, &timo ) ) {
			return -errno;
		}
		timo.tv_sec += timeout_sec;
		sla = (eeprom->sla & ~I2C_READ) | (((off >> 8) & 0x7) << 1 );
		do {
			st = bb_i2c_rw_a8( eeprom->fw, sla, off, buf, put );
			if ( st < 0 ) {
				if ( -ENODEV == st ) {
					/* NAK, possibly due to ongoing page write */
					if ( clock_gettime( CLOCK_REALTIME, &now ) ) {
						return -errno;
					}
					if ( now.tv_sec > timo.tv_sec || (now.tv_sec == timo.tv_sec && now.tv_nsec >= timo.tv_nsec ) ) {
						/* really no response */
						return st;
					}
				} else {
					return st;
				}
			} else {
				if ( put != st ) {
					/* not everything written; return the amount ACKed */
					return tot + st;
				}
			}
		} while ( st < 0 );
		tot += put;
		len -= put;
		off += put;
		buf += put;
	}
	return tot;
}

#ifdef __cplusplus
}
#endif
