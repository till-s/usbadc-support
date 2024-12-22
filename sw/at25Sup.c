#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>

#include "cmdXfer.h"
#include "fwComm.h"
#include "at25Sup.h"

#define VERIFY_AFTER_WRITE 0

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
#define AT25_OP_RESET_ENA  0x66
#define AT25_OP_RESET_EXE  0x99
#define AT25_OP_RESUME     0xAB

#define AT25_ST_BUSY       0x01
#define AT25_ST_WEL        0x02
#define AT25_ST_EPE        0x20

typedef struct AT25FlashParam {
	const char   *description;
	uint64_t      id;
	size_t        blockSize;
	size_t        pageSize;
	size_t        sizeBytes;
} AT25FlashParam;

static AT25FlashParam knownDevices[] = {
	/* Adesto AT25FF081A */
	{ description: "AT25FF081A", id: 0x1f45080100ULL, blockSize: 4096, pageSize: 256, sizeBytes: 1*1024*1024 },
	/* Adesto AT25SL641 */
	{ description: "AT25SL641",  id: 0x1f43171f43ULL, blockSize: 4096, pageSize: 256, sizeBytes: 8*1024*1024 }
};

struct AT25Flash {
	FWInfo         *fw;
	AT25FlashParam *devInfo;
};

static int
verify(AT25Flash *flash, unsigned addr, const uint8_t *cmp, size_t len, int addnl);

static int
do_xfer(AT25Flash *flash, const uint8_t *hdr, unsigned hlen, const uint8_t *tbuf, uint8_t *rbuf, unsigned buflen);

static int
do_xfer_bb(AT25Flash *flash, const uint8_t *hdr, unsigned hlen, const uint8_t *tbuf, uint8_t *rbuf, unsigned buflen);

static int
do_xfer_spi(AT25Flash *flash, const uint8_t *hdr, unsigned hlen, const uint8_t *tbuf, uint8_t *rbuf, unsigned buflen);


AT25Flash *
at25FlashOpen(FWInfo *fw, unsigned instance)
{
AT25Flash      *rv, *ok = 0;
int             st;
struct timespec wai;
uint64_t        idMask = 0x00ffffff0000;
int64_t         id;
int             i;

	if ( ! (rv = calloc( sizeof(*rv), 1 ) ) ) {
		fprintf(stderr, "at25FlashOpen(): no memory\n");
		return 0;
	}
	rv->fw = fw;

	if ( (st = at25_resume_updwn( rv )) ) {
		fprintf(stderr, "at25FlashOpen: resume failed: %s\n", strerror(-st));
		goto bail;
	}

	wai.tv_sec  = 0;
	wai.tv_nsec = 100*1000*1000;
	
	clock_nanosleep( CLOCK_REALTIME, 0, &wai, 0 );

	if ( (id = at25_id( rv )) < 0 ) {
		fprintf(stderr, "at25FlashOpen: reading ID failed: %s\n", strerror(-id));
		goto bail;
	}

	for ( i = 0; i < sizeof(knownDevices)/sizeof(knownDevices[0]); ++i ) {
		if ( 0 == ( (knownDevices[i].id ^ id) & idMask ) ) {
			rv->devInfo = &knownDevices[i];
			break;
		}
	}
	if ( ! rv->devInfo ) {
		if ( (id & idMask) == idMask || (id & idMask) == 0 ) {
			fprintf(stderr, "Reading JEDEC ID failed (got all-0 or all-1 ID)\n");
		} else {
			fprintf(stderr, "Flash Device (JEDEC IS 0x%010" PRIx64 ") not recognized; update knownDevices\n", id);
		}
		goto bail;
	}

	ok = rv;
	rv = 0;
bail:
	if ( rv ) {
		free (rv);
	}

	return ok;
}

size_t
at25FlashGetBlockSize(AT25Flash *flash)
{
	return flash && flash->devInfo ? flash->devInfo->blockSize : 0;
}

size_t
at25FlashGetSizeBytes(AT25Flash *flash)
{
	return flash && flash->devInfo ? flash->devInfo->sizeBytes : 0;
}


void
at25FlashClose(AT25Flash *flash)
{
	free( flash );
}

static int
__at25_get_id(AT25Flash *flash, uint8_t obuf[6])
{
int     i;
uint8_t buf[6];

	buf[0]  = AT25_OP_ID;
	buf[1]  = 0xff;
	buf[2]  = 0xff;
	buf[3]  = 0xff;
	buf[4]  = 0xff;
	buf[5]  = 0xff;

	if ( (i = do_xfer( flash, 0, 0, buf, obuf, 6 )) < 0 ) {
		return i;
	}

	return 0;
}

int
at25_print_id(AT25Flash *flash)
{
uint8_t buf[6];
int     i;

	if ( (i = __at25_get_id( flash, buf )) < 0 ) {
		return i;
	}

	for ( i = 1; i < 6; i++ ) {
		printf("0x%02x ", *(buf + i));
	}
	printf("\n");
	return 0;
}

int64_t
at25_id(AT25Flash *flash)
{
uint8_t buf[6];
int     i;
int64_t id;

	if ( (id = __at25_get_id( flash, buf )) < 0 ) {
		return id;
	}
	id = 0;
	for ( i = 1; i < 6; ++i ) {
		id = ( id << 8 ) | buf[i];
	}
	return id;
}
	
int
at25_spi_read(AT25Flash *flash, unsigned addr, uint8_t *rbuf, size_t len)
{
uint8_t  hdr[10];
unsigned hlen = 0;
int      st;

	hdr[hlen++] = AT25_OP_FAST_READ;
	hdr[hlen++] = (addr >> 16) & 0xff;
	hdr[hlen++] = (addr >>  8) & 0xff;
	hdr[hlen++] = (addr >>  0) & 0xff;
	hdr[hlen++] = 0x00; /* dummy     */

	if ( (st = do_xfer( flash, hdr, hlen, rbuf, rbuf, len )) != hlen + len ) {
		fprintf(stderr,"at25_spi_read -- receiving data failed or incomplete st %d, hlen %d, len %d\n", st, hlen, (unsigned)len);
		return st < 0 ? st : -EIO;
	}

	return len;
}

int
at25_status(AT25Flash *flash)
{
uint8_t buf[2];
int     st;
	buf[0] = AT25_OP_STATUS;
	if ( (st = do_xfer( flash, 0, 0, buf, buf, sizeof(buf) )) < 0 ) {
		fprintf(stderr, "at25_status: spi_xfer failed\n");
		return st;
	}
	return buf[1];
}

int
at25_cmd_2(AT25Flash *flash, uint8_t cmd, int arg)
{
uint8_t buf[2];
int     len = 0;
int     st;

	buf[len++] = cmd;
	if ( arg >= 0 && arg <= 255 ) {
		buf[len++] = arg;
	}

	if ( (st = do_xfer( flash, 0, 0, buf, buf, len )) < 0 ) {
		fprintf(stderr, "at25_cmd_2(0x%02x) transfer failed\n", cmd);
		return st;
	}
	return 0;
}

int
at25_cmd_1(AT25Flash *flash, uint8_t cmd)
{
	return at25_cmd_2( flash, cmd, -1 );
}

int
at25_write_ena(AT25Flash *flash)
{
	return at25_cmd_1( flash, AT25_OP_WRITE_ENA );
}

int
at25_write_dis(AT25Flash *flash)
{
	return at25_cmd_1( flash, AT25_OP_WRITE_DIS );
}

int
at25_reset(AT25Flash *flash)
{
	int st;
	if ( ( st = at25_cmd_1( flash, AT25_OP_RESET_ENA )) < 0 ) {
		return st;
	}
	return at25_cmd_1( flash, AT25_OP_RESET_EXE );
}

int
at25_resume_updwn(AT25Flash *flash)
{
	return at25_cmd_1( flash, AT25_OP_RESUME );
}

int
at25_status01_write(AT25Flash *flash, uint8_t val)
{
int st;
	if ( (st = at25_cmd_2    ( flash, AT25_OP_STATUS_WR, val )) ) {
		return st;
	}
	/* must wait until not busy anymore */
	return at25_status_poll( flash );
}

int
at25_global_unlock(AT25Flash *flash)
{
	/* must se write-enable again; global_unlock clears that bit */
	int st;
	if ( (st = at25_write_ena( flash )) < 0 ) {
		return st;
	}
    if ( (st = at25_status01_write( flash, 0x00 )) < 0) {
		return st;
	}
    if ( (st = at25_write_ena( flash )) < 0 ) {
		return st;
	}
	return 0;
}

int
at25_global_lock(AT25Flash *flash)
{
	int st;
	if ( (st = at25_write_ena( flash )) < 0 ) {
		return st;
	}
	if ( (st = at25_status01_write( flash, 0x3c )) < 0 ) {
		return st;
	}
	return 0;
}

int
at25_status_poll(AT25Flash *flash)
{
int st;
		/* poll status */
	do {
		if ( (st = at25_status( flash )) < 0 ) {
			fprintf(stderr, "at25_status_poll() - failed to read status\n");
			break;
		}
	} while ( (st & AT25_ST_BUSY) );

	return st;
}

int
at25_block_erase(AT25Flash *flash, unsigned addr, size_t sz)
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

	if ( (st = do_xfer( flash, 0, 0, buf, buf, l )) < 0 ) {
		fprintf(stderr, "at25_block_erase() -- sending command failed\n");
		return st;
	}

	if ( (st = at25_status_poll( flash )) < 0 ) {
		fprintf(stderr, "at25_block_erase() -- unable to poll status\n");
		return st;
	}

	if ( (st & AT25_ST_EPE) ) {
		fprintf(stderr, "at25_block_erase() -- programming error; status 0x%02x\n", st);
		return -EHWPOISON;
	}

	return 0;

}

static int verify(AT25Flash *flash, unsigned addr, const uint8_t *cmp, size_t len, int addnl)
{
uint8_t   buf[2048];
int       mismatch = 0;
unsigned  wrkAddr;
size_t    wrk, x;
int       got,i;

		for ( wrk = len, wrkAddr = addr; wrk > 0; wrk -= got, wrkAddr += got ) {
			x =  wrk > sizeof(buf) ? sizeof(buf) : wrk;
			got = at25_spi_read( flash, wrkAddr, buf, x );
			if ( got <= 0 ) {
				fprintf(stderr, "at25_prog() verification failed -- unable to read back\n");
				if ( 0 == got ) {
					got = -EIO;
				}
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
		if ( addnl ) {
			printf("\n");
		}
		return mismatch ? -EPROTO : 0;
}

int
at25_prog(AT25Flash *flash, unsigned addr, const uint8_t *data, size_t len, int check)
{
uint8_t        buf[2048];
unsigned       wrkAddr;
size_t         wrk, x;
int            i;
int            rval  = -1;
int            st;
const uint8_t *src;
uint8_t        junk[AT25_PAGE];

	if ( (check & AT25_CHECK_ERASED) ) {
		if ( (st = verify( flash, addr, 0, len, 1 )) < 0 )
			return st;
	}

	if ( (check & AT25_EXEC_PROG) ) {
		if ( (st = at25_global_unlock( flash )) < 0 ) {
			fprintf(stderr, "at25_prog() -- write-enable failed\n");
			return st;
		}

		wrk      = len;
		wrkAddr  = addr;
		src      = data;

		while ( wrk > 0 ) {

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

			if ( (rval = do_xfer( flash, buf, i, src, junk, x )) < 0 ) {
				fprintf(stderr, "at25_prog() - failed to transmit\n");
				goto bail;
			}

			/* should not be necessary but ensure CS is not
			 * reasserted too quickly (100ns) is specified (AT25SL641) but
			 * it is unclear if that applies to status queries, too.
			 */
			{
				struct timespec t;
				t.tv_sec  = 0;
				t.tv_nsec = 100000;
				nanosleep( &t, 0 );
			}

			if ( (rval = at25_status_poll( flash )) < 0 ) {
				fprintf(stderr, "at25_prog() - failed to poll status\n");
				goto bail;
			}

			if ( (st & AT25_ST_EPE) ) {
				fprintf(stderr, "at25_status_poll() -- programming error (status 0x%02x, writing page 0x%x) -- aborting\n", st, wrkAddr);
				rval = -EHWPOISON;
				goto bail;
			}

			if ( (check & AT25_CHECK_VERIFY) && VERIFY_AFTER_WRITE ) {
				if ( (st = verify( flash, wrkAddr, src, x, 0 )) < 0 ) {
					return st;
				}
			}

			/* programming apparently disables writing */
			at25_write_ena  ( flash ); /* just in case... */

			printf("%c", '.'); fflush(stdout);

			src     += x;
			wrk     -= x;
			wrkAddr += x;
		}
		printf("\n");

	}

	if ( (check & AT25_CHECK_VERIFY) ) {
		if ( (rval = verify( flash, addr, data, len, 1 )) < 0 ) {
			goto bail;
		}
	}

	rval = len;
	st   = 0;

bail:
	if ( (check & AT25_EXEC_PROG) ) {
		at25_global_lock( flash ); /* if this succeeds it clears the write-enable bit */
		at25_write_dis  ( flash ); /* just in case... */
	}

	return rval;
}

static int
do_xfer(AT25Flash *flash, const uint8_t *hdr, unsigned hlen, const uint8_t *tbuf, uint8_t *rbuf, unsigned buflen)
{
int st;
	if ( !! ( FW_FEATURE_SPI_CONTROLLER & fw_get_features( flash->fw ) ) ) {
		st = do_xfer_spi(flash, hdr, hlen, tbuf, rbuf, buflen);
	} else {
		st = do_xfer_bb(flash, hdr, hlen, tbuf, rbuf, buflen);
	}
	if ( -ETIMEDOUT == st ) {
		fprintf(stderr, "Error: transfer timed out\n");
	}
	return st;
}


static int
do_xfer_spi(AT25Flash *flash, const uint8_t *hdr, unsigned hlen, const uint8_t *tbuf, uint8_t *rbuf, unsigned buflen)
{
uint8_t cmd = fw_get_cmd( FW_CMD_SPI );
tbufvec tv[2];
rbufvec rv[2];
size_t  nt = 0;
size_t  nr = 0;
uint8_t hbuf[64];

	if ( hlen > sizeof(hbuf) ) {
		fprintf(stderr, "Internal error - header too big\n");
		abort();
	}

	if ( hlen > 0 ) {
		tv[nt].buf = hdr;
		tv[nt].len = hlen;
		nt++;
		
		rv[nr].buf = hbuf;
		rv[nr].len = hlen;
		nr++;
	}
	if ( buflen > 0 ) {
		tv[nt].buf = tbuf;
		tv[nt].len = buflen;
		nt++;
		rv[nr].buf = rbuf;
		rv[nr].len = buflen;
		nr++;
	}
	return fw_xfer_vec( flash->fw, cmd, tv, nt, rv, nr );
}

static int
do_xfer_bb(AT25Flash *flash, const uint8_t *hdr, unsigned hlen, const uint8_t *tbuf, uint8_t *rbuf, unsigned buflen)
{
unsigned onelen = 0;
int      rv     = 0;
int      st;
bb_vec   vec[2];
size_t   nelms = 0;

	if ( !  ( hlen > 0 && buflen > 0 ) ) {
		/* only one of tlen, hlen can be != 0 */
		onelen = hlen + buflen;
	}

	if ( 0 != onelen ) {
		return bb_spi_xfer( flash->fw, SPI_MODE0, SPI_FLASH, (hlen > 0 ) ? hdr : tbuf, rbuf, 0, onelen );
	} else {
		if ( hlen > 0 ) {
            vec[nelms].tbuf = hdr;
            vec[nelms].rbuf = 0;
            vec[nelms].zbuf = 0;
            vec[nelms].len  = hlen;
            nelms++;
		}
		if ( buflen > 0 ) {
            vec[nelms].tbuf = tbuf;
            vec[nelms].rbuf = rbuf;
            vec[nelms].zbuf = 0;
            vec[nelms].len  = buflen;
            nelms++;
		}
		if ( nelms ) {
			st = bb_spi_xfer_vec( flash->fw, SPI_MODE0, SPI_FLASH, vec, nelms );
			if ( st < 0 ) {
				fprintf(stderr, "do_xfer: bb_spi_xfer_vec failed\n");
				rv = st;
				goto bail;
			}
			rv += st;
		}
bail:
		return rv;
	}
}
