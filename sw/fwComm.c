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

#define BITS_FW_CMD_ACQ_MSK_SRC  7
#define BITS_FW_CMD_ACQ_SHF_SRC  0
#define BITS_FW_CMD_ACQ_SHF_EDG  3

#define BITS_FW_CMD_ACQ_IDX_MSK  0
#define BITS_FW_CMD_ACQ_IDX_SRC  (BITS_FW_CMD_ACQ_IDX_MSK + sizeof( uint8_t))
#define BITS_FW_CMD_ACQ_IDX_LVL  (BITS_FW_CMD_ACQ_IDX_SRC + sizeof( uint8_t))
#define BITS_FW_CMD_ACQ_IDX_NPT  (BITS_FW_CMD_ACQ_IDX_LVL + sizeof( int16_t))
#define BITS_FW_CMD_ACQ_IDX_AUT  (BITS_FW_CMD_ACQ_IDX_NPT + sizeof(uint16_t))
#define BITS_FW_CMD_ACQ_IDX_DCM  (BITS_FW_CMD_ACQ_IDX_AUT + sizeof(uint16_t))
#define BITS_FW_CMD_ACQ_IDX_SCL  (BITS_FW_CMD_ACQ_IDX_DCM + 3*sizeof(uint8_t))

#define BITS_FW_CMD_ACQ_IDX_LEN  (BITS_FW_CMD_ACQ_IDX_SCL + 4*sizeof(uint8_t))

#define BITS_FW_CMD_ACQ_DCM0_SHFT 20

struct FWInfo {
	int             fd;
	uint8_t         cmd;
	int             debug;
	int             ownFd;
	unsigned long   memSize;
	uint32_t        gitHash;
	uint8_t         brdVers;
	uint8_t         apiVers;
};

static int
fw_xfer_bb(FWInfo *fw, uint8_t subcmd, const uint8_t *tbuf, uint8_t *rbuf, size_t len);

static int
__bb_spi_cs(FWInfo *fw, uint8_t subcmd, int val);

#define BUF_SIZE_FAILED ((long)-1L)
static long
__buf_get_size(FWInfo *fw);

void
fw_set_debug(FWInfo *fw, int level)
{
	fw->debug = level;
}

static int
spi_get_subcmd(FWInfo *fw, SPIDev type)
{
unsigned long sup = (1<<SPI_NONE) | (1<<SPI_FLASH) | (1<<SPI_ADC);

	switch ( fw->brdVers ) {
		case 0:
			sup |= (1<<SPI_PGA) | (1<<SPI_FEG); break;
		case 1:
			sup |= (1<<SPI_VGA) | (1<<SPI_VGB); break;
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
		default:
			fprintf(stderr, "spi_get_subcmd() -- illegal switch case\n");
			abort();
	}
}

static int64_t
__fw_get_version(int fd)
{
uint8_t buf[sizeof(int64_t)];
int     got, i;
uint8_t cmd = fw_get_cmd( FW_CMD_VERSION );
int64_t rval;

	got = fifoXferFrame( fd, &cmd, 0, 0, buf, sizeof(buf) );
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
long    sz;
int64_t vers;

	if ( ! (rv = malloc(sizeof(*rv))) ) {
		perror("fw_open(): no memory");
		return 0;
	}

	rv->fd    = fd;
	rv->cmd   = BITS_FW_CMD_BB;
	rv->debug = 0;
	rv->ownFd = 0;
	sz = __buf_get_size( rv );
	if ( BUF_SIZE_FAILED == sz ) {
		fprintf(stderr, "Error: fw_open_fd unable to retrieve target memory size\n");
		rv->memSize = 0;
	} else {
		rv->memSize = (unsigned long)sz;
	}

	if ( ( vers = __fw_get_version( fd ) ) == (int64_t) -1 ) {
		fprintf(stderr, "Error: fw_open_fd unable to retrieve firmware version\n");
		free( rv );
		return 0;
	}

	rv->gitHash = ( vers & 0xffffffff );
	rv->apiVers = ( (vers >> 32) & 0xff );
    rv->brdVers = ( (vers >> 40) & 0xff );

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
uint8_t v = SPI_MASK | I2C_MASK;
int     rv;
uint8_t subcmd;

	rv = spi_get_subcmd( fw, type );

	if ( rv < 0 ) {
		return rv;
	}

	subcmd = (uint8_t) rv;

	v &= ~( (1<<CS_SHFT) | (1<<SCL_SHFT) | (1<<MOSI_SHFT) | (1<<HIZ_SHFT) );

	if ( cs ) {
		v |= (1 << CS_SHFT);
	}
	if ( clk ) {
		v |= (1 << SCL_SHFT);
	}
	if ( mosi ) {
		v |= (1 << MOSI_SHFT);
	}
	if ( ! hiz ) {
		v &= ~(1 << HIZ_SHFT);
	}
	rv = fw_xfer_bb(fw, subcmd, &v, &v, sizeof(v) );
	if ( rv >= 0 ) {
		rv = !! (v & (1<<MISO_SHFT));
	}
	return rv;
}

int
bb_spi_done(FWInfo *fw)
{
	return bb_spi_raw( fw, BITS_FW_CMD_BB_NONE, 1, 0, 0, 0 ); 
}

#define DEPTH    512 /* fifo depth */
#define MAXDEPTH 500

static int
fw_xfer_bb(FWInfo *fw, uint8_t subCmd, const uint8_t *tbuf, uint8_t *rbuf, size_t len)
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
__bb_spi_cs(FWInfo *fw, uint8_t subcmd, int val)
{
uint8_t bbbyte = ( ((val ? 1 : 0) << CS_SHFT) | (0 << SCLK_SHFT) ) | SPI_MASK;

	if ( fw_xfer_bb( fw, subcmd, &bbbyte, &bbbyte, 1 ) < 0 ) {
		fprintf(stderr, "Unable to set CS %d\n", !!val);
		return -1;
	}
	return 0;
}

int
bb_spi_cs(FWInfo *fw, SPIDev type, int val)
{
	int subcmd = spi_get_subcmd( fw, type );
	if ( subcmd < 0 ) {
		/* message already printed */
		return subcmd;
	}
	return __bb_spi_cs( fw, (uint8_t)subcmd, val );
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
uint8_t  subcmd;

	if ( ( i = spi_get_subcmd( fw, type ) ) < 0 ) {
		/* message already printed */
		return i;
	}
	subcmd = (uint8_t) i;

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


		if ( fw_xfer_bb( fw, subcmd, xbuf, xbuf, xlen*2*8 ) ) {
			fprintf(stderr, "bb_spi_xfer_nocs(): fw_xfer_bb failed\n");
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
uint8_t  subcmd;
int      got;

	if ( (got = spi_get_subcmd( fw, type )) < 0 ) {
		/* message already printed */
		return got;
	}

	subcmd = (uint8_t) got;

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
__buf_get_size(FWInfo *fw)
{
uint8_t buf[4];
long    rval;
uint8_t cmd = fw_get_cmd( FW_CMD_ADC_BUF ) | BITS_FW_CMD_MEMSIZE;

	rval = fifoXferFrame( fw->fd, &cmd, 0, 0, buf, sizeof(buf) );

	if ( 2 != rval ) {
		if ( -2 == rval ) {
			fprintf(stderr, "Error: buf_get_size() -- timeout; command unsupported?\n");
		} else {
			fprintf(stderr, "Error: buf_get_size() -- unexpected frame size %ld\n", rval);
		}
		return BUF_SIZE_FAILED;
	}
	rval = 512L * ((long)((buf[1]<<8) | buf[0]) + 1);
	return rval;
}

unsigned long
buf_get_size(FWInfo *fw)
{
	return fw->memSize;
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

	v[0].buf = h;
	v[0].len = sizeof(h);
	v[1].buf = buf;
	v[1].len = len;

	rcnt = (! hdr && 0 == len ? 0 : 2);

	uint8_t cmd = fw_get_cmd( FW_CMD_ADC_BUF );
	if ( 0 == len ) {
		cmd |= BITS_FW_CMD_ADCFLUSH;
	}
	rv = fifoXferFrameVec( fw->fd, &cmd, 0, 0, v, rcnt );
	if ( hdr ) {
		*hdr = (h[1]<<8) | h[0];
	}
	if ( rv >= 2 ) {
		rv -= 2;
	}
	return rv;
}

int
buf_read_flt(FWInfo *fw, uint16_t *hdr, float *buf, size_t nelms)
{
int      rv;
ssize_t  i;
int8_t  *i_p = (int8_t*)buf;


	rv = buf_read( fw, hdr, (uint8_t*)buf, nelms );
	if ( rv > 0 ) {
		for ( i = nelms - 1; i >= 0; i-- ) {
			buf[i] = (float)(i_p[i]);
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

/* Set new parameters and obtain previous parameters.
 * A new acquisition is started if any mask bit is set.
 *
 * Either 'set' or 'get' may be NULL with obvious semantics.
 */

int
acq_set_params(FWInfo *fw, AcqParams *set, AcqParams *get)
{
AcqParams p;
uint8_t   cmd = fw_get_cmd( FW_CMD_ACQ_PARMS );
uint8_t   buf[BITS_FW_CMD_ACQ_IDX_LEN];
uint16_t  v16;
uint32_t  v24;
uint32_t  v32;
int       got;

	p.mask = ACQ_PARAM_MSK_GET;

	if ( ! set ) {
		set = &p;
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
	}

	buf[BITS_FW_CMD_ACQ_IDX_MSK +  0]  = set->mask;
	buf[BITS_FW_CMD_ACQ_IDX_SRC +  0]  = (set->src & BITS_FW_CMD_ACQ_MSK_SRC)  << BITS_FW_CMD_ACQ_SHF_SRC;
	buf[BITS_FW_CMD_ACQ_IDX_SRC +  0] |= (set->rising ? 1 : 0)                 << BITS_FW_CMD_ACQ_SHF_EDG;

	buf[BITS_FW_CMD_ACQ_IDX_LVL +  0]  =  set->level       & 0xff;
	buf[BITS_FW_CMD_ACQ_IDX_LVL +  1]  = (set->level >> 8) & 0xff;

	if ( (set->mask & ACQ_PARAM_MSK_NPT) && (set->npts > fw->memSize - 1) ) {
		v16 = fw->memSize - 1;
		fprintf(stderr, "acq_set_params: WARNING npts > target memory size requested; clipping to %" PRId16 "\n", v16);
	} else {
		v16 = set->npts;
	}

	buf[BITS_FW_CMD_ACQ_IDX_NPT +  0]  =  v16       & 0xff;
	buf[BITS_FW_CMD_ACQ_IDX_NPT +  1]  = (v16 >> 8) & 0xff;

	if ( set->autoTimeoutMS > (1<<sizeof(uint16_t)*8) - 1 ) {
		v16 = (1<<sizeof(uint16_t)*8) - 1;
	} else {
		v16 = set->autoTimeoutMS;
	}

	buf[BITS_FW_CMD_ACQ_IDX_AUT +  0]  =  v16       & 0xff;
	buf[BITS_FW_CMD_ACQ_IDX_AUT +  1]  = (v16 >> 8) & 0xff;

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

	buf[BITS_FW_CMD_ACQ_IDX_DCM +  0]  =  v24        & 0xff;
	buf[BITS_FW_CMD_ACQ_IDX_DCM +  1]  = (v24 >>  8) & 0xff;
	buf[BITS_FW_CMD_ACQ_IDX_DCM +  2]  = (v24 >> 16) & 0xff;

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

	buf[BITS_FW_CMD_ACQ_IDX_SCL +  0]  =  v32        & 0xff;
	buf[BITS_FW_CMD_ACQ_IDX_SCL +  1]  = (v32 >>  8) & 0xff;
	buf[BITS_FW_CMD_ACQ_IDX_SCL +  2]  = (v32 >> 16) & 0xff;
	buf[BITS_FW_CMD_ACQ_IDX_SCL +  3]  = (v32 >> 24) & 0xff;


	got = fifoXferFrame( fw->fd, &cmd, buf, sizeof(buf), buf, sizeof(buf) );

	if ( got < 0 ) {
		fprintf(stderr, "Error: acq_set_params(); fifo transfer failed\n");
		return -1;
	}

	if ( got < BITS_FW_CMD_ACQ_IDX_LEN ) {
		fprintf(stderr, "Error: acq_set_params(); fifo transfer short?\n");
		return -1;
	}

	if ( ! get ) {
		return 0;
	}

	get->mask = ACQ_PARAM_MSK_ALL;

	switch ( (buf[BITS_FW_CMD_ACQ_IDX_SRC +  0] >> BITS_FW_CMD_ACQ_SHF_SRC) & BITS_FW_CMD_ACQ_MSK_SRC) {
		case 0:  get->src = CHA; break;
		case 1:  get->src = CHB; break;
		default: get->src = EXT; break;
	}

	get->rising  = !! ( (buf[BITS_FW_CMD_ACQ_IDX_SRC +  0] >> BITS_FW_CMD_ACQ_SHF_EDG) & 1 );

	get->level   = (int16_t)(    (((uint16_t)buf[BITS_FW_CMD_ACQ_IDX_LVL +  1]) << 8)
                               | (uint16_t)buf[BITS_FW_CMD_ACQ_IDX_LVL +  0] );

	get->npts           =   (    (((uint32_t)buf[BITS_FW_CMD_ACQ_IDX_NPT +  1]) << 8)
                               | (uint32_t)buf[BITS_FW_CMD_ACQ_IDX_NPT +  0] );

	get->autoTimeoutMS  =    (    (((uint32_t)buf[BITS_FW_CMD_ACQ_IDX_AUT +  1]) << 8)
                               | (uint32_t)buf[BITS_FW_CMD_ACQ_IDX_AUT +  0] );

	v32                 =    (    (((uint32_t)buf[BITS_FW_CMD_ACQ_IDX_DCM +  2]) << 16)
	                           | (((uint32_t)buf[BITS_FW_CMD_ACQ_IDX_DCM +  1]) <<  8)
                               | (uint32_t)buf[BITS_FW_CMD_ACQ_IDX_DCM +  0] );
    get->cic0Decimation = ((v32 >> BITS_FW_CMD_ACQ_DCM0_SHFT) & 0xf   ) + 1; /* zero-based */
    get->cic1Decimation = ((v32 >>                         0) & 0xffff) + 1; /* zero-based */

	v32                 =    (    (((uint32_t)buf[BITS_FW_CMD_ACQ_IDX_SCL +  3]) << 24)
	                           | (((uint32_t)buf[BITS_FW_CMD_ACQ_IDX_SCL +  2]) << 16)
	                           | (((uint32_t)buf[BITS_FW_CMD_ACQ_IDX_SCL +  1]) <<  8)
                               | (uint32_t)buf[BITS_FW_CMD_ACQ_IDX_SCL +  0] );

	get->cic0Shift      = ( v32 >> (20 + 7) ) & 0x1f;
	get->cic1Shift      = ( v32 >> (20    ) ) & 0x7f;
	get->scale          = ( v32 & ((1<<20) - 1)) << (32 - 18);
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
acq_set_level(FWInfo *fw, int16_t level)
{
AcqParams p;
	p.mask          = ACQ_PARAM_MSK_LVL;
	p.level         = level;
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
		p.rising  = !!rising;
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
