#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "cmdXfer.h"
#include "fwComm.h"

#define CS_SHFT   0
#define SCLK_SHFT 1
#define MOSI_SHFT 2
#define MISO_SHFT 3
#define HIZ_SHFT  6

#define SPI_MASK  (0xf0)
#define I2C_MASK  (0xC1)

#define SDA_SHFT  4
#define SCL_SHFT  5
#define I2C_NAK   1

/* Must match firmware */
#define BITS_FW_CMD_VER         0x00
#define BITS_FW_CMD_BB          0x01
#define BITS_FW_CMD_BB_NONE     (0<<4)
#define BITS_FW_CMD_BB_FLASH    (1<<4)
#define BITS_FW_CMD_BB_ADC      (2<<4)
#define BITS_FW_CMD_BB_PGA      (3<<4)
#define BITS_FW_CMD_BB_I2C      (4<<4)
#define BITS_FW_CMD_ADCBUF      0x02

struct FWInfo {
	int        fd;
	uint8_t    cmd;
	int        debug;
	int        ownFd;
};

static int
fw_xfer(FWInfo *fw, uint8_t subcmd, const uint8_t *tbuf, uint8_t *rbuf, size_t len);

static int
__bb_spi_cs(FWInfo *fw, uint8_t subcmd, int val);

void
fw_set_debug(FWInfo *fw, int level)
{
	fw->debug = level;
}

static uint8_t
spi_get_subcmd(SPIDev type)
{
	switch ( type ) {
		case SPI_FLASH : return BITS_FW_CMD_BB_FLASH;
		case SPI_ADC   : return BITS_FW_CMD_BB_ADC;
		case SPI_PGA   : return BITS_FW_CMD_BB_PGA;
		default:
			fprintf(stderr, "spi_get_subcmd() -- illegal switch case\n");
			abort();
	}
}

uint8_t
fw_get_cmd(FWCmd aCmd)
{
	switch ( aCmd ) {
		case FW_CMD_VERSION: return BITS_FW_CMD_VER;
		case FW_CMD_ADC_BUF: return BITS_FW_CMD_ADCBUF;
		case FW_CMD_BB_SPI : return BITS_FW_CMD_BB | BITS_FW_CMD_BB_FLASH;
		case FW_CMD_BB_I2C : return BITS_FW_CMD_BB | BITS_FW_CMD_BB_I2C;
		default:
			fprintf(stderr, "spi_get_subcmd() -- illegal switch case\n");
			abort();
	}
}

FWInfo *
fw_open(const char *devn, unsigned speed)
{
int     fd = fifoOpen( devn, speed );
FWInfo *rv;

	if ( fd < 0 ) {
		return 0;
	}

	rv = fw_open_fd( fd );
	if ( rv ) {
		rv->ownFd = 1;
	}
	return rv;
}

FWInfo *
fw_open_fd(int fd)
{
FWInfo *rv;

	if ( ! (rv = malloc(sizeof(*rv))) ) {
		perror("fw_open(): no memory");
		return 0;
	}

	rv->fd    = fd;
	rv->cmd   = BITS_FW_CMD_BB;
	rv->debug = 0;
	return rv;
}

void
fw_close(FWInfo *fw)
{
uint8_t v = SPI_MASK | I2C_MASK;
	if ( fw ) {
		fw_xfer(fw, BITS_FW_CMD_BB_NONE, &v, &v, sizeof(v) );
		if ( fw->ownFd ) {
			fifoClose( fw->fd );
		}
		free( fw );
	}
}

#define DEPTH    512 /* fifo depth */
#define MAXDEPTH 500

static int
fw_xfer(FWInfo *fw, uint8_t subCmd, const uint8_t *tbuf, uint8_t *rbuf, size_t len)
{
uint8_t cmdLoc = fw->cmd | subCmd;
	return fifoXferFrame( fw->fd, &cmdLoc, tbuf, tbuf ? len : 0, rbuf, rbuf ? len : 0 ) < 0 ? -1 : 0;
}

static void pr_i2c_dbg(uint8_t tbyte, uint8_t rbyte)
{
	printf("Writing %02x - got %02x (%d %d - %d %d)\n", tbyte, rbyte,
			!!(tbyte & (1<<SCL_SHFT)) , !!(tbyte & (1<<SDA_SHFT)),
			!!(rbyte & (1<<SCL_SHFT)) , !!(rbyte & (1<<SDA_SHFT)));
}

static int
bb_i2c_set(FWInfo *fw, int scl, int sda)
{
uint8_t bbbyte =  ((scl ? 1 : 0) << SCL_SHFT) | ((sda ? 1 : 0) << SDA_SHFT) | I2C_MASK;
uint8_t x = bbbyte;

	if ( fw_xfer( fw, BITS_FW_CMD_BB_I2C, &bbbyte, &bbbyte, 1 ) < 0 ) {
		fprintf(stderr, "bb_i2c_set: unable to set levels\n");
		return -1;
	}
	if ( fw->debug ) {
		pr_i2c_dbg(x, bbbyte);
	}
	return bbbyte;
}

int
bb_i2c_start(FWInfo *fw, int restart)
{
	if ( fw->debug ) {
		printf("bb_i2c_start:\n");
	}
	if ( restart ) {
		if ( bb_i2c_set(fw, 0, 1 ) < 0 ) {
			return -1;
		}
		if ( bb_i2c_set(fw, 1, 1) < 0 ) {
			return -1;
		}
	}

	if ( bb_i2c_set(fw, 1, 0) < 0 ) {
		return -1;
	}

	if ( bb_i2c_set(fw, 0, 0) < 0 ) {
		return -1;
	}

	return 0;
}

int
bb_i2c_stop(FWInfo *fw)
{
	if ( fw->debug ) {
		printf("bb_i2c_stop:\n");
	}
	if ( bb_i2c_set( fw, 1, 0 ) < 0 ) {
		return -1;
	}

	if ( bb_i2c_set( fw, 1, 1 ) < 0 ) {
		return -1;
	}
	return 0;
}

static int
__bb_spi_cs(FWInfo *fw, uint8_t subcmd, int val)
{
uint8_t bbbyte = ( ((val ? 1 : 0) << CS_SHFT) | (0 << SCLK_SHFT) ) | SPI_MASK;

	if ( fw_xfer( fw, subcmd, &bbbyte, &bbbyte, 1 ) < 0 ) {
		fprintf(stderr, "Unable to set CS %d\n", !!val);
		return -1;
	}
	return 0;
}

int
bb_spi_cs(FWInfo *fw, SPIDev type, int val)
{
	return __bb_spi_cs( fw, spi_get_subcmd( type ), val );
}

#define BUF_BRK 1024

int
bb_spi_xfer_nocs(FWInfo *fw, SPIDev type, const uint8_t *tbuf, uint8_t *rbuf, uint8_t *zbuf, size_t len)
{
uint8_t *xbuf = malloc(BUF_BRK*8*2);
uint8_t *p;
int      rval = -1;
size_t   work = len;
size_t   xlen;
int      i,j;
uint8_t  v,z;
uint8_t  subcmd = spi_get_subcmd( type );

	if ( ! xbuf ) {
		perror("bb_spi_xfer_nocs(): unable to allocate buffer memory");
	}

	while ( work > 0 ) {
		xlen = work > BUF_BRK ? BUF_BRK : work;
		p    = xbuf;

		for ( i = 0; i < xlen; i++ ) {
			v = tbuf ? tbuf[i] : 0;
            z = zbuf ? zbuf[i] : 0;
			for ( j = 0; j < 2*8; j += 2 ) {
				p[j + 0] = ( (((v & 0x80) ? 1 : 0) << MOSI_SHFT) | (0 << SCLK_SHFT) | (0 << CS_SHFT) ) | SPI_MASK ;
				if ( ! (z & 0x80) ) {
					p[j + 0] &= ~(1 << HIZ_SHFT);
				}
				p[j + 1] = p[j+0] | (1 << SCLK_SHFT);
				v      <<= 1;
				z      <<= 1;
			}
			p += j;
		}

		if ( fw_xfer( fw, subcmd, xbuf, xbuf, xlen*2*8 ) ) {
			fprintf(stderr, "bb_spi_xfer_nocs(): fw_xfer failed\n");
			goto bail;
		}

		if ( rbuf ) {

			p = xbuf;

			for ( i = 0; i < xlen; i++ ) {
				v = 0;
				for ( j = 0; j < 2*8; j += 2 ) {
					v   = (v << 1 ) | ( (p[j + 1] & (1 << MISO_SHFT)) ? 1 : 0 );
				}
				p += j;
				rbuf[i] = v;
			}
			
			rbuf += xlen;
		}
		if ( tbuf ) tbuf += xlen;
		work             -= xlen;
	}

	rval = len;

bail:
	free( xbuf );
	return rval;
}

int
bb_spi_xfer(FWInfo *fw, SPIDev type, const uint8_t *tbuf, uint8_t *rbuf, uint8_t *zbuf, size_t len)
{
uint8_t  subcmd = spi_get_subcmd( type );
int      got;


	if ( __bb_spi_cs( fw, subcmd, 0 ) ) {
		return -1;
	}

	got = bb_spi_xfer_nocs( fw, type, tbuf, rbuf, zbuf, len );

	if ( __bb_spi_cs( fw, subcmd, 1 ) ) {
		return -1;
	}

	return got;
}

/* XFER 9 bits (lsbit is ACK) */
static int
bb_i2c_xfer(FWInfo *fw, uint16_t val)
{
int i;
uint8_t xbuf[3*9];
uint8_t rbuf[3*9];
	for ( i = 0; i < 3*9; i+= 3 ) {
		uint8_t sda = (((val & (1<<8)) ? 1 : 0) << SDA_SHFT);
		xbuf[i + 0] = (I2C_MASK | (0 << SCL_SHFT) | sda);
		if ( i == 7*3 ) xbuf[i+0] &= 0x7f; 
		xbuf[i + 1] = (I2C_MASK | (1 << SCL_SHFT) | sda);
		xbuf[i + 2] = (I2C_MASK | (0 << SCL_SHFT) | sda); 
		val <<= 1;
	}
	if ( fw_xfer( fw, BITS_FW_CMD_BB_I2C, xbuf, rbuf, sizeof(xbuf)) ) {
		fprintf(stderr, "bb_i2c_xfer failed\n");
		return -1;
	}
	val = 0;
	for ( i = 0; i < 3*9; i+= 3 ) {
		val = (val<<1) | ( ( rbuf[i+2] & ( 1 << SDA_SHFT ) ) ? 1 : 0 );
	}
	if ( fw->debug ) {
		for ( i = 0; i < 3*9; i++ ) {
			pr_i2c_dbg(xbuf[i], rbuf[i]);
		}
	}
	
	return val & 0x1FF;
}

int
bb_i2c_read(FWInfo *fw, uint8_t *buf, size_t len)
{
uint16_t v;
int      got;
size_t   i;
	for ( i = 0; i < len; i++ ) {
		/* ACK all but the last bit */
		v = ( 0xff << 1 ) | (i == len - 1 ? I2C_NAK : 0);
		if ( ( got = bb_i2c_xfer( fw, v ) ) < 0 ) {
			return -1;
		}
		buf[i] = ( got >> 1 ) & 0xff;
	}
	return len;
}

int
bb_i2c_write(FWInfo *fw, uint8_t *buf, size_t len)
{
uint16_t v;
int      got;
size_t   i;
	for ( i = 0; i < len; i++ ) {
		v = ( buf[i] << 1 ) | I2C_NAK; /* set ACK when sending; releases to the slave */
		if ( ( got = bb_i2c_xfer( fw, v ) ) < 0 ) {
			return -1;
		}
		if ( ( got & I2C_NAK ) ) {
			fprintf(stderr, "bb_i2c_write - byte %i received NAK\n", (unsigned)i);
			return i;
		}
	}
	return len;
}

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

			if ( __bb_spi_cs( fw, BITS_FW_CMD_BB_FLASH, 0 ) ) {
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
			if ( __bb_spi_cs( fw, BITS_FW_CMD_BB_FLASH, 1 ) ) {
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

	__bb_spi_cs( fw, BITS_FW_CMD_BB_NONE, 1 );     /* just in case... */

	return rval;
}
