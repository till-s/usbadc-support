#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>

#undef  SLOW_ALGO

#define CS_SHFT   0
#define SCLK_SHFT 1
#define MOSI_SHFT 2
#define MISO_SHFT 3

#define MODE_SHFT 6

#define SPI_MASK  (0xf0 | (1<<MODE_SHFT))

#define I2C_MASK  (0xC1 & ~(1<<MODE_SHFT))

#define SDA_SHFT  4
#define SCL_SHFT  5
#define I2C_NAK   1

#define I2C_READ 1

static int bb_debug = 0;

int bb_open(const char *devn, unsigned speed)
{
int                rval = -1;
int                fd   = -1;
char               msg[256];
struct termios     atts;

	if ( (fd = open(devn, O_RDWR)) < 0 ) {
		snprintf(msg, sizeof(msg), "unable open device '%s'", devn);
		perror(msg);
		goto bail;
	}

	if ( tcgetattr( fd, &atts ) ) {
		perror( "tcgetattr failed" );
		goto bail;
	}

	cfmakeraw( &atts );
	if ( cfsetspeed( &atts, speed ) ) {
		perror( "cfsetspeed failed" );
		goto bail;
	}

	if ( tcsetattr( fd, TCSANOW, &atts ) ) {
		perror( "tcgetattr failed" );
		goto bail;
	}

	rval = fd;
	fd   = -1;	if ( fd >= 0 ) {
		close( fd );
	}


bail:
	if ( fd >= 0 ) {
		close( fd );
	}
	return rval;
}

void
bb_close(int fd)
{
uint8_t v = SPI_MASK | I2C_MASK;
	write( fd, &v, sizeof(v) );
	close( fd );
}

#define DEPTH    512 /* fifo depth */
#define MAXDEPTH 500

int
bb_xfer(int fd, const uint8_t *tbuf, uint8_t *rbuf, size_t len)
{
const uint8_t      *twrk = tbuf;
uint8_t            *rwrk = rbuf;
int                 rval = -1;
size_t              i, rlen, wlen, rtodo, wtodo;
uint8_t             dummy [MAXDEPTH];
uint8_t             zerbuf[MAXDEPTH];
int                 flgs;
fd_set              rfds, wfds;

	wtodo = rtodo = len;

	if ( -1 == (flgs = fcntl( fd, F_GETFL )) ) {
		perror("fcntl(F_GETFL) failed");
		return -1;
	}

	if ( -1 == fcntl( fd, F_SETFL, (flgs | O_NONBLOCK) ) ) {
		perror("fcntl(F_SETFL,O_NONBLOCK) failed");
		return -1;
	}

	if ( ! tbuf ) {
		memset(zerbuf, 0, len > sizeof(zerbuf) ? sizeof(zerbuf) : len);
	}

	
	while ( (rtodo > 0) || (wtodo > 0) ) {

	    FD_ZERO( &rfds );
		FD_ZERO( &wfds );

		wlen = wtodo > MAXDEPTH ? MAXDEPTH : wtodo;
		rlen = rtodo > MAXDEPTH ? MAXDEPTH : rtodo;

		if ( ! tbuf ) {
			twrk = zerbuf;
		}

		if ( ! rbuf ) {
			rwrk = dummy;
		}

		if ( rlen > 0 ) {
			FD_SET( fd, &rfds );
		}
		if ( wlen > 0 ) {
			FD_SET( fd, &wfds );
		}

		i = pselect( fd + 1, &rfds, &wfds, 0, 0, 0 );

		if ( i <= 0 ) {
			if ( 0 == i ) {
				fprintf(stderr, "Hmm - select returned 0\n");
				sleep(1);
				continue;
			}
			perror("select failure");
			goto bail;
		}

		if ( FD_ISSET( fd, &wfds ) ) {
			if ( (i = write(fd, twrk, wlen)) <= 0 ) {
				perror("Writing to FIFO failed");
				goto bail;
			}
			if ( bb_debug > 1 ) {
				int k;
				printf("bb_xfer - wrote:\n=>");
				for ( k = 0; k < i; k++ ) {
					printf("0x%02x ", twrk[k]);
					if ( (k&0xf) == 0xf ) {
						printf("\n=>");
					}
				}
				printf("\n");
			}
			twrk  += i;
			wtodo -= i;
		}

		if ( FD_ISSET( fd, &rfds ) ) {
			if ( (i = read(fd, rwrk, rlen)) <= 0 ) {
				perror("Reading from FIFO failed");
				goto bail;
			}
			if ( bb_debug > 1 ) {
				int k;
				printf("bb_xfer - read:\n<=");
				for ( k = 0; k < i; k++ ) {
					printf("0x%02x ", rwrk[k]);
					if ( (k&0xf) == 0xf ) {
						printf("\n<=");
					}
				}
				printf("\n");
			}
			rwrk  += i;
			rtodo -= i;
		}
	}

	rval = 0;

bail:
	fcntl( fd, F_SETFL, flgs );
	return rval;
}

static void pr_i2c_dbg(uint8_t tbyte, uint8_t rbyte)
{
	printf("Writing %02x - got %02x (%d %d - %d %d)\n", tbyte, rbyte,
			!!(tbyte & (1<<SCL_SHFT)) , !!(tbyte & (1<<SDA_SHFT)),
			!!(rbyte & (1<<SCL_SHFT)) , !!(rbyte & (1<<SDA_SHFT)));
}

int
bb_i2c_set(int fd, int scl, int sda)
{
uint8_t bbbyte =  ((scl ? 1 : 0) << SCL_SHFT) | ((sda ? 1 : 0) << SDA_SHFT) | I2C_MASK;
uint8_t x = bbbyte;

	if ( bb_xfer( fd, &bbbyte, &bbbyte, 1 ) < 0 ) {
		fprintf(stderr, "bb_i2c_set: unable to set levels\n");
		return -1;
	}
	if ( bb_debug ) {
		pr_i2c_dbg(x, bbbyte);
	}
	return bbbyte;
}

int
bb_i2c_start(int fd, int restart)
{

	if ( bb_debug ) {
		printf("bb_i2c_start:\n");
	}
	if ( restart ) {
		if ( bb_i2c_set(fd, 0, 1 ) < 0 ) {
			return -1;
		}
		if ( bb_i2c_set(fd, 1, 1) < 0 ) {
			return -1;
		}
	}

	if ( bb_i2c_set(fd, 1, 0) < 0 ) {
		return -1;
	}

	if ( bb_i2c_set(fd, 0, 0) < 0 ) {
		return -1;
	}

	return 0;
}

int
bb_i2c_stop(int fd)
{
	if ( bb_debug ) {
		printf("bb_i2c_stop:\n");
	}
	if ( bb_i2c_set(fd, 1, 0) < 0 ) {
		return -1;
	}

	if ( bb_i2c_set(fd, 1, 1) < 0 ) {
		return -1;
	}
	return 0;
}

int
bb_i2c_bit(int fd, int val)
{
int got;

	if ( bb_debug ) {
		printf("bb_i2c_bit:\n");
	}
	if ( bb_i2c_set(fd, 0, val) < 0 ) {
		return -1;
	}
	if ( (got = bb_i2c_set(fd, 1, val)) < 0 ) {
		return -1;
	}
	if ( bb_i2c_set(fd, 0, val) < 0 ) {
		return -1;
	}
	return !! (got & (1<<SDA_SHFT));
}

int
bb_spi_cs(int fd, int val)
{
uint8_t bbbyte = ( ((val ? 1 : 0) << CS_SHFT) | (0 << SCLK_SHFT) ) | SPI_MASK;

	if ( bb_xfer( fd, &bbbyte, 0, 1 ) < 0 ) {
		fprintf(stderr, "Unable to set CS %d\n", !!val);
		return -1;
	}
	return 0;
}

int
bb_spi_bit(int fd, int val)
{
uint8_t bbbyte = ( ((val ? 1 : 0) << MOSI_SHFT) | (0 << SCLK_SHFT) | (0 << CS_SHFT) ) | SPI_MASK ;

	if ( bb_xfer( fd, &bbbyte, 0, 1 ) < 0 ) {
		fprintf(stderr, "Unable to send bit (SCLK low)\n");
		return -1;
	}

    /* raise SCLK */
	bbbyte |= (1<<SCLK_SHFT);

	if ( bb_xfer( fd, &bbbyte, &bbbyte, 1 ) < 0 ) {
		fprintf(stderr, "Unable to send bit (SCLK low)\n");
		return -1;
	}

	return !!(bbbyte & (1 << MISO_SHFT));
}

#define MSBIT 0x80

#ifdef SLOW_ALGO
int
bb_spi_xfer_nocs(int fd, const uint8_t *tbuf, uint8_t *rbuf, size_t len)
{
size_t  i, j;
int     got;
uint8_t bbuf;

	for ( i = 0; i < len; i++ ) {
		bbuf = tbuf ? tbuf[i] : 0;
		for ( j = 0; j < 8*sizeof(tbuf[0]); j++ ) {
			got = bb_spi_bit(fd, !!(bbuf & MSBIT));
			if ( got < 0 ) {
				fprintf(stderr, "Sending bit #%d of octet %d failed\n", (unsigned)j, (unsigned)i);
				return -1;
			}
			bbuf = ( (bbuf << 1) | (got ? 1 : 0) ) & 0xff;
		}
		if ( rbuf ) {
			rbuf[i] = bbuf;
		}
	}

	return len;
}

#else
#define BUF_BRK 1024

int
bb_spi_xfer_nocs(int fd, const uint8_t *tbuf, uint8_t *rbuf, size_t len)
{
uint8_t *xbuf = malloc(BUF_BRK*8*2);
uint8_t *p;
int      rval = -1;
size_t   work = len;
size_t   xlen;
int      i,j;
uint8_t  v;

	if ( ! xbuf ) {
		perror("bb_spi_xfer_nocs(): unable to allocate buffer memory");
	}

	while ( work > 0 ) {
		xlen = work > BUF_BRK ? BUF_BRK : work;
		p    = xbuf;

		for ( i = 0; i < xlen; i++ ) {
			v = tbuf ? tbuf[i] : 0;
			for ( j = 0; j < 2*8; j += 2 ) {
				p[j + 0] = ( (((v & 0x80) ? 1 : 0) << MOSI_SHFT) | (0 << SCLK_SHFT) | (0 << CS_SHFT) ) | SPI_MASK ;
				p[j + 1] = p[j+0] | (1 << SCLK_SHFT);
				v      <<= 1;
			}
			p += j;
		}

		if ( bb_xfer( fd, xbuf, xbuf, xlen*2*8 ) ) {
			fprintf(stderr, "bb_spi_xfer_nocs(): bb_xfer failed\n");
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
#endif

int
bb_spi_xfer(int fd, const uint8_t *tbuf, uint8_t *rbuf, size_t len)
{
int got;

	if ( bb_spi_cs(fd, 0) ) {
		return -1;
	}

	got = bb_spi_xfer_nocs(fd, tbuf, rbuf, len);

	if ( bb_spi_cs(fd, 1) ) {
		return -1;
	}

	return got;
}

/* XFER 9 bits (lsbit is ACK) */
int
bb_i2c_xfer(int fd, uint16_t val)
{
int i;
#ifdef SLOW_ALGO
int got;
	for ( i = 0; i < 9; i++ ) {
		if ( (got = bb_i2c_bit(fd, (val & (1<<8)) )) < 0 ) {
			fprintf(stderr, "bb_i2c_xfer failed (bit %i)\n", i);
			return -1;
		}
		val = (val << 1) | got;
	}
#else
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
	if ( bb_xfer( fd, xbuf, rbuf, sizeof(xbuf)) ) {
		fprintf(stderr, "bb_i2c_xfer failed\n");
		return -1;
	}
	val = 0;
	for ( i = 0; i < 3*9; i+= 3 ) {
		val = (val<<1) | ( ( rbuf[i+2] & ( 1 << SDA_SHFT ) ) ? 1 : 0 );
	}
	if ( bb_debug ) {
		for ( i = 0; i < 3*9; i++ ) {
			pr_i2c_dbg(xbuf[i], rbuf[i]);
		}
	}
#endif
	
	return val & 0x1FF;
}

int
bb_i2c_read(int fd, uint8_t *buf, size_t len)
{
uint16_t v;
int      got;
size_t   i;
	for ( i = 0; i < len; i++ ) {
		/* ACK all but the last bit */
		v = ( 0xff << 1 ) | (i == len - 1 ? I2C_NAK : 0);
		if ( (got = bb_i2c_xfer(fd, v)) < 0 ) {
			return -1;
		}
		buf[i] = ( got >> 1 ) & 0xff;
	}
	return len;
}

int
bb_i2c_write(int fd, uint8_t *buf, size_t len)
{
uint16_t v;
int      got;
size_t   i;
	for ( i = 0; i < len; i++ ) {
		v = ( buf[i] << 1 ) | I2C_NAK; /* set ACK when sending; releases to the slave */
		if ( (got = bb_i2c_xfer(fd, v)) < 0 ) {
			return -1;
		}
		if ( (got & I2C_NAK) ) {
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
at25_id(int fd)
{
uint8_t buf[128];
int     i;

	buf[0] = AT25_OP_ID;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;

	if ( bb_spi_xfer( fd, buf, buf + 0x10, 5 ) < 0 ) {
		return -1;
	}

	for ( i = 0; i < 5; i++ ) {
		printf("0x%02x ", *(buf + 0x10 + i));
	}
	printf("\n");
	return 0;
}

int
bb_spi_read(int fd, unsigned addr, uint8_t *rbuf, size_t len)
{
uint8_t  hdr[10];
unsigned hlen = 0;
int      rval = -1;

	hdr[hlen++] = AT25_OP_FAST_READ;
	hdr[hlen++] = (addr >> 16) & 0xff;
	hdr[hlen++] = (addr >>  8) & 0xff;
	hdr[hlen++] = (addr >>  0) & 0xff;
	hdr[hlen++] = 0x00; /* dummy     */

	if ( bb_spi_cs(fd, 0) < 0 ) {
		return -1;
	}

	if ( bb_spi_xfer_nocs(fd, hdr, 0, hlen) != hlen ) {
		fprintf(stderr,"bb_spi_read -- sending header failed\n");
		goto bail;
	}

	if ( (rval = bb_spi_xfer_nocs(fd, 0, rbuf, len)) != len ) {
		fprintf(stderr,"bb_spi_read -- receiving data failed or incomplete\n");
		goto bail;
	} 

bail:
	bb_spi_cs(fd, 1);
	return rval;
}

int
at25_status(int fd)
{
uint8_t buf[2];
	buf[0] = AT25_OP_STATUS;
	if ( bb_spi_xfer( fd, buf, buf, sizeof(buf) ) < 0 ) {
		fprintf(stderr, "at25_status: bb_spi_xfer failed\n");
		return -1;
	}
	return buf[1];
}

int
at25_cmd_2(int fd, uint8_t cmd, int arg)
{
uint8_t buf[2];
int     len = 0;

	buf[len++] = cmd;
	if ( arg >= 0 && arg <= 255 ) {
		buf[len++] = arg;
	}

	if ( bb_spi_xfer(fd, buf, 0, len) < 0 ) {
		fprintf(stderr, "at25_cmd_2(0x%02x) transfer failed\n", cmd);
		return -1;
	}
	return 0;
}

int
at25_cmd_1(int fd, uint8_t cmd)
{
	return at25_cmd_2( fd, cmd, -1 );
}

int
at25_write_ena(int fd)
{
	return at25_cmd_1( fd, AT25_OP_WRITE_ENA );
}

int
at25_write_dis(int fd)
{
	return at25_cmd_1( fd, AT25_OP_WRITE_DIS );
}

int
at25_global_unlock(int fd)
{
	/* must se write-enable again; global_unlock clears that bit */
	return   at25_write_ena( fd )
          || at25_cmd_2    ( fd, AT25_OP_STATUS_WR, 0x00 )
          || at25_write_ena( fd );
}

int
at25_global_lock(int fd)
{
	return at25_write_ena( fd ) || at25_cmd_2( fd, AT25_OP_STATUS_WR, 0x3c );
}

int
at25_status_poll( int fd )
{
int st;
		/* poll status */
	do { 
		if ( (st = at25_status( fd )) < 0 ) {
			fprintf(stderr, "at25_status_poll() - failed to read status\n");
			break;
		}
	} while ( (st & AT25_ST_BUSY) );

	return st;
}

static int sz2bsz(int sz)
{
	if ( sz <= 4*1024 ) {
		return 4*1024;
	} else if ( sz <= 32*1024 ) {
		return 32*1024;
	} else if ( sz <= 64*1024 ) {
		return 64*1024;
	}
	return 512*1024;
}

int
at25_block_erase(int fd, unsigned addr, size_t sz)
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

	if ( bb_spi_xfer( fd, buf, 0, l ) < 0 ) {
		fprintf(stderr, "at25_block_erase() -- sending command failed\n"); 
		return -1;
	}

	if ( (st = at25_status_poll( fd )) < 0 ) {
		fprintf(stderr, "at25_block_erase() -- unable to poll status\n");
		return -1;
	}

	if ( (st & AT25_ST_EPE) ) {
		fprintf(stderr, "at25_block_erase() -- programming error; status 0x%02x\n", st);
		return -1;
	}

	return 0;

}

#define AT25_CHECK_ERASED 1
#define AT25_CHECK_VERIFY 2
#define AT25_EXEC_PROG    4

static int verify(int fd, unsigned addr, const uint8_t *cmp, size_t len)
{
uint8_t   buf[2048];
int       mismatch = 0;
unsigned  wrkAddr;
size_t    wrk, x;
int       got,i;

		for ( wrk = len, wrkAddr = addr; wrk > 0; wrk -= got, wrkAddr += got ) {
			x =  wrk > sizeof(buf) ? sizeof(buf) : wrk;
			got = bb_spi_read( fd, wrkAddr, buf, x );
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
at25_prog(int fd, unsigned addr, const uint8_t *data, size_t len, int check)
{
uint8_t        buf[2048];
unsigned       wrkAddr;
size_t         wrk, x;
int            i;
int            rval  = -1;
int            st;
const uint8_t *src;

	if ( (check & AT25_CHECK_ERASED) ) {
		if ( verify(fd, addr, 0, len) )
			return -1;
	}

	if ( (check & AT25_EXEC_PROG) ) {
		if ( at25_global_unlock( fd ) ) {
			fprintf(stderr, "at25_prog() -- write-enable failed\n");
			return -1;
		}

		wrk      = len;
		wrkAddr  = addr;
		src      = data;

		while ( wrk > 0 ) {

			if ( bb_spi_cs( fd, 0 ) ) {
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
			if ( bb_spi_xfer_nocs( fd, buf, 0, i ) < 0 ) {
				fprintf(stderr, "at25_prog() - failed to transmit address\n");
				goto bail;
			}
			if ( bb_spi_xfer_nocs( fd, src, 0, x ) < 0 ) {
				fprintf(stderr, "at25_prog() - failed to transmit data\n");
				goto bail;
			}

			/* this triggers the write */
			if ( bb_spi_cs( fd, 1 ) ) {
				fprintf(stderr, "at25_prog() - failed to de-assert CSb\n");
				goto bail;
			}


			if ( (st = at25_status_poll( fd )) < 0 ) {
				fprintf(stderr, "at25_prog() - failed to poll status\n");
				goto bail;
			}

			if ( (st & AT25_ST_EPE) ) {
				fprintf(stderr, "at25_status_poll() -- programming error (status 0x%02x, writing page 0x%x) -- aborting\n", st, wrkAddr);
				goto bail;
			}

			/* programming apparently disables writing */
			at25_write_ena  ( fd ); /* just in case... */

			printf("%c", '.'); fflush(stdout);

			src     += x;
			wrk     -= x;
			wrkAddr += x;
		}
		printf("\n");

	}

	if ( (check & AT25_CHECK_VERIFY) ) {
		if ( verify(fd, addr, data, len) ) {
			goto bail;
		}
	}

	rval = len;
	
bail:
	if ( (check & AT25_EXEC_PROG) ) {
		at25_global_lock( fd ); /* if this succeeds it clears the write-enable bit */
		at25_write_dis  ( fd ); /* just in case... */
	}

	bb_spi_cs( fd, 1 );     /* just in case... */

	return rval;
}

static int
fileMap(const char *progFile,  uint8_t **mapp, off_t *sizp, off_t creatsz)
{
int             progfd    = -1;
int             rval      = -1;
struct stat     sb;

	if ( progFile && ( MAP_FAILED == (void*)*mapp ) ) {

		if ( (progfd = open( progFile, (O_RDWR | O_CREAT), 0664 )) < 0 ) {
			perror("unable to open program file");
			goto bail;	
		}
		if ( creatsz && ftruncate( progfd, creatsz ) ) {
			perror("fileMap(): ftruncate failed");
			goto bail;
		}
		if ( fstat( progfd, &sb ) ) {
			perror("unable to fstat program file");
			goto bail;
		}
		*mapp = (uint8_t*) mmap( 0, sb.st_size, ( PROT_WRITE | PROT_READ ), MAP_SHARED, progfd, 0 );
		if ( MAP_FAILED == (void*) *mapp ) {
			perror("unable to map program file");
			goto bail;
		}
		*sizp = sb.st_size;
	}

	rval = 0;

bail:
	if ( progfd >= 0 ) {
		close( progfd );
	}
	return rval;
}


static void usage(const char *nm)
{
	printf("usage: %s [-hvDI!?] [-d usb-dev] [-S SPI_flashCmd] [-a flash_addr] [-f flash_file] [register] [values...]\n", nm);
	printf("   -S cmd{,cmd}       : commands to execute on 25DF041 SPI flash (see below).\n");
	printf("   -f flash-file      : file to write/verify when operating on SPI flash.\n");
    printf("   -!                 : must be given in addition to flash-write/program command. This is a 'safety' feature.\n");
    printf("   -?                 : instead of programming the flash verify its contents against a file (-f also required).\n");
	printf("   -a address         : start-address for SPI flash opertions [0].\n");
	printf("   -I                 : address I2C (47CVB02 DAC). Supply register address and values (when writing).\n");
	printf("   -D                 : address I2C clock (5P49V5925). Supply register address and values (when writing).\n");
	printf("   -d usb-device      : usb-device [/dev/ttyUSB0].\n");
	printf("   -I                 : test I2C (47CVB02 DAC).\n");
	printf("   -h                 : this message.\n");
	printf("   -v                 : increase verbosity level\n");
	printf("\n");
	printf("    SPI Flash commands: multiple commands (separated by ',' w/o blanks) may be given.\n");
	printf("       Id             : read and print ID bytes.\n");
	printf("       St             : read and print status.\n");
	printf("       Rd<size>       : read and print <size> bytes [100] (starting at -a <addr>)\n");
    printf("       Wena           : enable write/erase -- needed for erasing; the programming operation does this implicitly\n");
    printf("       Wdis           : disable write/erase (programming operation still implicitly enables writing).\n");
    printf("       Erase<size>    : erase a block of <size> bytes. Starting address (-a) is down-aligned to block\n");
    printf("                        size and <size> is up-aligned to block size: 4k, 32k, 64k or entire chip.\n");
    printf("                        <size> may be omitted if '-f' is given. The file size will be used...\n");
	printf("\n");
    printf("Example: erase and write 'foo.bit' starting at address 0x10000:\n");
	printf("\n");
    printf("   %s -a 0x10000 -f foo.bin -SWena,Erase,Prog -!\n", nm);
}
    
	
int main(int argc, char **argv)
{
const char        *devn      = "/dev/ttyUSB0";
int                fd        = -1;
int                rval      = 1;
unsigned           speed     = 115200;
uint8_t           *buf       = 0;
unsigned           buflen    = 100;
int                i;
int                reg       =  0;
int                val       = -1;
unsigned           rdl       = 0;
unsigned           wrl       = 0;

uint8_t            sla;

int                dac       = 0;

int                opt;
int                test_i2c  = 0;
char              *test_spi  = 0;
unsigned           flashAddr = 0;
unsigned          *u_p;
char              *progFile  = 0;
uint8_t           *progMap   = (uint8_t*)MAP_FAILED;
off_t              progSize  = 0;
int                doit      = 0;

	while ( (opt = getopt(argc, argv, "hvd:DIS:a:f:!?")) > 0 ) {
		u_p = 0;
		switch ( opt ) {
            case 'h': usage(argv[0]);             return 0;
			default : fprintf(stderr, "Unknown option -%c (use -h for help)\n", opt); return 1;
			case 'd': devn = optarg;              break;
			case 'D': dac  = 1; test_i2c = 1;     break;
			case 'v': bb_debug++;                 break;
			case 'I': test_i2c = 1;               break;
			case 'S': test_spi = strdup(optarg);  break;
			case 'a': u_p      = &flashAddr;      break;
			case 'f': progFile = optarg;          break;
			case '!': doit     = 1;               break;
			case '?': doit     = (doit <= 0 ? doit - 1 : -1); break;
		}
		if ( u_p && 1 != sscanf(optarg, "%i", u_p) ) {
			fprintf(stderr, "Unable to scan argument to option -%c -- should be a number\n", opt);
			return -1;
		}
	}

	if ( argc > optind && (1 != sscanf(argv[optind], "%i", &reg) || reg > 0xff) ) {
		fprintf(stderr, "Invalid reg\n");
		return 1;
	}

	if ( argc > optind + 1 && (1 != sscanf(argv[optind + 1], "%i", &val) || val < 0 || val > 0xffff) ) {
		fprintf(stderr, "Invalid val\n");
		return 1;
	}

	if ( (fd = bb_open(devn, speed)) < 0 ) {
		goto bail;
	}

	if ( ! (buf = malloc(buflen)) ) {
		perror("No memory");
		goto bail;
	}

	if ( test_spi ) {
		char *wrk;
		char *op;

		for ( op = test_spi; (op = strtok_r( op, ",", &wrk )); op = 0 /* for strtok_r */  ) {

			if ( strstr(op, "Id") ) {
				if ( at25_id(fd) < 0 ) {
					goto bail;
				}
			} else if ( strstr(op, "Rd") ) {

				uint8_t *maddr;
				off_t    msize;

				i = buflen;

				if ( strlen(op) > 2 && 1 != sscanf(op, "Rd%i", &i) ) {
					fprintf(stderr, "Skipping '%s' -- expected format 'Rd<xxx>' with xxx a number\n", op);
					continue;
				}

				if ( 0 == i ) {
					fprintf(stderr, "Skipping read of zero bytes\n");
					continue;
				}

				if ( progFile ) {
					if ( fileMap(progFile,  &progMap, &progSize, i) ) {
						goto bail;
					}
					maddr = progMap;
					msize = progSize;
				} else {
					if ( i != buflen ) {
						buf    = 0;
						buflen = i;
						if ( ! (buf = malloc( buflen )) ) {
							perror("No memory to alloc buffer");
							goto bail;
						}
					}
					maddr = buf;
					msize = buflen;
				}

				if ( (i = bb_spi_read(fd, flashAddr, maddr, msize)) < 0 ) {
					goto bail;
				}

				if ( i != msize ) {
					printf("Incomplete read; only got %d out of %d\n", i, (unsigned)msize);
				}

				if ( ! progFile ) {
					for ( i = 0; i < msize; i++ ) {
						printf("0x%02x ", maddr[i]);
						if ( (i & 0xf) == 0xf ) printf("\n");
					}
					printf("\n");
				}
			} else if ( strstr(op, "St") ) {
				if ( (i = at25_status( fd )) < 0 ) {
					goto bail;
				}
				printf("SPI Flash status: 0x%02x\n", i);
			} else if ( strstr(op, "Wena") ) {
				if ( at25_global_unlock( fd ) ) {
					goto bail;
				}
			} else if ( strstr(op, "Wdis") ) {
				if ( at25_global_lock( fd ) ) {
					goto bail;
				}
			} else if ( strstr(op, "Prog") ) {
				unsigned cmd;

				if ( ! progFile ) {
					fprintf(stderr, "Prog requires a file name (use -f; -h for help)\n");
					goto bail;
				}

				printf("Programming '%s' (0x%lx / %ld bytes) to address 0x%x in flash\n",
						progFile,
						(unsigned long)progSize,
						(unsigned long)progSize,
						flashAddr);
				if ( ! doit ) {
					printf("... bailing out -- please use -! to proceed or -? to just verify the flash\n");
					continue;
				}

				if ( doit < 0 ) {
					cmd = AT25_CHECK_VERIFY;
				} else {
					cmd = AT25_CHECK_ERASED | AT25_EXEC_PROG | AT25_CHECK_VERIFY;
				}

				if ( fileMap(progFile,  &progMap, &progSize, 0) ) {
					goto bail;
				}

				if ( at25_prog(fd, flashAddr, progMap, progSize, cmd) < 0 ) {
					fprintf(stderr, "Programming flash failed\n");
					goto bail;
				}
			} else if ( strstr(op, "Erase") ) {

				unsigned aligned;

				if ( doit < 0 ) {
					printf("Erase: skipping during verify (-?)\n");
					continue;
				}


				if ( progFile ) {
					if ( fileMap(progFile,  &progMap, &progSize, 0) ) {
						goto bail;
					}
					i = progSize;
				} else if ( 1 != sscanf(op, "Erase%i", &i) ) {
					fprintf(stderr, "Skipping '%s' -- expected format 'Erase<xxx>' with xxx a number\n", op);
					continue;
				}

                i       = sz2bsz( i );
                aligned = flashAddr & ~ (i-1);
				printf("Erasing 0x%x/%d bytes from address 0x%x\n", i, i, aligned);

				if ( doit <= 0 ) {
					printf("... bailing out -- please use -! to proceed or -? to just verify the flash\n");
					continue;
				}

				if ( at25_block_erase(fd, aligned, i) < 0 ) {
					fprintf(stderr, "at25_block_erase(%d) failed\n", i);
					goto bail;
				}
			}

			 else {
				fprintf(stderr, "Skipping unrecognized SPI command '%s'\n", op);
			}

		}
	}


	if ( test_i2c ) {

		sla = dac ? 0xc2 : 0xd4;

		bb_i2c_start(fd, 0);

		if ( reg < 0 && dac ) {
			/* reset */
			buf[0] = 0x00;
			buf[1] = 0x06;
			bb_i2c_write(fd, buf, 2);
		} else {
			if ( val < 0 ) {
				/* read */
				buf[0] = sla;
				buf[1] = dac ? ( 0x06 | ((reg&0x1f) << 3) ) : reg;
				bb_i2c_write(fd, buf, 2);
				bb_i2c_start(fd, 1);
				buf[0] = sla | I2C_READ;
				bb_i2c_write(fd, buf, 1);
				rdl    = dac ? 2 : 1;
				bb_i2c_read(fd, buf, rdl);
			} else {
				wrl    = 0;
				buf[wrl++] = sla;
				buf[wrl++] = dac ? ( 0x00 | ((reg&0x1f) << 3) ) : reg;
				if ( dac ) {
					buf[wrl++] = (val >> 8) & 0xff;
				}
				buf[wrl++] = (val >> 0) & 0xff;
				bb_i2c_write(fd, buf, wrl);
			}
		}
		bb_i2c_stop(fd);

		if ( rdl ) {
			printf("reg: 0x%x: 0x", reg);
			for ( i = 0; i < rdl; i++ ) {
				printf("%02x", buf[i]);
			}
			printf("\n");
		}
	}

	rval = 0;

bail:
	if ( fd >= 0 ) {
		bb_close( fd );
	}
	if ( (void*)progMap != MAP_FAILED ) {
		munmap( (void*)progMap, progSize );
	}
	if ( buf ) {
		free( buf );
	}
	return rval;
}
