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
		case SPI_NONE  : return BITS_FW_CMD_BB_NONE;
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
		goto bail;
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

static int
bb_i2c_rw_reg(FWInfo *fw, uint8_t sla, uint8_t reg, int val)
{
uint8_t buf[3];
int     wrl;
int     rval = -1;

    wrl        = 0;
	buf[wrl++] = sla;
	buf[wrl++] = reg;
	if ( val >= 0 ) {
		buf[wrl++] = (uint8_t)(val & 0xff);
	}
	if ( bb_i2c_start( fw, 0 ) )
		return -1;
	if ( bb_i2c_write( fw, buf, wrl ) < 0 ) {
		goto bail;
	}
	if ( val < 0 ) {
		if ( bb_i2c_start( fw, 1 ) ) {
			goto bail;
		}
		buf[0] = sla | I2C_READ;
		if ( bb_i2c_write( fw, buf, 1 ) < 0 ) {
			goto bail;
		}
		if ( bb_i2c_read( fw, buf, 1 ) < 0 ) {
			goto bail;
		}
		rval = buf[0];
	} else {
		rval = 0;
	}

bail:
	/* attempt to stop */
	if ( bb_i2c_stop( fw ) < 0 ) {
		rval = -1;
	}
	return rval;
}

/*
 * RETURNS: read value, -1 on error;
 */
int
bb_i2c_read_reg(FWInfo *fw, uint8_t sla, uint8_t reg)
{
	return bb_i2c_rw_reg(fw, sla, reg, -1);
}

/*
 * RETURNS: 0 on success, -1 on error;
 */
int
bb_i2c_write_reg(FWInfo *fw, uint8_t sla, uint8_t reg, uint8_t val)
{
	return bb_i2c_rw_reg(fw, sla, reg, val);
}
