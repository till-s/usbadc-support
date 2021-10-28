#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "cmdXfer.h"
#include "fwComm.h"
#include "at25Sup.h"

#define AT25_PAGE          256
#define AT25_OP_ID         0x9f
#define AT25_OP_FAST_READ  0x0b
#define AT25_OP_WRITE_ENA  0x06
#define AT25_OP_WRITE_DIS  0x04
#define AT25_OP_PAGE_WRITE 0x02
#define AT25_OP_STATUS     0x05
#define AT25_OP_STATUS_WR  0x01
#define AT25_OP_ERASE_4K   0x20
#define AT25_OP_ERASE_32K  0x52
#define AT25_OP_ERASE_64K  0xD8
#define AT25_OP_ERASE_ALL  0x60

#define AT25_ST_BUSY       0x01
#define AT25_ST_WEL        0x02
#define AT25_ST_EPE        0x20

int
at25_id(FWInfo *fw)
{
uint8_t buf[128];
int     i;

	buf[0] = AT25_OP_ID;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;

	if ( bb_spi_xfer( fw, SPI_FLASH, buf, buf + 0x10, 0, 5 ) < 0 ) {
		return -1;
	}

	for ( i = 0; i < 5; i++ ) {
		printf("0x%02x ", *(buf + 0x10 + i));
	}
	printf("\n");
	return 0;
}

int
at25_spi_read(FWInfo *fw, unsigned addr, uint8_t *rbuf, size_t len)
{
uint8_t  hdr[10];
unsigned hlen = 0;
int      rval = -1;

	hdr[hlen++] = AT25_OP_FAST_READ;
	hdr[hlen++] = (addr >> 16) & 0xff;
	hdr[hlen++] = (addr >>  8) & 0xff;
	hdr[hlen++] = (addr >>  0) & 0xff;
	hdr[hlen++] = 0x00; /* dummy     */

	if ( bb_spi_cs( fw, SPI_FLASH, 0 ) < 0 ) {
		return -1;
	}

	if ( bb_spi_xfer_nocs( fw, SPI_FLASH, hdr, 0, 0, hlen ) != hlen ) {
		fprintf(stderr,"at25_spi_read -- sending header failed\n");
		goto bail;
	}

	if ( ( rval = bb_spi_xfer_nocs( fw, SPI_FLASH, 0, rbuf, 0, len ) ) != len ) {
		fprintf(stderr,"at25_spi_read -- receiving data failed or incomplete\n");
		goto bail;
	} 

bail:
	bb_spi_cs( fw, SPI_FLASH, 1 );
	return rval;
}

int
at25_status(FWInfo *fw)
{
uint8_t buf[2];
	buf[0] = AT25_OP_STATUS;
	if ( bb_spi_xfer( fw, SPI_FLASH, buf, buf, 0, sizeof(buf) ) < 0 ) {
		fprintf(stderr, "at25_status: bb_spi_xfer failed\n");
		return -1;
	}
	return buf[1];
}

int
at25_cmd_2(FWInfo *fw, uint8_t cmd, int arg)
{
uint8_t buf[2];
int     len = 0;

	buf[len++] = cmd;
	if ( arg >= 0 && arg <= 255 ) {
		buf[len++] = arg;
	}

	if ( bb_spi_xfer( fw, SPI_FLASH, buf, 0, 0, len ) < 0 ) {
		fprintf(stderr, "at25_cmd_2(0x%02x) transfer failed\n", cmd);
		return -1;
	}
	return 0;
}

int
at25_cmd_1(FWInfo *fw, uint8_t cmd)
{
	return at25_cmd_2( fw, cmd, -1 );
}

int
at25_write_ena(FWInfo *fw)
{
	return at25_cmd_1( fw, AT25_OP_WRITE_ENA );
}

int
at25_write_dis(FWInfo *fw)
{
	return at25_cmd_1( fw, AT25_OP_WRITE_DIS );
}

int
at25_global_unlock(FWInfo *fw)
{
	/* must se write-enable again; global_unlock clears that bit */
	return   at25_write_ena( fw )
          || at25_cmd_2    ( fw, AT25_OP_STATUS_WR, 0x00 )
          || at25_write_ena( fw );
}

int
at25_global_lock(FWInfo *fw)
{
	return at25_write_ena( fw ) || at25_cmd_2( fw, AT25_OP_STATUS_WR, 0x3c );
}

int
at25_status_poll(FWInfo *fw)
{
int st;
		/* poll status */
	do { 
		if ( (st = at25_status( fw )) < 0 ) {
			fprintf(stderr, "at25_status_poll() - failed to read status\n");
			break;
		}
	} while ( (st & AT25_ST_BUSY) );

	return st;
}

int
at25_block_erase(FWInfo *fw, unsigned addr, size_t sz)
{
uint8_t op = AT25_OP_ERASE_ALL; 

uint8_t buf[4];
int     l, st;

	if ( sz <= 4*1024 ) {
		op = AT25_OP_ERASE_4K;
	} else if ( sz <= 32*1024 ) {
		op = AT25_OP_ERASE_32K;
	} else if ( sz <= 64*1024 ) {
		op = AT25_OP_ERASE_64K;
	}

	l = 0;
	buf[l++] = op;

	if ( op != AT25_OP_ERASE_ALL ) {
		/* address will be implicitly down-aligned to next block boundary */
		buf[l++] = (addr >> 16) & 0xff;
		buf[l++] = (addr >>  8) & 0xff;
		buf[l++] = (addr >>  0) & 0xff;
	}

	if ( bb_spi_xfer( fw, SPI_FLASH, buf, 0, 0, l ) < 0 ) {
		fprintf(stderr, "at25_block_erase() -- sending command failed\n"); 
		return -1;
	}

	if ( (st = at25_status_poll( fw )) < 0 ) {
		fprintf(stderr, "at25_block_erase() -- unable to poll status\n");
		return -1;
	}

	if ( (st & AT25_ST_EPE) ) {
		fprintf(stderr, "at25_block_erase() -- programming error; status 0x%02x\n", st);
		return -1;
	}

	return 0;

}

static int verify(FWInfo *fw, unsigned addr, const uint8_t *cmp, size_t len)
{
uint8_t   buf[2048];
int       mismatch = 0;
unsigned  wrkAddr;
size_t    wrk, x;
int       got,i;

		for ( wrk = len, wrkAddr = addr; wrk > 0; wrk -= got, wrkAddr += got ) {
			x =  wrk > sizeof(buf) ? sizeof(buf) : wrk;
			got = at25_spi_read( fw, wrkAddr, buf, x );
			if ( got <= 0 ) {
				fprintf(stderr, "at25_prog() verification failed -- unable to read back\n");
				return got;
			}
			for ( i = 0; i < got; i++ ) {
				if ( buf[i] != (cmp ? cmp[i] : 0xff) ) {
					if ( cmp ) {
						fprintf(stderr, "Flash @ 0x%x mismatch : 0x%02x (expected 0x%02x)\n", wrkAddr + i, buf[i], cmp[i]);
					} else {
						fprintf(stderr, "Flash @ 0x%x not empty: 0x%02x\n", wrkAddr + i, buf[i]);
					}
					mismatch++;
				}
			}
			if ( cmp ) {
				cmp += got;
			}
			printf("%c", cmp ? 'v' : 'z'); fflush(stdout);
		}
		printf("\n");
		return mismatch ? -1 : 0;
}

int
at25_prog(FWInfo *fw, unsigned addr, const uint8_t *data, size_t len, int check)
{
uint8_t        buf[2048];
unsigned       wrkAddr;
size_t         wrk, x;
int            i;
int            rval  = -1;
int            st;
const uint8_t *src;

	if ( (check & AT25_CHECK_ERASED) ) {
		if ( verify( fw, addr, 0, len ) )
			return -1;
	}

	if ( (check & AT25_EXEC_PROG) ) {
		if ( at25_global_unlock( fw ) ) {
			fprintf(stderr, "at25_prog() -- write-enable failed\n");
			return -1;
		}

		wrk      = len;
		wrkAddr  = addr;
		src      = data;

		while ( wrk > 0 ) {

			if ( bb_spi_cs( fw, SPI_FLASH, 0 ) ) {
				fprintf(stderr, "at25_prog() - failed to assert CSb\n");
				goto bail;
			}

			/* page-align the end of the write */

			x = ( (wrkAddr + AT25_PAGE) & ~ (AT25_PAGE - 1) ) - wrkAddr;

			if ( x > wrk ) {
				x = wrk;
			}

			i = 0;
			buf[i++] = AT25_OP_PAGE_WRITE;
			buf[i++] = (wrkAddr >> 16) & 0xff;
			buf[i++] = (wrkAddr >>  8) & 0xff;
			buf[i++] = (wrkAddr >>  0) & 0xff;
			if ( bb_spi_xfer_nocs( fw, SPI_FLASH, buf, 0, 0, i ) < 0 ) {
				fprintf(stderr, "at25_prog() - failed to transmit address\n");
				goto bail;
			}
			if ( bb_spi_xfer_nocs( fw, SPI_FLASH, src, 0, 0, x ) < 0 ) {
				fprintf(stderr, "at25_prog() - failed to transmit data\n");
				goto bail;
			}

			/* this triggers the write */
			if ( bb_spi_cs( fw, SPI_FLASH, 1 ) ) {
				fprintf(stderr, "at25_prog() - failed to de-assert CSb\n");
				goto bail;
			}


			if ( (st = at25_status_poll( fw )) < 0 ) {
				fprintf(stderr, "at25_prog() - failed to poll status\n");
				goto bail;
			}

			if ( (st & AT25_ST_EPE) ) {
				fprintf(stderr, "at25_status_poll() -- programming error (status 0x%02x, writing page 0x%x) -- aborting\n", st, wrkAddr);
				goto bail;
			}

			/* programming apparently disables writing */
			at25_write_ena  ( fw ); /* just in case... */

			printf("%c", '.'); fflush(stdout);

			src     += x;
			wrk     -= x;
			wrkAddr += x;
		}
		printf("\n");

	}

	if ( (check & AT25_CHECK_VERIFY) ) {
		if ( verify( fw, addr, data, len ) ) {
			goto bail;
		}
	}

	rval = len;
	
bail:
	if ( (check & AT25_EXEC_PROG) ) {
		at25_global_lock( fw ); /* if this succeeds it clears the write-enable bit */
		at25_write_dis  ( fw ); /* just in case... */
	}

	bb_spi_cs( fw, SPI_NONE, 1 );     /* just in case... */

	return rval;
}
