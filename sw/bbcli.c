#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include <math.h>

#include "fwComm.h"
#include "fwUtil.h"
#include "at25Sup.h"
#include "dac47cxSup.h"
#include "lmh6882Sup.h"
#include "max195xxSup.h"

static void usage(const char *nm)
{
	printf("usage: %s [-hvDI!?] [-d usb-dev] [-S SPI_flashCmd] [-a flash_addr] [-f flash_file] [register] [values...]\n", nm);
	printf("   -S cmd{,cmd}       : commands to execute on 25DF041 SPI flash (see below).\n");
	printf("   -f flash-file      : file to write/verify when operating on SPI flash.\n");
    printf("   -!                 : must be given in addition to flash-write/program command. This is a 'safety' feature.\n");
    printf("   -?                 : instead of programming the flash verify its contents against a file (-f also required).\n");
	printf("   -a address         : start-address for SPI flash opertions [0].\n");
	printf("   -I                 : address I2C clock (5P49V5925). Supply register address and values (when writing).\n");
	printf("   -D                 : address I2C DAC (47CVB02). Supply register address and values (when writing).\n");
	printf("   -d usb-device      : usb-device [/dev/ttyUSB0].\n");
	printf("   -I                 : test I2C (47CVB02 DAC).\n");
	printf("   -h                 : this message.\n");
	printf("   -v                 : increase verbosity level.\n");
	printf("   -V                 : dump firmware version.\n");
	printf("   -B                 : dump ADC buffer (raw).\n");
    printf("   -T [op=value]      : set acquisition parameter and trigger (op: 'level', 'autoMS', 'decim', 'src', 'edge', 'npts', 'factor').\n");
    printf("                        NOTE: 'level' is normalized to int16 range; 'factor' to 2^%d!\n", ACQ_LD_SCALE_ONE);
	printf("   -p                 : dump acquisition parameters.\n");
	printf("   -F                 : flush ADC buffer.\n");
	printf("   -P                 : access PGA registers.\n");
	printf("   -A                 : access ADC registers.\n");
	printf("\n");
	printf("    SPI Flash commands: multiple commands (separated by ',' w/o blanks) may be given.\n");
	printf("       Id             : read and print ID bytes.\n");
	printf("       St             : read and print status.\n");
	printf("       Rd<size>       : read and print <size> bytes [100] (starting at -a <addr>)\n");
    printf("       Wena           : enable write/erase -- needed for erasing; the programming operation does this implicitly\n");
    printf("       Wdis           : disable write/erase (programming operation still implicitly enables writing).\n");
    printf("       Prog           : program flash.\n");
    printf("       Erase<size>    : erase a block of <size> bytes. Starting address (-a) is down-aligned to block\n");
    printf("                        size and <size> is up-aligned to block size: 4k, 32k, 64k or entire chip.\n");
    printf("                        <size> may be omitted if '-f' is given. The file size will be used...\n");
	printf("\n");
    printf("Example: erase and write 'foo.bit' starting at address 0x10000:\n");
	printf("\n");
    printf("   %s -a 0x10000 -f foo.bin -SWena,Erase,Prog -!\n", nm);
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

#define TEST_I2C 1
#define TEST_PGA 2
#define TEST_ADC 3

static int
scanl(const char *tok, const char *eq, long *vp)
{
	if ( 1 != sscanf( eq + 1, "%li", vp ) ) {
		fprintf(stderr, "Error -- parseAcqParam: unable to scan value in '%s'\n", tok);
		return -1;
	}
	return 0;
}

static int
scanDecm(const char *tok, const char *eq, long *d0p, long *d1p)
{
	if ( 2 != sscanf( eq + 1, "%lix%li", d0p, d1p ) ) {
		fprintf(stderr, "Error -- parseAcqParam: unable to scan value in '%s'\n", tok);
		return -1;
	}
	return 0;
}

static int
scanScal(const char *tok, const char *eq, long *d0p, long *d1p, long *d2p)
{
double s;
	if ( 3 != sscanf( eq + 1, "%li:%li:%lf", d0p, d1p, &s ) ) {
		fprintf(stderr, "Error -- parseAcqParam: unable to scan value in '%s'\n", tok);
		return -1;
	}
    if ( s > 2.0 ) {
		*d2p = (long)s;
	} else {
		*d2p = (long)round(exp2(ACQ_LD_SCALE_ONE) * s);
	}
	return 0;
}


static int
parseAcqParams(AcqParams *pp, const char *ops)
{
char *str = strdup( ops );
char *ctx;
char *tok;
char *eq;
char *val;
int   rval = -1;
long  v[4];

	if ( ! str ) {	
		fprintf(stderr, "Error -- parseAcqParams: no memory\n");
		return -1;
	}

	pp->mask = ACQ_PARAM_MSK_GET; /* clear mask */

	for ( (tok = strtok_r( str, ",", &ctx )); tok; (tok = strtok_r(0, ",", &ctx)) ) {
		if ( ! (eq = strchr(tok, '=')) ) {
			fprintf(stderr, "Error -- parseAcqParams: expect <parm> '=' <value> pairs ('=' missing in op %s)\n", tok);
			goto bail;
		}

		switch ( toupper( tok[0] ) ) {
			case 'A':
				for ( val = eq + 1; isspace( *val ); val++ )
					/* nothing else */;
				if ( 'I' == toupper( *val ) ) {
					v[0] = ACQ_PARAM_TIMEOUT_INF;
				} else if ( scanl( tok, eq, &v[0] ) ) {
					goto bail;
				}
				pp->autoTimeoutMS = (uint32_t) v[0];
				pp->mask |= ACQ_PARAM_MSK_AUT;
				break;
			case 'D':
				if ( scanDecm( tok, eq, &v[0], &v[1] ) ) goto bail;
				pp->cic0Decimation = (uint8_t)  v[0];
				pp->cic1Decimation = (uint32_t) v[1];
				pp->mask |= ACQ_PARAM_MSK_DCM;
				break;
			case 'E':
				for ( val = eq + 1; isspace( *val ); val++ )
					/* nothing else */;
				switch( toupper( *val ) ) {
					case 'R': pp->rising = 1; break;
					case 'F': pp->rising = 0; break;
					default :
						fprintf(stderr, "Error -- parseAcqParams: Invalid trigger edge '%s'\n", val);
						goto bail;
				}
				pp->mask |= ACQ_PARAM_MSK_EDG;
				break;
			case 'F':
				if ( scanScal( tok, eq, &v[0], &v[1], &v[2] ) ) goto bail;
				pp->cic0Shift      = (uint8_t)  v[0];
				pp->cic1Shift      = (uint8_t)  v[1];
				pp->scale          = (int32_t)  v[2];
				pp->mask |= ACQ_PARAM_MSK_SCL;
				break;
			case 'L':
				if ( scanl( tok, eq, &v[0] ) ) goto bail;
				pp->level = (int16_t) v[0];
				pp->mask |= ACQ_PARAM_MSK_LVL;
				break;
			case 'N':
				if ( scanl( tok, eq, &v[0] ) ) goto bail;
				pp->npts  = (uint32_t) v[0];
				pp->mask |= ACQ_PARAM_MSK_NPT;
				break;
			case 'S':
				for ( val = eq + 1; isspace( *val ); val++ )
					/* nothing else */;
				if (   ! strcmp( val, "Channel A")
				    || ! strcmp( val, "ChannelA")
				    || ! strcmp( val, "CHA")
				    || ! strcmp( val, "CH_A")
				    || ! strcmp( val, "A") ) {
					pp->src = CHA;
				} else
				if (   ! strcmp( val, "Channel B")
				    || ! strcmp( val, "ChannelB")
				    || ! strcmp( val, "CHB")
				    || ! strcmp( val, "CH_B")
				    || ! strcmp( val, "B") ) {
					pp->src = CHB;
				} else if ( 'E' == toupper( *val ) ) {
					pp->src = EXT;
				} else {
			    	fprintf(stderr, "Error -- parseAcqParams: Invalid trigger source '%s'\n", val);
					goto bail;
				}
				pp->mask |= ACQ_PARAM_MSK_SRC;
				break;
			default:
			    fprintf(stderr, "Error -- parseAcqParams: invalid operation: '%s'\n", tok);
				goto bail;
		}
	}

	rval = 0;
bail:
	free( str );
	return rval;
}

int main(int argc, char **argv)
{
const char        *devn      = "/dev/ttyUSB0";
FWInfo            *fw        = 0;
int                rval      = 1;
unsigned           speed     = 115200; /* not sure this really matters */
uint8_t           *buf       = 0;
/* max # samples in 200 device is 16k */
unsigned           buflen    = 33000;
int                i;
int                reg       =  0;
int                val       = -1;
unsigned           rdl       = 0;

uint8_t            sla;

int                dac       = 0;

int                opt;
int                test_reg  = 0;
char              *test_spi  = 0;
unsigned           flashAddr = 0;
unsigned          *u_p;
char              *progFile  = 0;
uint8_t           *progMap   = (uint8_t*)MAP_FAILED;
off_t              progSize  = 0;
int                doit      = 0;
int                debug     = 0;
int                fwVersion = 0;
int                dumpAdc   = 0;
int                dumpPrms  = 0;
const char        *trgOp     = 0;

	while ( (opt = getopt(argc, argv, "Aa:BDd:Ff:hIPpS:T:Vv!?")) > 0 ) {
		u_p = 0;
		switch ( opt ) {
            case 'h': usage(argv[0]);                                                 return 0;
			default : fprintf(stderr, "Unknown option -%c (use -h for help)\n", opt); return 1;
			case 'd': devn = optarg;                                                  break;
			case 'D': dac  = 1; test_reg = TEST_I2C;                                  break;
			case 'P': dac  = 0; test_reg = TEST_PGA;                                  break;
			case 'A': dac  = 0; test_reg = TEST_ADC;                                  break;
			case 'B': dumpAdc = 1;                                                    break;
			case 'F': dumpAdc = -1;                                                    break;
			case 'p': dumpPrms= 1;                                                    break;
			case 'v': debug++;                                                        break;
			case 'V': fwVersion= 1;                                                   break;
			case 'I': dac = 0; test_reg = TEST_I2C;                                   break;
			case 'S': test_spi = strdup(optarg);                                      break;
			case 'T': trgOp    = optarg;                                              break;
			case 'a': u_p      = &flashAddr;                                          break;
			case 'f': progFile = optarg;                                              break;
			case '!': doit     = 1;                                                   break;
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

	if ( ! (fw = fw_open( devn, speed ) ) ) {
		goto bail;
	}

	fw_set_debug( fw, debug );

	if ( ! (buf = malloc(buflen)) ) {
		perror("No memory");
		goto bail;
	}

	if ( fwVersion ) {
		int64_t v = fw_get_version( fw );
		if ( (int64_t)-1 == v ) {
			fprintf(stderr, "Error: fw_get_version() failed\n");
		} else {
			printf("Firmware version: %08" PRIX64 "\n", v);
		}
	}


	if ( trgOp ) {
		AcqParams p;
		if ( parseAcqParams( &p, trgOp ) ) {
			goto bail;
		}
		if ( acq_set_params( fw, &p, 0 ) ) {
			fprintf(stderr, "Error: transferring acquisition parameters failed\n");
			goto bail;
		}
	}
	if ( dumpPrms ) {
		AcqParams p;
		p.mask = ACQ_PARAM_MSK_GET;
		if ( acq_set_params( fw, 0, &p ) ) {
			fprintf(stderr, "Error: transferring acquisition parameters failed\n");
			goto bail;
		}
		printf("Trigger Source     : %s\n",
			CHA == p.src ? "Channel A" : (CHB == p.src ? "Channel B" : "External"));
		printf("Edge               : %s\n", p.rising ? "rising" : "falling");
		printf("Trigger Level      : %" PRId16 "\n", p.level );
		printf(" NOTE: Trigger level is int16_t, ADC numbers are normalized to\n");
		printf("       this range!\n");
		printf("N Pretrig samples  : %" PRIu32 "\n", p.npts  );
		printf("Autotrig timeout   : ");
			if ( ACQ_PARAM_TIMEOUT_INF == p.autoTimeoutMS ) {
				printf("<infinite>\n");
			} else {
				printf("%" PRIu32 " ms\n", p.autoTimeoutMS  );
			}
		printf("Decimation         : %" PRIu8 " x %" PRIu32 "\n", p.cic0Decimation, p.cic1Decimation);
		printf("Scale\n");
        printf("    Cic0 Shift     : %" PRIu8 "\n", p.cic0Shift);
        printf("    Cic1 Shift     : %" PRIu8 "\n", p.cic1Shift);
        printf("    Scale          : %" PRIi32 " (%f)\n", p.scale, (double)p.scale/exp2(ACQ_LD_SCALE_ONE));
	}


	if ( dumpAdc ) {
		int      j;
		uint16_t hdr;
		printf("ADC Buffer size: %ld\n", buf_get_size( fw ));
		if ( dumpAdc > 0 ) {
			i = buf_read( fw, &hdr, buf, buflen );
		} else {
			i = buf_flush( fw );
		}
		if ( i > 0 ) {
			printf("ADC Data (got %d, header: 0x%04" PRIx16 ")\n", i, hdr);
			for ( j = 0; j < i; j++ ) {
				printf("0x%02" PRIx8 " ", buf[j]);
				if ( 0xf == ( j & 0xf) ) {
					printf("\n");
				}
			}
			if ( (j & 0xf) ) {
				printf("\n");
			}
		}
		
	}

	if ( test_spi ) {
		char *wrk;
		char *op;

		for ( op = test_spi; (op = strtok_r( op, ",", &wrk )); op = 0 /* for strtok_r */  ) {

			if ( strstr(op, "Id") ) {
				if ( at25_id( fw ) < 0 ) {
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

				if ( ( i = at25_spi_read( fw, flashAddr, maddr, msize ) ) < 0 ) {
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
				if ( (i = at25_status( fw )) < 0 ) {
					goto bail;
				}
				printf("SPI Flash status: 0x%02x\n", i);
			} else if ( strstr(op, "Wena") ) {
				if ( at25_global_unlock( fw ) ) {
					goto bail;
				}
			} else if ( strstr(op, "Wdis") ) {
				if ( at25_global_lock( fw ) ) {
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

				if ( at25_prog( fw, flashAddr, progMap, progSize, cmd ) < 0 ) {
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

				if ( at25_block_erase( fw, aligned, i) < 0 ) {
					fprintf(stderr, "at25_block_erase(%d) failed\n", i);
					goto bail;
				}
			}

			 else {
				fprintf(stderr, "Skipping unrecognized SPI command '%s'\n", op);
			}

		}

	}


	if ( test_reg ) {

		switch ( test_reg ) {

			case TEST_I2C:

				sla = dac ? 0xc2 : 0xd4;

				if ( reg < 0 && dac ) {
					/* reset */
					dac47cxReset( fw );
				} else {
					if ( dac ) {
						if ( val < 0 ) {
							uint16_t dacdat;
							dac47cxReadReg( fw, reg, &dacdat );
							buf[0] = dacdat >> 8;
							buf[1] = dacdat >> 0;
							rdl    = 2;
						} else {
							dac47cxWriteReg( fw, reg, val );
						}
					} else {
						i = ( val < 0 ? bb_i2c_read_reg( fw, sla, reg ) : bb_i2c_write_reg( fw, sla, reg, val ) );
						if ( i < 0 ) {
							fprintf(stderr, "bb_i2c_%s_reg failed\n", val < 0 ? "read" : "write");
							goto bail;
						}
						if ( val < 0 ) {
							buf[0] = (uint8_t) i ;
							rdl    = 1;
						}
					}
				}
				break;

			case TEST_PGA:
				i = ( val < 0 ? lmh6882ReadReg( fw, reg ) : lmh6882WriteReg( fw, reg, val ) );
				if ( i < 0 ) {
					fprintf(stderr, "lmh6882%sReg() failed\n", val < 0 ? "Read" : "Write");
					goto bail;
				}
				if ( val < 0 ) {
					buf[0] = (uint8_t)i;
					rdl = 1;
				}
				break;

			case TEST_ADC:
				if ( val < 0 ) {
					max195xxReadReg( fw, reg, buf );
				} else {
					buf[0] = val;
					max195xxWriteReg( fw, reg, buf[0] );
				}
				rdl = (val < 0 ? 1 : 0);
				break;

			default:
				break;
		}

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
	if ( fw ) {
		fw_close( fw );
	}
	if ( (void*)progMap != MAP_FAILED ) {
		munmap( (void*)progMap, progSize );
	}
	if ( buf ) {
		free( buf );
	}
	return rval;
}
