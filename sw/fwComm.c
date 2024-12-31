#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>
#include <termios.h>
#include <errno.h>
#include <string.h>

#include "cmdXfer.h"
#include "fwComm.h"
#include "at24EepromSup.h"
#include "scopeSup.h"

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
#define BITS_FW_CMD_BB_FEG      (5<<4)
#define BITS_FW_CMD_BB_VGA      (6<<4)
#define BITS_FW_CMD_BB_VGB      (7<<4)
#define BITS_FW_CMD_ADCBUF      0x02
#define BITS_FW_CMD_ADCFLUSH    (1<<4)
#define BITS_FW_CMD_MEMSIZE     (2<<4)
#define BITS_FW_CMD_SMPLFREQ    (3<<4) /* API vers 3 */
#define BITS_FW_CMD_ACQPRM      0x03
#define BITS_FW_CMD_SPI         0x04

#define BITS_FW_CMD_REG         0x05
#define BITS_FW_CMD_REG_RD8     (0<<4)
#define BITS_FW_CMD_REG_WR8     (1<<4)

#define BITS_FW_CMD_UNSUPPORTED   0xff

struct FWInfo {
	int             fd;
	uint8_t         cmd;
	int             debug;
	int             ownFd;
	uint32_t        gitHash;
	uint8_t         brdVers;
	uint8_t         apiVers;
	uint64_t        features;
	AT24EEPROM     *eeprom;
};

static int
fw_xfer_bb(FWInfo *fw, uint8_t subcmd, const uint8_t *tbuf, uint8_t *rbuf, size_t len);

static int
__bb_spi_cs(FWInfo *fw, SPIMode mode, uint8_t subcmd, uint8_t lastval);

void
fw_set_debug(FWInfo *fw, int level)
{
	fw->debug = level;
	fifoSetDebug( (level > 1) );
}

static int
spi_get_subcmd(FWInfo *fw, SPIDev type)
{
unsigned long sup = (1<<SPI_NONE) | (1<<SPI_FLASH) | (1<<SPI_ADC);

	switch ( fw->brdVers ) {
		case 0:
			sup |= (1<<SPI_PGA) | (1<<SPI_FEG); break;
		case 1:
		case 2:
			sup |= (1<<SPI_PGA);                break;
		default:
			fprintf(stderr, "spi_get_subcmd(): unsupported board/hw version %i\n", fw->brdVers);
			return -ENOTSUP;
	}
	if ( ! ((1<<type) & sup) ) {
		fprintf(stderr, "spi_get_subcmd(): SPI device %i not supported on this board/hw\n", type);
		return -ENOTSUP;
	}
	switch ( type ) {
		case SPI_NONE  : return BITS_FW_CMD_BB_NONE;
		case SPI_FLASH : return BITS_FW_CMD_BB_FLASH;
		case SPI_ADC   : return BITS_FW_CMD_BB_ADC;
		case SPI_PGA   : return BITS_FW_CMD_BB_PGA;
		case SPI_FEG   : return BITS_FW_CMD_BB_FEG;
		case SPI_VGA   : return BITS_FW_CMD_BB_VGA;
		case SPI_VGB   : return BITS_FW_CMD_BB_VGB;
		default:
			fprintf(stderr, "spi_get_subcmd() -- illegal switch case\n");
			abort();
	}
}

uint8_t
fw_get_cmd(FWCmd aCmd)
{
	switch ( aCmd ) {
		case FW_CMD_VERSION    : return BITS_FW_CMD_VER;
		case FW_CMD_ADC_BUF    : return BITS_FW_CMD_ADCBUF;
		case FW_CMD_ADC_FLUSH  : return BITS_FW_CMD_ADCBUF | BITS_FW_CMD_ADCFLUSH;
		case FW_CMD_BB_SPI     : return BITS_FW_CMD_BB | BITS_FW_CMD_BB_FLASH;
		case FW_CMD_BB_I2C     : return BITS_FW_CMD_BB | BITS_FW_CMD_BB_I2C;
		case FW_CMD_ACQ_PARMS  : return BITS_FW_CMD_ACQPRM;
		case FW_CMD_SPI        : return BITS_FW_CMD_SPI;
        case FW_CMD_REG_RD8    : return BITS_FW_CMD_REG | BITS_FW_CMD_REG_RD8;
        case FW_CMD_REG_WR8    : return BITS_FW_CMD_REG | BITS_FW_CMD_REG_WR8;
		default:
			fprintf(stderr, "spi_get_subcmd() -- illegal switch case\n");
			abort();
	}
}

uint64_t
fw_get_features(FWInfo *fw)
{
	return fw->features;
}

void
fw_disable_features(FWInfo *fw, uint64_t mask)
{
	fw->features &= ~mask;
}

static int64_t
__fw_get_version(FWInfo *fw)
{
uint8_t buf[sizeof(int64_t)];
int     got, i;
uint8_t cmd = fw_get_cmd( FW_CMD_VERSION );
int64_t rval;

	got = fw_xfer( fw, cmd, 0, buf, sizeof(buf) );
	if ( got < 0 ) {
		return (int64_t)got;
	}
	rval = 0;
	for ( i = 0; i < got; i++ ) {
		rval = (rval << 8) | buf[i];
	}
	return rval;
}

FWInfo *
fw_open(const char *devn, unsigned speed)
{
/* set speed to B0 which should deassert DTR; this
 * allows firmwares to MUX a FIFO and a UART using
 * DTR as the mux control signal. Unlikely that
 * there is an actual modem...
 */
int     fd = fifoOpen( devn, B0 );
FWInfo *rv;

	if ( fd < 0 ) {
		return 0;
	}

	rv = fw_open_fd( fd );
	if ( rv ) {
		rv->ownFd = 1;
	} else {
		close( fd );
	}
	return rv;
}

FWInfo *
fw_open_fd(int fd)
{
FWInfo  *fw;
int64_t  vers;

	if ( ! (fw = calloc( sizeof( *fw ), 1 )) ) {
		perror("fw_open(): no memory");
		return NULL;
	}

	fw->fd             = fd;
	fw->cmd            = BITS_FW_CMD_BB;
	fw->debug          = 0;
	fw->ownFd          = 0;
	fw->features       = 0;

	switch ( __fw_has_buf( fw, NULL, NULL ) ) {
		case BUF_SIZE_FAILED:
			fprintf(stderr, "Error: fw_open_fd unable to retrieve target memory size\n");
			break;
		case BUF_SIZE_NOTSUP:
			break;
		default:
			fw->features |= FW_FEATURE_ADC;
			break;

	}

	if ( ( vers = __fw_get_version( fw ) ) < 0 ) {
		fprintf(stderr, "Error: fw_open_fd unable to retrieve firmware version\n");
		goto bail;
	}

	fw->gitHash = ( vers & 0xffffffff );
	fw->apiVers = ( (vers >> 32) & 0xff );
    fw->brdVers = ( (vers >> 40) & 0xff );

	/* avoid a timeout on old fw */
	if ( fw->apiVers >= FW_API_VERSION_1 &&  0 == fw_xfer( fw, BITS_FW_CMD_SPI, 0, 0, 0 ) ) {
		fw->features |= FW_FEATURE_SPI_CONTROLLER;
	}

	/* abiVers etc. valid after this point */

	switch ( fw->brdVers ) {
		case 2:
			fw->eeprom         = at24EepromCreate( fw, 0x50, 128, 8 );
			break;
		default:
			break;
	}

	return fw;

bail:
	fw_close( fw );
	return NULL;
}

void
fw_close(FWInfo *fw)
{
uint8_t v = SPI_MASK | I2C_MASK;
	if ( fw ) {
		fw_xfer_bb(fw, BITS_FW_CMD_BB_NONE, &v, &v, sizeof(v) );
		if ( fw->ownFd ) {
			fifoClose( fw->fd );
		}
		if ( fw->eeprom ) {
			at24EepromDestroy( fw->eeprom );
		}
		free( fw );
	}
}

int
bb_spi_raw(FWInfo *fw, SPIDev type, int clk, int mosi, int cs, int hiz)
{
uint8_t v = SPI_MASK;
int     rv;
uint8_t subcmd;

	rv = spi_get_subcmd( fw, type );

	if ( rv < 0 ) {
		return rv;
	}

	subcmd = (uint8_t) rv;

	v &= ~( (1<<CS_SHFT) | (1<<SCLK_SHFT) | (1<<MOSI_SHFT) | (1<<HIZ_SHFT) );

	if ( cs ) {
		v |= (1 << CS_SHFT);
	}
	if ( clk ) {
		v |= (1 << SCLK_SHFT);
	}
	if ( mosi ) {
		v |= (1 << MOSI_SHFT);
	}
	if ( hiz ) {
		v |= (1 << HIZ_SHFT);
	}
	rv = fw_xfer_bb(fw, subcmd, &v, &v, sizeof(v) );
	if ( rv >= 0 ) {
		rv = !! (v & (1<<MISO_SHFT));
	}
	return rv;
}

#define DEPTH    512 /* fifo depth */
#define MAXDEPTH 500

static int
fw_xfer_bb(FWInfo *fw, uint8_t subCmd, const uint8_t *tbuf, uint8_t *rbuf, size_t len)
{
uint8_t cmdLoc = fw->cmd | subCmd;
int     st;

    st = fw_xfer( fw, cmdLoc, tbuf, rbuf, len );
    return st < 0 ? st : 0;
}

/* Caution: fw_xfer is called from fw_open and not all fields are initialized yet
 *          (but fd is).
 */
int
fw_xfer(FWInfo *fw, uint8_t cmd, const uint8_t *tbuf, uint8_t *rbuf, size_t len)
{
uint8_t cmdLoc = cmd;
int     st;

	st = fifoXferFrame( fw->fd, &cmdLoc, tbuf, tbuf ? len : 0, rbuf, rbuf ? len : 0 );
	if ( BITS_FW_CMD_UNSUPPORTED == cmdLoc ) {
		st = -ENOTSUP;
	}
	return st;
}

int
fw_xfer_vec(FWInfo *fw, uint8_t cmd, const tbufvec *tbuf, size_t tcnt, const rbufvec *rbuf, size_t rcnt)
{
uint8_t cmdLoc = cmd;
int     st;
	st = fifoXferFrameVec( fw->fd, &cmdLoc, tbuf, tcnt, rbuf, rcnt );
	if ( BITS_FW_CMD_UNSUPPORTED == cmdLoc ) {
		st = -ENOTSUP;
	}
	return st;
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
int     st;

	if ( (st = fw_xfer_bb( fw, BITS_FW_CMD_BB_I2C, &bbbyte, &bbbyte, 1 )) < 0 ) {
		fprintf(stderr, "bb_i2c_set: unable to set levels\n");
		return st;
	}
	if ( fw->debug ) {
		pr_i2c_dbg(x, bbbyte);
	}
	return bbbyte;
}

int
bb_i2c_start(FWInfo *fw, int restart)
{
	int st;
	if ( fw->debug ) {
		printf("bb_i2c_start(restart = %i):\n", restart);
	}
	if ( restart ) {
		if ( (st = bb_i2c_set(fw, 0, 1)) < 0 ) {
			return st;
		}
		if ( (st = bb_i2c_set(fw, 1, 1)) < 0 ) {
			return st;
		}
	}

	if ( (st = bb_i2c_set(fw, 1, 0)) < 0 ) {
		return st;
	}

	if ( (st = bb_i2c_set(fw, 0, 0)) < 0 ) {
		return st;
	}

	return 0;
}

int
bb_i2c_stop(FWInfo *fw)
{
	int st;
	if ( fw->debug ) {
		printf("bb_i2c_stop:\n");
	}
	if ( (st = bb_i2c_set( fw, 1, 0 )) < 0 ) {
		return st;
	}

	if ( (st = bb_i2c_set( fw, 1, 1 )) < 0 ) {
		return st;
	}
	return 0;
}

static int
__bb_spi_cs(FWInfo *fw, SPIMode mode, uint8_t subcmd, uint8_t lastval)
{
uint8_t buf[2];
int     csWasHi = !! ( lastval & (1 << CS_SHFT) );
int     st;

	if ( SPI_MODE0 == mode || SPI_MODE1 == mode ) {
       lastval &= ~(1 << SCLK_SHFT);
    } else {
       lastval |=  (1 << SCLK_SHFT);
	}
	if ( ! csWasHi ) {
		/* CS will be deasserted below; make SDIO hi-z */
		lastval |= (1 << HIZ_SHFT);
	}
	buf[0]   = lastval;
	/* Toggle CS */
	lastval ^= (1 << CS_SHFT);
	buf[1]   = lastval;

	if ( (st = fw_xfer_bb( fw, subcmd, buf, buf, sizeof(buf) )) < 0 ) {
		fprintf(stderr, "Unable to set CS to %d\n", !csWasHi);
		return st;
	}
	return lastval;
}

#define BUF_BRK 1024

/* work buffer is assumed to have space for stretch*2*8*len octets */
static void
shift_into_buf(uint8_t *xbuf, SPIMode mode, unsigned stretch, const uint8_t *tbuf, const uint8_t *zbuf, size_t len)
{
int      i,j,k;
uint8_t  bbo;
uint8_t *p;
uint8_t  z,v;
uint8_t  msk;

	p = xbuf;

	msk = SPI_MASK & ~ (1 << CS_SHFT);
	if ( SPI_MODE3 == mode || SPI_MODE1 == mode ) {
		msk |= ( 1 << SCLK_SHFT );
	}

	for ( i = 0; i < len; i++ ) {
		v = tbuf ? tbuf[i] : 0;
		z = zbuf ? zbuf[i] : 0;
		for ( j = 0; j < 8; j++ ) {
			bbo = ( (((v & 0x80) ? 1 : 0) << MOSI_SHFT) | msk );
			if ( ! (z & 0x80) ) {
				bbo &= ~(1 << HIZ_SHFT);
			}
			for ( k = 0; k < stretch; k++ ) {
				*p = bbo;
				++p;
			}
            bbo     ^= (1 << SCLK_SHFT);
			for ( k = 0; k < stretch; k++ ) {
				*p = bbo;
				++p;
			}
			v      <<= 1;
			z      <<= 1;
		}
	}
}

static void
shift_outof_buf(uint8_t *xbuf, unsigned stretch, uint8_t *rbuf, size_t len)
{
int      i,j;
uint8_t *p;
uint8_t  v;

	p = xbuf;

	for ( i = 0; i < len; i++ ) {
		v = 0;
		for ( j = 0; j < 2*8; j += 2 ) {
			v   = (v << 1 ) | ( (p[stretch*j + 2*stretch - 1] & (1 << MISO_SHFT)) ? 1 : 0 );
		}

		p += stretch * j;
		rbuf[i] = v;
	}
}

int
bb_spi_xfer_vec(FWInfo *fw, SPIMode mode, SPIDev type, const struct bb_vec *vec, size_t nelms)
{
int               stretch = 1;
uint8_t           buf[BUF_BRK*8*2*stretch];
int               el;
int               rval = 0;
uint8_t           subcmd;
size_t            work;
size_t            xlen;
size_t            stretchlen;
const uint8_t    *tbuf;
uint8_t          *rbuf;
const uint8_t    *zbuf;
uint8_t           last;
int               st;

	if ( ( el = spi_get_subcmd( fw, type ) ) < 0 ) {
		/* message already printed */
		return el;
	}
	subcmd = (uint8_t) el;


	/* assert CS */
	if ( (el = __bb_spi_cs( fw, mode, subcmd, SPI_MASK | (1 << CS_SHFT) )) < 0 ) {
		return el;
	}
	last = el;

	for ( el = 0; el < nelms; el++ ) {

		work = vec[el].len;
        tbuf = vec[el].tbuf;
        rbuf = vec[el].rbuf;
        zbuf = vec[el].zbuf;

		rval += work;

		while ( work > 0 ) {
			xlen = work > BUF_BRK ? BUF_BRK : work;

		    stretchlen = xlen * 2 * 8 * stretch;

			shift_into_buf( buf, mode, stretch, tbuf, zbuf, xlen );

			/* keep a copy of the last bbo */
			last = buf[stretchlen - 1];

			if ( (st = fw_xfer_bb( fw, subcmd, buf, buf, stretchlen ) ) < 0 ) {
				fprintf(stderr, "bb_spi_xfer_vec(): fw_xfer_bb failed\n");
				return st;
			}

			if ( rbuf ) {

				shift_outof_buf( buf, stretch, rbuf, xlen );

				rbuf += xlen;
			}
			if ( tbuf ) tbuf += xlen;
			if ( zbuf ) zbuf += xlen;
			work             -= xlen;
		}
	}

	/* deassert CS */
	if ( (st = __bb_spi_cs( fw, mode, subcmd, last )) < 0 ) {
		return st;
	}

	return rval;
}

int
bb_spi_xfer(FWInfo *fw, SPIMode mode, SPIDev type, const uint8_t *tbuf, uint8_t *rbuf, uint8_t *zbuf, size_t len)
{
bb_vec vec[1];
size_t nelms = 0;

	vec[nelms].tbuf = tbuf;
	vec[nelms].rbuf = rbuf;
	vec[nelms].zbuf = zbuf;
	vec[nelms].len  = len;
	nelms++;
	return bb_spi_xfer_vec(fw, mode, type, vec, nelms);
}

/* XFER 9 bits (lsbit is ACK) */
static int
bb_i2c_xfer(FWInfo *fw, uint16_t val)
{
int i,st;
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
	if ( (st = fw_xfer_bb( fw, BITS_FW_CMD_BB_I2C, xbuf, rbuf, sizeof(xbuf))) < 0 ) {
		fprintf(stderr, "bb_i2c_xfer failed\n");
		return st;
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
			return got;
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
			return got;
		}
		if ( ( got & I2C_NAK ) ) {
			if ( fw->debug ) {
				fprintf(stderr, "bb_i2c_write - byte %i received NAK\n", (unsigned)i);
			}
			return i;
		}
	}
	return len;
}

int
bb_i2c_rw_a8(FWInfo *fw, uint8_t sla, uint8_t addr, uint8_t *data, size_t len)
{
uint8_t buf[3];
int     wrl;
int     rval = -1;
int     st;

    wrl        = 0;
	buf[wrl++] = sla & ~I2C_READ;
	buf[wrl++] = addr;
	if ( (rval = bb_i2c_start( fw, 0 )) < 0 )
		return rval;
	if ( (rval = bb_i2c_write( fw, buf, wrl )) < wrl ) {
		if ( rval >= 0 ) {
			/* NAK on first byte => -ENODEV */
			rval = (0 == rval ? -ENODEV : -EIO );
		}
		goto bail;
	}
	if ( !!(sla & I2C_READ) ) {
		if ( (rval = bb_i2c_start( fw, 1 )) < 0 ) {
			goto bail;
		}
		buf[0] = sla;
		if ( (rval = bb_i2c_write( fw, buf, 1 )) < 1 ) {
			if ( rval >= 0 ) {
				rval = -EIO;
			}
			goto bail;
		}
		rval = bb_i2c_read( fw, data, len );
	} else {
		rval = bb_i2c_write( fw, data, len );
	}

bail:
	/* attempt to stop */
	if ( (st = bb_i2c_stop( fw )) < 0 && rval >= 0 ) {
		rval = st;
	}
	return rval;
}

static int
bb_i2c_rw_reg(FWInfo *fw, uint8_t sla, uint8_t reg, int val)
{
	int rval;
	uint8_t buf[1];

	if ( val < 0 ) {
		sla |= I2C_READ;
	} else {
		buf[0] = val;
	}

	rval = bb_i2c_rw_a8( fw, sla, reg, buf, sizeof(buf) );
	if ( rval > 0 ) {
		rval =  (val < 0 ) ? buf[0] : 0;
	}
	return rval;
}


/*
 * RETURNS: read value, negative status on error;
 */
int
bb_i2c_read_reg(FWInfo *fw, uint8_t sla, uint8_t reg)
{
	return bb_i2c_rw_reg(fw, sla, reg, -1);
}

/*
 * RETURNS: 0 on success, negative status on error;
 */
int
bb_i2c_write_reg(FWInfo *fw, uint8_t sla, uint8_t reg, uint8_t val)
{
	return bb_i2c_rw_reg(fw, sla, reg, val);
}

int
__fw_has_buf(FWInfo *fw, size_t *psz, unsigned *pflg)
{
uint8_t  buf[4];
long     rval;
int      ret = BUF_SIZE_FAILED;
uint8_t  cmd = fw_get_cmd( FW_CMD_ADC_BUF ) | BITS_FW_CMD_MEMSIZE;
size_t   sz  = 0;
unsigned flg = 0;

	rval = fw_xfer( fw, cmd, 0, buf, sizeof(buf) );

	switch ( rval ) {
		case 3:
			flg = (uint8_t)buf[2];
			/* fall through */
		case 2: /* older fw version has no flags */
			sz = 512UL * ((size_t)((buf[1]<<8) | buf[0]) + 1);
			ret  = 0;
			break;
		case -ENOTSUP:
			sz = 0UL;
			ret  = BUF_SIZE_NOTSUP;
			break;
		case -ETIMEDOUT:
			fprintf(stderr, "Error: buf_get_size() -- timeout; command unsupported?\n");
			break;
		case -EINVAL:
			fprintf(stderr, "Error: buf_get_size() -- invalid arguments.\n");
			break;
		default:
			if ( rval < 0 ) {
				fprintf(stderr, "Error: buf_get_size() -- error occurred: %s\n", strerror(-rval));
			} else {
				fprintf(stderr, "Error: buf_get_size() -- unexpected frame size/status %ld\n", rval);
			}
			break;
	}
	
	if ( pflg ) {
		*pflg = flg;
	}
	if ( psz ) {
		*psz = sz;
	}

	return ret;
}

int
__fw_get_sampling_freq_mhz(FWInfo *fw)
{
uint8_t buf[1];
uint8_t cmd = fw_get_cmd( FW_CMD_ADC_BUF ) | BITS_FW_CMD_SMPLFREQ;
long    rval;
	if ( fw->apiVers < FW_API_VERSION_3 ) {
		return -ENOTSUP;
	}
	rval = fw_xfer( fw, cmd, 0, buf, sizeof(buf) );
	if ( 1 == rval ) {
		return buf[0];
	}
	return -EINVAL;
}

uint8_t
fw_get_board_version(FWInfo *fw)
{
	return fw->brdVers;
}

uint8_t
fw_get_api_version(FWInfo *fw)
{
	return fw->apiVers;
}


uint32_t
fw_get_version(FWInfo *fw)
{
	return fw->gitHash;
}
uint8_t
fw_spireg_cmd_read(unsigned ch)
{
	return 0x80 | (ch & 0x1f);
}

uint8_t
fw_spireg_cmd_write(unsigned ch)
{
	return 0x00 | (ch & 0x1f);
}

int
fw_reg_read(FWInfo *fw, uint32_t addr, uint8_t *buf, size_t len, unsigned flags)
{
	tbufvec tvec[1];
	rbufvec rvec[2];
	uint8_t pbuf[2];
	uint8_t status;
	uint8_t cmd      = fw_get_cmd( FW_CMD_REG_RD8 );
	int     st;

	if ( addr >= 256 || (addr + len) > 256 ) {
		return -EINVAL;
	}

	pbuf[0]     = (uint8_t)addr;
	pbuf[1]     = (uint8_t)(len - 1);

    tvec[0].buf = pbuf;
    tvec[0].len = sizeof(pbuf);

	rvec[0].buf = buf;
	rvec[0].len = len;
	rvec[1].buf = &status;
	rvec[1].len = 1;

	st = fw_xfer_vec( fw, cmd, tvec, sizeof(tvec)/sizeof(tvec[0]), rvec, sizeof(rvec)/sizeof(rvec[0]) );
	if ( st < 0 ) {
		return st;
	}
	return (len + 1 != st) || status ? -EIO : len;
}

int
fw_reg_write(FWInfo *fw, uint32_t addr, const uint8_t *buf, size_t len, unsigned flags)
{
	tbufvec tvec[2];
	rbufvec rvec[1];
	uint8_t byteAddr = addr;
	uint8_t status;
	uint8_t cmd      = fw_get_cmd( FW_CMD_REG_WR8 );
	int     st;

	if ( addr >= 256 || (addr + len) > 256 ) {
		return -EINVAL;
	}

    tvec[0].buf = &byteAddr;
    tvec[0].len = 1;
	tvec[1].buf = buf;
	tvec[1].len = len;

	rvec[0].buf = &status;
	rvec[0].len = 1;

	st = fw_xfer_vec( fw, cmd, tvec, sizeof(tvec)/sizeof(tvec[0]), rvec, sizeof(rvec)/sizeof(rvec[0]) );
	if ( st < 0 ) {
		return st;
	}
	return (1 != st ) || status ? -EIO : len;
}

int
fw_inv_cmd(FWInfo *fw)
{
	int st = fw_xfer( fw, BITS_FW_CMD_UNSUPPORTED, 0, 0, 0 );
	return (-ENOTSUP == st) ? 0 : st;
}

int
eepromGetSize(FWInfo *fw)
{
	return fw->eeprom ? at24EepromGetSize( fw->eeprom ) : -ENODEV;
}

int
eepromRead(FWInfo *fw, unsigned off, uint8_t *buf, size_t len)
{
	return fw->eeprom ? at24EepromRead( fw->eeprom, off, buf, len ) : -ENODEV;
}

int
eepromWrite(FWInfo *fw, unsigned off, uint8_t *buf, size_t len)
{
	return fw->eeprom ? at24EepromWrite( fw->eeprom, off, buf, len ) : -ENODEV;
}
