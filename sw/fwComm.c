#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>

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
#define BITS_FW_CMD_BB_FEG      (5<<4)
#define BITS_FW_CMD_BB_VGA      (6<<4)
#define BITS_FW_CMD_BB_VGB      (7<<4)
#define BITS_FW_CMD_ADCBUF      0x02
#define BITS_FW_CMD_ADCFLUSH    (1<<4)
#define BITS_FW_CMD_MEMSIZE     (2<<4)
#define BITS_FW_CMD_ACQPRM      0x03
#define BITS_FW_CMD_SPI         0x04

#define BITS_FW_CMD_REG         0x05
#define BITS_FW_CMD_REG_RD8     (0<<4)
#define BITS_FW_CMD_REG_WR8     (1<<4)

#define BITS_FW_CMD_ACQ_MSK_SRC  7
#define BITS_FW_CMD_ACQ_SHF_SRC  0
#define BITS_FW_CMD_ACQ_SHF_EDG  3

#define BITS_FW_CMD_ACQ_IDX_MSK     0
#define BITS_FW_CMD_ACQ_LEN_MSK_V1  sizeof(uint8_t)
#define BITS_FW_CMD_ACQ_LEN_MSK_V2  sizeof(uint32_t)
#define BITS_FW_CMD_ACQ_LEN_SRC     1
#define BITS_FW_CMD_ACQ_LEN_LVL     2
#define BITS_FW_CMD_ACQ_LEN_NPT_V1  2
#define BITS_FW_CMD_ACQ_LEN_NPT_V2  3
#define BITS_FW_CMD_ACQ_LEN_NSM_V1  0
#define BITS_FW_CMD_ACQ_LEN_NSM_V2  3
#define BITS_FW_CMD_ACQ_LEN_AUT     2
#define BITS_FW_CMD_ACQ_LEN_DCM     3
#define BITS_FW_CMD_ACQ_LEN_SCL     4
#define BITS_FW_CMD_ACQ_LEN_HYS     2

#define BITS_FW_CMD_ACQ_TOT_LEN_V1 15
#define BITS_FW_CMD_ACQ_TOT_LEN_V2 24

#define BITS_FW_CMD_ACQ_DCM0_SHFT 20

#define BITS_FW_CMD_UNSUPPORTED   0xff

struct FWInfo {
	int             fd;
	uint8_t         cmd;
	int             debug;
	int             ownFd;
	unsigned long   memSize;
	uint8_t         memFlags;
	uint32_t        gitHash;
	uint8_t         brdVers;
	uint8_t         apiVers;
	uint64_t        features;
	AcqParams       acqParams;
};

static int
fw_xfer_bb(FWInfo *fw, uint8_t subcmd, const uint8_t *tbuf, uint8_t *rbuf, size_t len);

static int
__bb_spi_cs(FWInfo *fw, SPIMode mode, uint8_t subcmd, uint8_t lastval);

#define BUF_SIZE_FAILED ((long)-1L)
static long
__buf_get_size(FWInfo *fw, unsigned long *psz, uint8_t *pflg);

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
			sup |= (1<<SPI_PGA);                break;
		default:
			fprintf(stderr, "spi_get_subcmd(): unsupported board/hw version %i\n", fw->brdVers);
			return -1;
	}
	if ( ! ((1<<type) & sup) ) {
		fprintf(stderr, "spi_get_subcmd(): SPI device %i not supported on this board/hw\n", type);
		return -1;
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
		return (int64_t)-1;
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
int     fd = fifoOpen( devn, speed );
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
FWInfo *rv;
int64_t vers;
int     st;

	if ( ! (rv = malloc(sizeof(*rv))) ) {
		perror("fw_open(): no memory");
		return 0;
	}

	rv->fd       = fd;
	rv->cmd      = BITS_FW_CMD_BB;
	rv->debug    = 0;
	rv->ownFd    = 0;
	rv->features = 0;
	if ( BUF_SIZE_FAILED == __buf_get_size( rv, &rv->memSize, &rv->memFlags ) ) {
		fprintf(stderr, "Error: fw_open_fd unable to retrieve target memory size\n");
	}

	if ( ( vers = __fw_get_version( rv ) ) == (int64_t) -1 ) {
		fprintf(stderr, "Error: fw_open_fd unable to retrieve firmware version\n");
		free( rv );
		return 0;
	}

	rv->gitHash = ( vers & 0xffffffff );
	rv->apiVers = ( (vers >> 32) & 0xff );
    rv->brdVers = ( (vers >> 40) & 0xff );

	/* avoid a timeout on old fw */
	if ( rv->apiVers >= FW_API_VERSION_1 &&  0 == fw_xfer( rv, BITS_FW_CMD_SPI, 0, 0, 0 ) ) {
		rv->features |= FW_FEATURE_SPI_CONTROLLER;
	}

	/* abiVers etc. valid after this point */

	if ( (st = acq_set_params( rv, NULL, &rv->acqParams )) ) {
		fprintf(stderr, "Error %d: unable to read initial acquisition parameters\n", st);
	}

	return rv;
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
		st = FW_CMD_ERR_NOTSUP;
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
		st = FW_CMD_ERR_NOTSUP;
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

	if ( fw_xfer_bb( fw, BITS_FW_CMD_BB_I2C, &bbbyte, &bbbyte, 1 ) < 0 ) {
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
__bb_spi_cs(FWInfo *fw, SPIMode mode, uint8_t subcmd, uint8_t lastval)
{
uint8_t buf[2];
int     csWasHi = !! ( lastval & (1 << CS_SHFT) );

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

	if ( fw_xfer_bb( fw, subcmd, buf, buf, sizeof(buf) ) < 0 ) {
		fprintf(stderr, "Unable to set CS to %d\n", !csWasHi);
		return -1;
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

	if ( ( el = spi_get_subcmd( fw, type ) ) < 0 ) {
		/* message already printed */
		return -1;
	}
	subcmd = (uint8_t) el;


	/* assert CS */
	if ( (el = __bb_spi_cs( fw, mode, subcmd, SPI_MASK | (1 << CS_SHFT) )) < 0 ) {
		return -1;
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

			if ( fw_xfer_bb( fw, subcmd, buf, buf, stretchlen ) ) {
				fprintf(stderr, "bb_spi_xfer_vec(): fw_xfer_bb failed\n");
				return -1;
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
	if ( __bb_spi_cs( fw, mode, subcmd, last ) < 0 ) {
		return -1;
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
	if ( fw_xfer_bb( fw, BITS_FW_CMD_BB_I2C, xbuf, rbuf, sizeof(xbuf)) ) {
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

static long
__buf_get_size(FWInfo *fw, unsigned long *psz, uint8_t *pflg)
{
uint8_t buf[4];
long    rval;
long    ret = BUF_SIZE_FAILED;
uint8_t cmd = fw_get_cmd( FW_CMD_ADC_BUF ) | BITS_FW_CMD_MEMSIZE;

    *psz  = 0;
    *pflg = 0;

	rval = fw_xfer( fw, cmd, 0, buf, sizeof(buf) );

	switch ( rval ) {
		case 3:
			*pflg = buf[2];
			/* fall through */
		case 2: /* older fw version has no flags */
			*psz = 512UL * ((unsigned long)((buf[1]<<8) | buf[0]) + 1);
			ret  = 0;
			break;
		case -2:
			fprintf(stderr, "Error: buf_get_size() -- timeout; command unsupported?\n");
			break;
		default:
			fprintf(stderr, "Error: buf_get_size() -- unexpected frame size %ld\n", rval);
			break;
	}

	return ret;
}

unsigned long
buf_get_size(FWInfo *fw)
{
	return fw->memSize;
}

uint8_t
buf_get_flags(FWInfo *fw)
{
	return fw->memFlags;
}


int
buf_flush(FWInfo *fw)
{
	return buf_read(fw, 0, 0, 0);
}

int
buf_read(FWInfo *fw, uint16_t *hdr, uint8_t *buf, size_t len)
{
uint8_t h[2];
rbufvec v[2];
size_t  rcnt;
int     rv;
int     i;
const union {
	uint8_t  b[2];
	uint16_t s;
} isLE = { s : 1 };

	v[0].buf = h;
	v[0].len = sizeof(h);
	v[1].buf = buf;
	v[1].len = len;

	rcnt = (! hdr && 0 == len ? 0 : 2);

	uint8_t cmd = fw_get_cmd( FW_CMD_ADC_BUF );
	if ( 0 == len ) {
		cmd |= BITS_FW_CMD_ADCFLUSH;
	}
	rv = fw_xfer_vec( fw, cmd, 0, 0, v, rcnt );
	if ( hdr ) {
		*hdr = (h[1]<<8) | h[0];
	}
	if ( ! isLE.b[0] && !! (buf_get_flags(fw) & FW_BUF_FLG_16B) ) {
		for ( i = 0; i < (len & ~1); i+=2 ) {
			uint8_t tmp = buf[i];
			buf[i  ] = buf[i+1];
			buf[i+1] = tmp;
		}
	}
	if ( rv >= 2 ) {
		rv -= 2;
	}
	return rv;
}

int
buf_read_flt(FWInfo *fw, uint16_t *hdr, float *buf, size_t nelms)
{
int       rv;
ssize_t   i;
int8_t   *i8_p  = (int8_t*)buf;
int16_t  *i16_p = (int16_t*)buf;
int       elsz  = ( (buf_get_flags( fw ) & FW_BUF_FLG_16B) ? 2 : 1 );


	rv = buf_read( fw, hdr, (uint8_t*)buf, nelms*elsz );
	if ( rv > 0 ) {
		if ( 2 == elsz ) {
			for ( i = nelms - 1; i >= 0; i-- ) {
				buf[i] = (float)(i16_p[i]);
			}
		} else {
			for ( i = nelms - 1; i >= 0; i-- ) {
				buf[i] = (float)(i8_p[i]);
			}
		}
	}
	return rv;
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

static void
putBuf(uint8_t **bufp, uint32_t val, int len)
{
int i;

	for ( i = 0; i < len; i++ ) {
		**bufp  = (val & 0xff);
		val   >>= 8;
		(*bufp)++;
	}
}

static uint32_t
getBuf(uint8_t **bufp, int len)
{
int      i;
uint32_t rv = 0;

	for ( i = len - 1; i >= 0; i-- ) {
		rv = (rv << 8 ) | (*bufp)[ i ];
	}
	(*bufp) += len;

	return rv;
}


/* Set new parameters and obtain previous parameters.
 * A new acquisition is started if any mask bit is set.
 *
 * Either 'set' or 'get' may be NULL with obvious semantics.
 */

int
acq_set_params(FWInfo *fw, AcqParams *set, AcqParams *get)
{
uint8_t   cmd = fw_get_cmd( FW_CMD_ACQ_PARMS );
uint8_t   buf[BITS_FW_CMD_ACQ_TOT_LEN_V2];
uint8_t  *bufp;
uint8_t   v8;
uint32_t  v24;
uint32_t  v32;
uint32_t  nsamples;
int       got;
int       len;

	if ( ! fw ) {
		return FW_CMD_ERR_INVALID;
	}

	if ( ! set || (ACQ_PARAM_MSK_GET == set->mask) ) {
		if ( get == &fw->acqParams ) {
			/* read the cache !! */
			if ( ! set ) {
				set = get;
				set->mask = ACQ_PARAM_MSK_GET;
			}
		} else {
			if ( get ) {
				*get = fw->acqParams;
			}
			return 0;
		}
	}

	/* parameter validation and updating of cache */
	if ( ( set->mask & ACQ_PARAM_MSK_SRC ) ) {
		fw->acqParams.src    = set->src;
	}

	if ( ( set->mask & ACQ_PARAM_MSK_EDG ) ) {
		fw->acqParams.rising = set->rising;
	}

	if ( ( set->mask & ACQ_PARAM_MSK_DCM ) ) {
        if ( 1 >= set->cic0Decimation ) {
printf("Forcing cic1 decimation to 1");
			set->cic1Decimation = 1;
        }
printf("Setting dcim %d x %d\n", set->cic0Decimation, set->cic1Decimation);
		/* If they change the decimation but not explicitly the scale
		 * then adjust the scale automatically
		 */
		if (  ! ( set->mask & ACQ_PARAM_MSK_SCL ) ) {
			set->mask  |= ACQ_PARAM_MSK_SCL;
			set->cic0Shift = 0;
			set->cic1Shift = 0;
			set->scale     = acq_default_cic1Scale( set->cic1Decimation );
printf("Default scale %d\n", set->scale);
		}
		fw->acqParams.cic0Decimation = set->cic0Decimation;
		fw->acqParams.cic1Decimation = set->cic1Decimation;
	}

	nsamples = fw->acqParams.nsamples;

	if ( ( set->mask & ACQ_PARAM_MSK_NSM ) ) {
		if ( fw->apiVers < FW_API_VERSION_2 ) {
			return FW_CMD_ERR_NOTSUP;
		}
		if ( set->nsamples > fw->memSize ) {
			set->nsamples = fw->memSize;
printf("Forcing nsamples to %ld\n", fw->memSize);
		}
		if ( set->nsamples < 1 ) {
			set->nsamples = 1;
printf("Forcing nsamples to 1\n");
		}
		nsamples = set->nsamples;
        fw->acqParams.nsamples = set->nsamples;
	}

    bufp = buf + BITS_FW_CMD_ACQ_IDX_MSK;
	len  = fw->apiVers >= FW_API_VERSION_2 ? BITS_FW_CMD_ACQ_LEN_MSK_V2 : BITS_FW_CMD_ACQ_LEN_MSK_V1;
    putBuf( &bufp, set->mask, len );

	v8  = (set->src & BITS_FW_CMD_ACQ_MSK_SRC)  << BITS_FW_CMD_ACQ_SHF_SRC;
	v8 |= (set->rising ? 1 : 0)                 << BITS_FW_CMD_ACQ_SHF_EDG;
    putBuf( &bufp, v8, BITS_FW_CMD_ACQ_LEN_SRC );

	if ( ( set->mask & ACQ_PARAM_MSK_LVL ) ) {
		fw->acqParams.level      = set->level;
        fw->acqParams.hysteresis = set->hysteresis;
	}

	putBuf( &bufp, set->level, BITS_FW_CMD_ACQ_LEN_LVL );

	if ( ( set->mask & ACQ_PARAM_MSK_NPT ) ) {
		if ( (set->npts >= nsamples ) ) {
			set->npts = nsamples - 1;
			fprintf(stderr, "acq_set_params: WARNING npts >= nsamples requested; clipping to %" PRId32 "\n", set->npts);
		}
		fw->acqParams.npts = set->npts;
	}

	len  = fw->apiVers >= FW_API_VERSION_2 ? BITS_FW_CMD_ACQ_LEN_NPT_V2 : BITS_FW_CMD_ACQ_LEN_NPT_V1;
	putBuf( &bufp, set->npts, len );

	/* this implicitly does nothing for V1 */
	len  = fw->apiVers >= FW_API_VERSION_2 ? BITS_FW_CMD_ACQ_LEN_NSM_V2 : BITS_FW_CMD_ACQ_LEN_NSM_V1;
	/* firmware uses nsamples - 1 */
	putBuf( &bufp, nsamples - 1, len );

	if ( ( set->mask & ACQ_PARAM_MSK_AUT ) ) {
		if ( set->autoTimeoutMS > (1<<sizeof(uint16_t)*8) - 1 ) {
			set->autoTimeoutMS = (1<<sizeof(uint16_t)*8) - 1;
		}
		fw->acqParams.autoTimeoutMS = set->autoTimeoutMS;
	}

	putBuf( &bufp, set->autoTimeoutMS, BITS_FW_CMD_ACQ_LEN_AUT );

	if ( set->cic0Decimation > 16 ) {
		v24 = 15;
	} else if ( 0 == set->cic0Decimation ) {
		v24 = 1 - 1;
	} else {
		v24 = set->cic0Decimation - 1;
	}
    v24 <<= BITS_FW_CMD_ACQ_DCM0_SHFT;

	if ( 0 != v24 ) {
		if ( set->cic1Decimation > (1<<16) ) {
			v24 |= (1<<16) - 1;
		} else if ( 0 == set->cic1Decimation ) {
			v24 |= 1 - 1;
		} else {
			v24 |= set->cic1Decimation - 1;
		}
	}

	putBuf( &bufp, v24, BITS_FW_CMD_ACQ_LEN_DCM );

	v32 = set->cic0Shift;
    if ( v32 > 15 ) {
		v32 = 15;
	}
	v32 <<= 7;
	if ( set->cic1Shift > 16*4 - 1 ) {
		v32 |= 16*4 - 1;
	} else {
		v32 |= set->cic1Shift;
	}
	v32 <<= 20;

	v32 |= ( (set->scale >> (32 - 18)) & ( (1<<18) - 1 ) );

	if ( ( set->mask & ACQ_PARAM_MSK_SCL ) ) {
			fw->acqParams.cic0Shift = set->cic0Shift;
			fw->acqParams.cic1Shift = set->cic1Shift;
			fw->acqParams.scale     = set->scale;
	}

	putBuf( &bufp, v32, BITS_FW_CMD_ACQ_LEN_SCL );

	if ( fw->apiVers >= FW_API_VERSION_2 ) {
		putBuf( &bufp, set->hysteresis, BITS_FW_CMD_ACQ_LEN_HYS );
	}

	got = fw_xfer( fw, cmd, buf, buf, sizeof(buf) );

	if ( got < 0 ) {
		fprintf(stderr, "Error: acq_set_params(); fifo transfer failed\n");
		return -1;
	}

	len  = fw->apiVers >= FW_API_VERSION_2 ? BITS_FW_CMD_ACQ_TOT_LEN_V2 : BITS_FW_CMD_ACQ_TOT_LEN_V1;

	if ( got < len ) {
		fprintf(stderr, "Error: acq_set_params(); fifo transfer short\n");
		return -1;
	}

	if ( ! get ) {
		return 0;
	}

	get->mask = ACQ_PARAM_MSK_ALL;

	len  = fw->apiVers >= FW_API_VERSION_2 ? BITS_FW_CMD_ACQ_LEN_MSK_V2 : BITS_FW_CMD_ACQ_LEN_MSK_V1;
    bufp = buf + BITS_FW_CMD_ACQ_IDX_MSK + len;
    v8   = getBuf( &bufp, BITS_FW_CMD_ACQ_LEN_SRC );

	switch ( (v8 >> BITS_FW_CMD_ACQ_SHF_SRC) & BITS_FW_CMD_ACQ_MSK_SRC) {
		case 0:  get->src = CHA; break;
		case 1:  get->src = CHB; break;
		default: get->src = EXT; break;
	}

	get->rising  = !! ( (v8 >> BITS_FW_CMD_ACQ_SHF_EDG) & 1 );

	get->level          = getBuf( &bufp, BITS_FW_CMD_ACQ_LEN_LVL );

	len  = fw->apiVers >= FW_API_VERSION_2 ? BITS_FW_CMD_ACQ_LEN_NPT_V2 : BITS_FW_CMD_ACQ_LEN_NPT_V1;
	get->npts           = getBuf( &bufp, len );

	if ( fw->apiVers < FW_API_VERSION_2 ) {
		get->nsamples   = fw->memSize;
	} else {
		/* firmware uses nsamples - 1 */
		get->nsamples   = getBuf( &bufp, BITS_FW_CMD_ACQ_LEN_NSM_V2 ) + 1;
	}

	get->autoTimeoutMS  = getBuf( &bufp, BITS_FW_CMD_ACQ_LEN_AUT );

	v32                 = getBuf( &bufp, BITS_FW_CMD_ACQ_LEN_DCM );

    get->cic0Decimation = ((v32 >> BITS_FW_CMD_ACQ_DCM0_SHFT) & 0xf   ) + 1; /* zero-based */
    get->cic1Decimation = ((v32 >>                         0) & 0xffff) + 1; /* zero-based */

	v32                 = getBuf( &bufp, BITS_FW_CMD_ACQ_LEN_SCL );

	get->cic0Shift      = ( v32 >> (20 + 7) ) & 0x1f;
	get->cic1Shift      = ( v32 >> (20    ) ) & 0x7f;
	get->scale          = ( v32 & ((1<<20) - 1)) << (32 - 18);

	if ( fw->apiVers >= FW_API_VERSION_2 ) {
		get->hysteresis     = getBuf( &bufp, BITS_FW_CMD_ACQ_LEN_HYS );
	} else {
		get->hysteresis     = 0;
	}
	return 0;
}

/*
 * Helpers
 */
int
acq_manual(FWInfo *fw)
{
	return acq_set_autoTimeoutMs(fw, 0);
}

int
acq_set_level(FWInfo *fw, int16_t level, uint16_t hyst)
{
AcqParams p;
	p.mask          = ACQ_PARAM_MSK_LVL;
	p.level         = level;
    p.hysteresis    = hyst;
	return acq_set_params( fw, &p, 0 );
}

int
acq_set_npts(FWInfo *fw, uint32_t npts)
{
AcqParams p;
	p.mask          = ACQ_PARAM_MSK_NPT;
	p.npts          = npts;
	return acq_set_params( fw, &p, 0 );
}

int
acq_set_nsamples(FWInfo *fw, uint32_t nsamples)
{
AcqParams p;

	if ( fw->apiVers < FW_API_VERSION_2 ) {
		return FW_CMD_ERR_NOTSUP;
	}
	p.mask          = ACQ_PARAM_MSK_NSM;
	p.nsamples      = nsamples;
	return acq_set_params( fw, &p, 0 );
}


#define CIC1_SHF_STRIDE  8
#define CIC1_STAGES      4
#define STRIDE_STAGS_RAT 2

int32_t
acq_default_cic1Scale(uint32_t cic1Decimation)
{
uint32_t nbits;
/* details based on the ration of shifter stride to number of CIC
 * stages being an integer number
 */
uint32_t shift;
double   scale;

	if ( cic1Decimation < 2 ) {
		shift = 0;
        nbits = 0;
	} else {
        nbits = (uint32_t)floor( log2( (double)(cic1Decimation - 1) ) ) + 1;
        /* implicit 'floor' in integer operation */
		shift = (nbits - 1) / STRIDE_STAGS_RAT;
	}
	/* Correct the CIC1 gain */

	scale = 1./pow((double)cic1Decimation, (double)CIC1_STAGES);

	/* Adjust for built-in shifter operation */
	scale /= exp2(-(double)(shift * CIC1_SHF_STRIDE));
	return (int32_t)floor( scale * (double)ACQ_SCALE_ONE );
}

int
acq_set_decimation(FWInfo *fw, uint8_t cic0Decimation, uint32_t cic1Decimation)
{
AcqParams p;
	p.mask           = ACQ_PARAM_MSK_DCM;
	p.cic0Decimation = cic0Decimation;
	p.cic1Decimation = cic1Decimation;
	return acq_set_params( fw, &p, 0 );
}

int
acq_set_scale(FWInfo *fw, uint8_t cic0RShift, uint8_t cic1RShift, int32_t scale)
{
AcqParams p;
	p.mask           = ACQ_PARAM_MSK_SCL;
	p.cic0Shift      = cic0RShift;
	p.cic1Shift      = cic1RShift;
	p.scale          = scale;
	return acq_set_params( fw, &p, 0 );
}


int
acq_set_source(FWInfo *fw, TriggerSource src, int rising)
{
AcqParams p;
	p.mask          = ACQ_PARAM_MSK_SRC;
	p.src           = src;
	if ( rising ) {
		p.mask   |= ACQ_PARAM_MSK_EDG;
		p.rising  = rising > 0 ? 1 : 0;
	}
	return acq_set_params( fw, &p, 0 );
}

int
acq_set_autoTimeoutMs(FWInfo *fw, uint32_t timeout)
{
AcqParams p;
	p.mask          = ACQ_PARAM_MSK_AUT;
	p.autoTimeoutMS = timeout;
	return acq_set_params( fw, &p, 0 );
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
		return FW_CMD_ERR_INVALID;
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
	return (len + 1 != st) || status ? FW_CMD_ERR : len;
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
		return FW_CMD_ERR_INVALID;
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
	return (1 != st ) || status ? FW_CMD_ERR : len;
}

int
fw_inv_cmd(FWInfo *fw)
{
	int st = fw_xfer( fw, BITS_FW_CMD_UNSUPPORTED, 0, 0, 0 );
	return (FW_CMD_ERR_NOTSUP == st) ? 0 : st;
}
