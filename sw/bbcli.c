#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include <math.h>
#include <regex.h>
#include <errno.h>
#include <unistd.h>

#include "fwComm.h"
#include "fwUtil.h"
#include "at25Sup.h"
#include "dac47cxSup.h"
#include "max195xxSup.h"
#include "fegRegSup.h"
#include "scopeSup.h"
#include "hdf5Sup.h"

/* covers image size of xc3s200a for multiboot */
#ifndef  FLASHADDR_DFLT
#define  FLASHADDR_DFLT 0x30000
#endif

static void usage(const char *nm)
{
	printf("usage: %s [-hvDI!?] [-d usb-dev] [-S SPI_flashCmd] [-a flash_addr] [-f flash_file] [register] [values...]\n", nm);
	printf("   -S cmd{,cmd}       : commands to execute on 25DF041 SPI flash (see below).\n");
	printf("   -f flash-file      : file to write/verify when operating on SPI flash.\n");
    printf("   -!                 : must be given in addition to flash-write/program command. This is a 'safety' feature.\n");
    printf("   -?                 : instead of programming the flash verify its contents against a file (-f also required).\n");
	printf("   -a address         : start-address for SPI flash operations [0x%x].\n", FLASHADDR_DFLT);
	printf("   -I                 : address I2C clock (5P49V5925). Supply register address and values (when writing).\n");
	printf("   -D                 : address I2C DAC (47CVB02). Supply register address and values (when writing).\n");
	printf("   -d usb-device      : usb-device [/dev/ttyACM0]; you may also set the BBCLI_DEVICE env-var.\n");
	printf("   -h                 : this message.\n");
	printf("   -v                 : increase verbosity level.\n");
	printf("   -V                 : dump firmware version.\n");
	printf("   -B                 : dump ADC buffer (raw).\n");
	printf("   -5 hdf5_filename   : dump ADC buffer (HDF5).\n");
	printf("   -C <comment>       : add <comment> to the HDF5 data.\n");
    printf("   -T [op=value]      : set acquisition parameter and trigger (op: 'level', 'autoMS', 'decim', 'src', 'edge', 'npts', 'nsmpl', 'factor', 'extTrgOE').\n");
    printf("                        NOTE: 'level' is normalized to int16 range; 'factor' to 2^%d!\n", ACQ_LD_SCALE_ONE);
    printf("                              and may be appended with ':hysteresis'\n");
    printf("                              (hysteresis always positive)\n");
	printf("   -p                 : dump acquisition parameters.\n");
	printf("   -F                 : flush ADC buffer.\n");
	printf("   -P                 : Program Front-end -- comma-separated list of '<parm>[channel]=<value>'.\n");
	printf("                        If no channel index is specified the all channels are set.\n");
    printf("                           Coupling=AC|DC\n");
    printf("                           Termination=On|Off\n");
    printf("                           FECAttenuator=On|Off\n");
    printf("                           DACRangeHigh=On|Off\n");
    printf("                           PGAAttenuation=<value_in_dB>\n");
    printf("                         Example: -PTerm[0]=On,Term[1]=Off,Coupling=AC\n");
	printf("   -R <reg_op>        : register read/write operation:\n");
	printf("                         READ : <addr>:<len>\n");
	printf("                         WRITE: <addr>=<val>{,<val>}\n");
	printf("   -A                 : access ADC registers.\n");
	printf("   -i i2c_addr        : access ADC registers.\n");
	printf("\n");
	printf("    SPI Flash commands: multiple commands (separated by ',' w/o blanks) may be given.\n");
	printf("       ForceBB        : force using bit-bang, even if a SPI controller is available.\n");
	printf("       Id             : read and print ID bytes.\n");
	printf("       St             : read and print status.\n");
	printf("       Reset          : reset the flash device.\n");
	printf("       Resume         : resume from (ultra-) power down\n");
	printf("       Rd<size>       : read and print <size> bytes [100] (starting at -a <addr>)\n");
    printf("       Wena           : enable write/erase -- needed for erasing; the programming operation does this implicitly\n");
    printf("       Wdis           : disable write/erase (programming operation still implicitly enables writing).\n");
    printf("       Prog           : program flash.\n");
    printf("       Erase<size>    : erase a block of <size> bytes. Starting address (-a) is down-aligned to block\n");
    printf("                        size and <size> is up-aligned to block size: 4k, 32k, 64k or entire chip.\n");
    printf("                        <size> may be omitted if '-f' is given. The file size will be used...\n");
	printf("\n");
    printf("Example: erase and write 'foo.bin' starting at address 0x00000:\n");
	printf("\n");
    printf("   %s -a 0x00000 -f foo.bin -SWena,Erase,Prog -!\n", nm);
}

static const int blocks [] = {
	4*1024,
	32*1024,
	64*1024
};

/* largest block smaller than 'sz' */
static int sz2bsz(int sz)
{
int i;
	for ( i = sizeof(blocks)/sizeof(blocks[0]) - 1; i >= 0; i-- ) {
		if ( blocks[i] <= sz ) {
			return blocks[i];
		}
	}
	return blocks[0];
}

static int algnblk(unsigned addr)
{
int i;
	for ( i = sizeof(blocks)/sizeof(blocks[0]) - 1; i >= 0; i-- ) {
		if ( (addr & (blocks[i] - 1)) == 0 ) {
			return blocks[i];
		}
	}
	return blocks[0];
}

#define TEST_I2C 1
#define TEST_ADC 3
#define TEST_FEG 4

static int
scanl(const char *tok, const char *eq, long *vp)
{
	if ( 1 != sscanf( eq + 1, "%li", vp ) ) {
		fprintf(stderr, "Error -- parseAcqParam: unable to scan long int value in '%s'\n", tok);
		return -1;
	}
	return 0;
}

static int
scanDecm(const char *tok, const char *eq, long *d0p, long *d1p)
{
	if ( 2 != sscanf( eq + 1, "%lix%li", d0p, d1p ) ) {
		fprintf(stderr, "Error -- parseAcqParam: unable to scan values for '%s'; expected: <decim0>x<decim1>\n", tok);
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

static void pronoff(ScopePvt *scp, const char *prefix, const char *onstr, const char *offstr, int (*f)(ScopePvt*, unsigned))
{
	unsigned ch;
	unsigned numCh = scope_get_num_channels( scp );
	int      vi;
	printf("    %-15s:", prefix);
	for ( ch = 0; ch < numCh; ++ch ) {
		if ( (vi = f( scp, ch )) < 0 ) {
			printf(" NOT SUPPORTED");
			break;
		} else {
			/* trick to map index to 'A'..'F' without having to range-check */
			printf(" CH %X: %s", ch + 10, vi ? onstr : offstr);
		}
	}
	printf("\n");
}

static void
dumpFrontEndParams(ScopePvt *scp)
{
double   vd1, vd2;
unsigned ch;
unsigned numCh = scope_get_num_channels( scp );

	printf("Front End Settings:\n");
	printf("  PGA:");
	if ( 0 != pgaGetAttRange( scp, &vd1, &vd2 ) ) {
		printf(" NOT SUPPORTED\n");
	} else {
		printf("\n");
		printf("    %-15s: %.0lfdB..%.0lfdB\n", "Range", vd1, vd2);
		printf("    %-15s:", "Attenuation");
		for ( ch = 0; ch < numCh; ++ch ) {
			if ( 0 == pgaGetAtt( scp, ch, &vd1 ) ) {
				printf(" CH %d: %3.1lfdB", ch, vd1);
			} else {
				printf(" NOT SUPPORTED");
				break;
			}
		}
		printf("\n");
	}
	printf("  FEC:\n");
	pronoff( scp, "Coupling",     "    AC", "    DC", fecGetACMode );
	pronoff( scp, "Termination",  " 50Ohm", " 1MOhm", fecGetTermination );
	pronoff( scp, "DACHighRange", "    On", "   Off", fecGetDACRangeHi );
	printf("    %-15s:", "Attenuation");
	for ( ch = 0; ch < numCh; ++ch ) {
		if ( 0 == fecGetAtt( scp, ch, &vd1 ) ) {
			/* map channel index to 'A'.. */
			printf(" CH %X: %4.0lfdB", ch + 10, vd1);
		} else {
			printf(" NOT SUPPORTED");
			break;
		}
	}
	printf("\n");
}

static int scanBool(const char *prefix, const char *val)
{
int iv = -1;
	switch ( toupper( val[0] ) ) {
		case 'T':
		case '1':
			iv = 1;
			break;
		case 'F':
		case '0':
			iv = 0;
			break;
		case 'O':
			switch ( toupper( val[1] ) ) {
				case 'N': iv = 1; break;
				case 'F': iv = 0; break;
				default:          break;
			}
		default:
			break;
	}
	if ( iv < 0 ) {
		fprintf(stderr, "Error -- parseFrontEndParams: invalid '%s' value - expect '1' or '0'\n", prefix);
	}
	return iv;
}

static int
parseFrontEndParams(ScopePvt *scp, const char *ops)
{
char   *str  = strdup( ops );
int     rval = -1;
char   *ctx;
char   *tok;
char   *val;
regex_t chanPat;
int     chb, che;
int     iv;
double  dv1, dv2;
int     st;
regmatch_t matches[5];

	/*if ( (st = regcomp( &chanPat, "^[^[=]*[[]([0-9]+)[]]", REG_EXTENDED )) ) {*/
	if ( (st = regcomp( &chanPat, "^[^=[]*(([[]([0-9]+)[]]){0,1}[=])", REG_EXTENDED )) ) {
		char msg[256];
		regerror( st, &chanPat, msg, sizeof(msg) );
		fprintf(stderr, "Internal Error -- regcomp failed: %s\n", msg);
		abort();
	}

	if ( ! str ) {
		fprintf(stderr, "Error -- parseFrontEndParams: no memory\n");
		goto bail;
	}

	for ( (tok = strtok_r( str, ",", &ctx )); tok; (tok = strtok_r(0, ",", &ctx)) ) {

		val = 0;
		chb = 0;
		che = scope_get_num_channels( scp ) - 1;
		if ( 0 == regexec( &chanPat, tok, sizeof(matches)/sizeof(matches[0]), matches, 0 ) ) {
			if ( matches[1].rm_eo > 0 ) {
				val = tok + matches[1].rm_eo;
			}
			if ( matches[3].rm_so > 0 && matches[3].rm_eo > 0 ) {
				if ( 1 != sscanf( tok + matches[3].rm_so, "%u", &chb ) ) {
					chb = -1;
				}
				che = chb;
			}
		}
		if ( ! val || chb < 0 ) {
			fprintf(stderr, "Error -- parseFrontEndParams: '%s' expect <parm>[chnl] '=' <value> pairs ([chnl] is optional, all channels are set when missing)\n", tok);
			goto bail;
		}

		if ( 0 == strncasecmp( tok, "Coup", 4 ) ) {
			switch ( toupper( val[0] ) ) {
				case 'A': iv = 1; break;
				case 'D': iv = 0; break;
				default:
					fprintf(stderr, "Error -- parseFrontEndParams: invalid 'Coupling' value - expect 'AC' or 'DC'\n");
					goto bail;
			}
			while ( chb <= che ) {
				if ( (st = fecSetACMode( scp, chb, iv )) < 0 ) {
					fprintf(stderr, "Error -- setting 'Coupling' failed: %s\n", strerror(-st));
					goto bail;
				}
				++chb;
			}
		} else if ( 0 == strncasecmp( tok, "Term", 4 ) ) {
			iv = scanBool( "Termination", val );
			if ( iv < 0 ) {
				goto bail;
			}
			while ( chb <= che ) {
				if ( (st = fecSetTermination( scp, chb, iv )) < 0 ) {
					fprintf(stderr, "Error -- setting 'Coupling' failed: %s\n", strerror(-st));
					goto bail;
				}
				++chb;
			}
		} else if ( 0 == strncasecmp( tok, "DAC", 3 ) ) {
			iv = scanBool( "DACRangeHigh", val );
			if ( iv < 0 ) {
				goto bail;
			}
			while ( chb <= che ) {
				if ( (st = fecSetDACRangeHi( scp, chb, iv )) < 0 ) {
					fprintf(stderr, "Error -- setting 'DACRangeHigh' failed: %s\n", strerror(-st));
					goto bail;
				}
				++chb;
			}
		} else if ( 0 == strncasecmp( tok, "FECA", 4 ) ) {
			if ( fecGetAttRange( scp, &dv1, &dv2 ) < 0 ) {
				fprintf(stderr, "Error -- Setting FEC Attenuator not supported?\n");
				goto bail;
			}
			iv = scanBool( "FECAttenuator", val );
			if ( iv < 0 ) {
				goto bail;
			}
			while ( chb <= che ) {
				if ( (st = fecSetAtt( scp, chb, iv ? dv2 : dv1 )) < 0 ) {
					fprintf(stderr, "Error -- setting 'FECAttenuator' failed: %s\n", strerror(-st));
					goto bail;
				}
				++chb;
			}
		} else if ( 0 == strncasecmp( tok, "PGAA", 4 ) || 0 == strncasecmp( tok, "Att", 3 )  ) {
			if ( 1 != sscanf( val, "%lg", &dv1 ) ) {
				fprintf(stderr, "Error -- parseFrontEndParams: unable to scan value for '%s' (double expected)\n", tok);
				goto bail;
			}
			while ( chb <= che ) {
				if ( (st = pgaSetAtt( scp, chb, dv1 )) < 0 ) {
					fprintf(stderr, "Error -- setting 'PGAAttenuator' failed: %s\n", strerror(-st));
					goto bail;
				}
				++chb;
			}
		} else {
			fprintf(stderr, "Error -- parseFrontEndParams: invalid operation: '%s'\n", tok);
			goto bail;
		}
	}

	rval = 0;
bail:
	regfree( &chanPat );
	free( str );
	return rval;
}


static int
parseAcqParams(AcqParams *pp, const char *ops)
{
char *str = strdup( ops );
char *ctx;
char *tok;
char *eq;
char *col;
char *val;
int   rval = -1;
long  v[4];
int   badParm = 0;

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

		badParm = 0;

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
				switch ( toupper( tok[1] ) ) {
					case 'D':
						{
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
						}
					break;

					case 'X':
						{
							if ( scanl( tok, eq, &v[0] ) ) {
								goto bail;
							}
							pp->trigOutEn = !!v[0];
							pp->mask     |= ACQ_PARAM_MSK_TGO;
						}
					break;

					default:
						badParm = 1;
					break;
				}
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
				pp->level      = (int16_t) v[0];
				v[0] = 0;
				if ( (col = strchr( eq, ':')) ) {
					if ( scanl( tok, col, &v[0] ) ) goto bail;
				}
                pp->hysteresis = (int16_t)v[0];
				pp->mask |= ACQ_PARAM_MSK_LVL;
				break;
			case 'N':
				if ( strlen(tok) < 2 )  {
			    	fprintf(stderr, "Error -- parseAcqParams: invalid operation: '%s'\n", tok);
					goto bail;
				}
				if ( scanl( tok, eq, &v[0] ) ) goto bail;
				if ( toupper( tok[1] ) == 'P' ) {
					pp->npts      = (uint32_t) v[0];
					pp->mask     |= ACQ_PARAM_MSK_NPT;
				} else {
					pp->nsamples  = (uint32_t) v[0];
					pp->mask     |= ACQ_PARAM_MSK_NSM;
				}
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
				badParm = 1;
				break;
		}
		if ( badParm ) {
		    fprintf(stderr, "Error -- parseAcqParams: invalid operation: '%s'\n", tok);
			goto bail;
		}
	}

	rval = 0;
bail:
	free( str );
	return rval;
}

static int
opReg(FWInfo *fw, const char *op)
{
unsigned    addr, len, val;
uint8_t     buf[256];
int         st;
const char *p;
	if        ( 2 == sscanf(op, "%i:%i", &addr, &len) ) {
		if ( addr >= 256 || len > sizeof(buf) || (addr + len) > 256 ) {
			fprintf(stderr, "Error: invalid register read address or/and length.\n");
			return -1;
		}
		if ( (st = fw_reg_read(fw, addr, buf, len, 0)) < 0 ) {
			fprintf(stderr, "Error: fw_reg_read() failed (%d)\n", st);
			return -1;
		}
		for ( st = 0; st < len; st++ ) {
			printf("0x%02x: 0x%02x\n", addr + st, buf[st]);
		}
	} else if ( 2 == sscanf(op, "%i=%i", &addr, &val) ) {
		buf[0] = val;
		len    = 1;
		for ( p = strchr(op, ','); p; p = strchr(p, ',') ) {
			++p;
			if ( len >= sizeof(buf) ) {
				fprintf(stderr, "Error: too many register values\n");
				return -1;
			}
			if ( 1 != sscanf(p, "%i", &val) ) {
				fprintf(stderr, "Error: unable to scan register value\n");
				return -1;
			}
			if ( val > 255 ) {
				fprintf(stderr, "Error: register value out of range\n");
				return -1;
			}
			buf[len] = val;
			len++;
		}
		if ( addr > 256 || (addr + len ) >= 256 ) {
			fprintf(stderr, "Error: invalid register write address or/and too many values.\n");
			return -1;
		}
		if ( (st = fw_reg_write( fw, addr, buf, len, 0 )) < 0 ) {
			fprintf(stderr, "Error: fw_reg_write() failed (%d)\n", st);
			return -1;
		}
	} else {
		fprintf(stderr, "Error: Unable to parse register operation command\n");
		return -1;
	}
	return 0;
}

static void
printBufInfo(FILE *f, ScopePvt *scp)
{
unsigned long sz = buf_get_size( scp );
uint8_t       fl = buf_get_flags( scp );
int           bs = buf_get_sample_size( scp );
const char *  xt = "";
	if ( bs < 0 ) {
		bs = ( (fl & FW_BUF_FLG_16B) ? 16 : 8 );
	} else if ( bs > 8 ) {
		xt = "; left-adjusted to 16-bit";
	}
	fprintf(f, "ADC Buffer size: %ld (%d-bit%s) samples/channel.\n", sz, bs, xt);
}

int main(int argc, char **argv)
{
const char        *devn;
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

unsigned           sla       = 0;

int                dac       = 0;

int                opt;
int                test_reg  = 0;
char              *test_spi  = 0;
unsigned           flashAddr = FLASHADDR_DFLT;
unsigned          *u_p;
char              *progFile  = 0;
uint8_t           *progMap   = (uint8_t*)MAP_FAILED;
off_t              progSize  = 0;
int                progRdonly= 1;
int                doit      = 0;
int                debug     = 0;
int                fwVersion = 0;
int                dumpAdc   = 0;
int                dumpPrms  = 0;
const char        *trgOp     = 0;
const char        *regOp     = 0;
const char        *feOp      = 0;
AT25Flash         *flash     = 0;
ScopePvt          *scope     = 0;
const char        *h5nam     = NULL;
ScopeH5Data       *h5d       = NULL;
int                h5st      = 0;
const char        *h5comment = NULL;

	if ( ! (devn = getenv( "BBCLI_DEVICE" )) ) {
		devn = "/dev/ttyACM0";
	}

	while ( (opt = getopt(argc, argv, "5:Aa:BC:Dd:Ff:GhIi:P:pR:S:T:Vv!?")) > 0 ) {
		u_p = 0;
		switch ( opt ) {
            case 'h': usage(argv[0]);                                                 return 0;
			default : fprintf(stderr, "Unknown option -%c (use -h for help)\n", opt); return 1;
			case 'C': h5comment = optarg;                                             break;
			case 'd': devn = optarg;                                                  break;
			case 'D': dac  = 1; test_reg = TEST_I2C;                                  break;
			case 'P': feOp = optarg;                                                  break;
			case 'A': dac  = 0; test_reg = TEST_ADC;                                  break;
			case 'G': dac  = 0; test_reg = TEST_FEG;                                  break;
			case 'B': dumpAdc = 1;                                                    break;
			case '5': dumpAdc = 2; h5nam = optarg;                                     break;
			case 'F': dumpAdc = -1;                                                   break;
			case 'p': dumpPrms= 1;                                                    break;
            case 'R': regOp   = optarg;                                               break;
			case 'v': debug++;                                                        break;
			case 'V': fwVersion= 1;                                                   break;
			case 'I': dac = 0; test_reg = TEST_I2C;                                   break;
			case 'i': dac = 0; test_reg = TEST_I2C; u_p = &sla;                       break;
			case 'S': test_spi = strdup(optarg);                                      break;
			case 'T': trgOp    = optarg;                                              break;
			case 'a': u_p      = &flashAddr;                                          break;
			case 'f': progFile = optarg;                                              break;
			case '!': doit     = 1;                                                   break;
			case '?': doit     = (doit <= 0 ? doit - 1 : -1); break;
		}
		if ( u_p && 1 != sscanf(optarg, "%i", u_p) ) {
			fprintf(stderr, "Unable to scan argument to option -%c -- should be a number\n", opt);
			goto bail;
		}
	}

	if ( argc > optind && (1 != sscanf(argv[optind], "%i", &reg) || reg > 0xff) ) {
		fprintf(stderr, "Invalid reg\n");
		goto bail;
	}

	if ( argc > optind + 1 && (1 != sscanf(argv[optind + 1], "%i", &val) || val < 0 || val > 0xffff) ) {
		fprintf(stderr, "Invalid val\n");
		goto bail;
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
		uint8_t  v_api = fw_get_api_version( fw );
		uint8_t  v_brd = fw_get_board_version( fw );
		uint32_t v_git = fw_get_version( fw );

		printf("Firmware version:\n");
		printf("  Git Hash: %08" PRIX32 "\n", v_git);
		printf("  API     : %8"  PRIu8  "\n", v_api);
		printf("  Board HW: %8"  PRIu8  "\n", v_brd);
	}

	if ( trgOp || feOp || dumpPrms || dumpAdc ) {
		if ( (fw_get_features( fw ) & FW_FEATURE_ADC) ) {
			if ( ! (scope = scope_open( fw )) ) {
				fprintf(stderr, "ERROR: scope_open failed\n");
				goto bail;
			}
		}
		if ( ! scope ) {
			fprintf(stderr, "No scope support in firmware; requested operation not supported\n");
			goto bail;
		}
	}

	if ( trgOp ) {
		AcqParams p;
		if ( parseAcqParams( &p, trgOp ) ) {
			goto bail;
		}
		if ( acq_set_params( scope, &p, 0 ) ) {
			fprintf(stderr, "Error: transferring acquisition parameters failed\n");
			goto bail;
		}
	}
	if ( feOp ) {
		parseFrontEndParams( scope, feOp );
	}
	if ( dumpPrms ) {
		AcqParams p;
		p.mask = ACQ_PARAM_MSK_GET;
		if ( acq_set_params( scope, 0, &p ) ) {
			fprintf(stderr, "Error: transferring acquisition parameters failed\n");
			goto bail;
		}
		dumpFrontEndParams( scope );
		printf("Trigger Source     : %s\n",
			CHA == p.src ? "Channel A" : (CHB == p.src ? "Channel B" : "External"));
		printf("Edge               : %s\n", p.rising ? "rising" : "falling");
		printf("Trigger Level      : %" PRId16 "\n", p.level );
		printf("Trigger Hysteresis : %" PRId16 "\n", p.hysteresis );
		printf(" NOTE: Trigger level is int16_t, ADC numbers are normalized to\n");
		printf("       this range!\n");
        printf("External Trigger   : %s\n", p.trigOutEn ? "OUTPUT" : "INPUT");
		printf("N Samples          : %" PRIu32 "\n", p.nsamples  );
		printf("N Pretrig samples  : %" PRIu32 "\n", p.npts      );
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
		printBufInfo( stdout, scope );
	}


	if ( dumpAdc ) {
		int      j;
		uint16_t hdr;
		unsigned long nSamples = buf_get_size( scope );
		uint8_t       fl       = buf_get_flags( scope );
		size_t        reqBufSz = nSamples * scope_get_num_channels( scope ) * sizeof(buf[0]);
		if ( (fl & FW_BUF_FLG_16B) ) {
			reqBufSz *= 2;
		}
		printBufInfo( stderr, scope );
		if ( dumpAdc > 0 ) {
			if ( buflen < reqBufSz ) {
				buflen = reqBufSz;
				buf = realloc(buf, buflen);
				if ( ! buf ) {
					fprintf(stderr, "Error: not enough memory\n");
					goto bail;
				}
			}
			i = buf_read( scope, &hdr, buf, buflen );
		} else {
			i = buf_flush( scope );
		}
		if ( i > 0 ) {
			fprintf(stderr, "ADC Data (got %d, header: 0x%04" PRIx16 ")\n", i, hdr);
			if ( dumpAdc > 1 ) {
				ScopeH5SampleType dtyp = (fl & FW_BUF_FLG_16B) ? INT16LE_T : INT8_T;
				int               ssiz = (INT8_T == dtyp ? sizeof(int8_t) : sizeof(int16_t));
				int               prec = buf_get_sample_size( scope );
				size_t            dims[2];
				if ( prec < 0 ) {
					prec = ssiz*8;
				}
				dims[1] = scope_get_num_channels( scope );
				/* num-samples = nbytes */ 
				dims[0] = i / dims[1] / ssiz;
				h5d = scope_h5_create( h5nam, dtyp, 8*ssiz - prec, dims, sizeof(dims)/sizeof(dims[0]), buf );
				if ( ! h5d ) {
					goto bail;
				}
				h5st = scope_h5_add_bufhdr( h5d, hdr, scope_get_num_channels( scope ) );
				if ( h5st ) {
					goto bail;
				}
				h5st = scope_h5_add_acq_parameters( scope, h5d );
				if ( h5st ) {
					goto bail;
				}
				if ( h5comment ) {
					h5st = scope_h5_add_comment( h5d, h5comment );
					if ( h5st ) {
						goto bail;
					}
				}
			} else {
				if ( (fl & FW_BUF_FLG_16B) ) {
					if ( ( i & 1 ) ) {
						i -= 1;
					}
					for ( j = 0; j < i; j += 2 ) {
						uint16_t w = ( (uint16_t) buf[j+1] << 8 ) | (uint16_t) buf[j];
						printf("0x%04" PRIx16 " ", w);
						if ( 0xe == ( j & 0xf ) ) {
							printf("\n");
						}
					}
				} else {
					for ( j = 0; j < i; j++ ) {
						printf("0x%02" PRIx8 " ", buf[j]);
						if ( 0xf == ( j & 0xf) ) {
							printf("\n");
						}
					}
				}
				if ( (j & 0xf) ) {
					printf("\n");
				}
			}
		} else if ( i < 0 ) {
			if ( -ETIMEDOUT == i ) {
				fw_inv_cmd( fw );
				fprintf(stderr, "Error: buffer-read timeout\n");
			}
			goto bail;
		} else {
			if ( dumpAdc > 0 ) {
				fprintf(stderr, "Info: currently no data on device\n");
			}
		}
	}

	if ( test_spi ) {
		char *wrk;
		char *op;
		if ( ! (flash = at25_open( fw, 0 )) ) {
			fprintf(stderr, "Opening AT25 Flash failed\n");
			goto bail;
		}

		for ( op = test_spi; (op = strtok_r( op, ",", &wrk )); op = 0 /* for strtok_r */  ) {

			if ( strstr(op, "ForceBB") ) {
				fw_disable_features( fw, FW_FEATURE_SPI_CONTROLLER );
			} else if ( strstr(op, "Reset") ) {
				if ( at25_reset( flash ) < 0 ) {
					goto bail;
				}
			} else if ( strstr(op, "Resume") ) {
				if ( at25_resume_updwn( flash ) < 0 ) {
					goto bail;
				}
			} else if ( strstr(op, "Id") ) {
				if ( at25_print_id( flash ) < 0 ) {
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
					if ( progRdonly ) {
						if ( (progMap != (uint8_t*)MAP_FAILED) ) {
							munmap( (void*) progMap, progSize );
							progMap  = (uint8_t*)MAP_FAILED;
							progSize = 0;
						}
						progRdonly = 0;	
					}
					if ( fileMap(progFile,  &progMap, &progSize, i, progRdonly) ) {
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

				if ( ( i = at25_spi_read( flash, flashAddr, maddr, msize ) ) < 0 ) {
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
				if ( (i = at25_status( flash )) < 0 ) {
					goto bail;
				}
				printf("SPI Flash status: 0x%02x\n", i);
			} else if ( strstr(op, "Gunlock") ) {
				if ( at25_global_unlock( flash ) ) {
					goto bail;
				}
			} else if ( strstr(op, "Glock") ) {
				if ( at25_global_lock( flash ) ) {
					goto bail;
				}
			} else if ( strstr(op, "Wena") ) {
				if ( at25_write_ena( flash ) ) {
					goto bail;
				}
			} else if ( strstr(op, "Wdis") ) {
				if ( at25_write_dis( flash ) ) {
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

				if ( fileMap(progFile,  &progMap, &progSize, 0, progRdonly) ) {
					goto bail;
				}

				if ( at25_prog( flash, flashAddr, progMap, progSize, cmd ) < 0 ) {
					fprintf(stderr, "Programming flash failed\n");
					goto bail;
				}
			} else if ( strstr(op, "Erase") ) {

				unsigned aligned;
                unsigned bsz;
				int      st;

				if ( doit < 0 ) {
					printf("Erase: skipping during verify (-?)\n");
					continue;
				}


				if ( progFile ) {
					if ( fileMap(progFile,  &progMap, &progSize, 0, progRdonly) ) {
						goto bail;
					}
					i = progSize;
				} else if ( 1 != sscanf(op, "Erase%i", &i) ) {
					fprintf(stderr, "Skipping '%s' -- expected format 'Erase<xxx>' with xxx a number\n", op);
					continue;
				}

				/* biggest block that aligns to flashAddr */
                bsz     = algnblk( flashAddr );
                if ( i < bsz ) {
					/* if we need to erase less try to find a smaller block */
					bsz = sz2bsz( i );
				}
				/* up-align end */
                i = ( flashAddr + i + bsz - 1) & ~ (bsz - 1);
				/* down-align start */
                aligned = flashAddr & ~ (bsz - 1);

				printf("Erasing 0x%x/%d bytes from address 0x%x\n", i - aligned, i - aligned, aligned);

				if ( doit <= 0 ) {
					printf("... bailing out -- please use -! to proceed or -? to just verify the flash\n");
					continue;
				}

				if ( (st = at25_status( flash )) < 0 ) {
					fprintf(stderr, "at25_status() failed\n");
					goto bail;
				}

				if ( 0 == ( st & AT25_ST_WEL ) ) {
					fprintf(stderr, "Unable to erase; write-protection still engaged (use Wena?)\n");
					goto bail;
				}

				while ( aligned < i ) {

					if ( at25_global_unlock( flash ) ) {
						fprintf(stderr, "at25_global_unlock() failed\n");
						goto bail;
					}

					if ( at25_block_erase( flash, aligned, bsz ) < 0 ) {
						fprintf(stderr, "at25_block_erase(%d) failed\n", i);
						goto bail;
					}
					printf("."); fflush(stdout);

					aligned += bsz;

				}
				printf("\n");
			} else {
				fprintf(stderr, "Skipping unrecognized SPI command '%s'\n", op);
			}

		}

	}

    if ( regOp ) {
		if ( opReg( fw, regOp ) ) {
			goto bail;
		}
	}


	if ( test_reg ) {

		switch ( test_reg ) {

			case TEST_I2C:

				if ( dac ) {
					sla = 0xc2;
				} else if ( 0 == sla ) {
					sla = 0xd4;
				} else {
					/* they provided address; we convert into i2c cmd */
					sla <<= 1;
				}

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
							fprintf(stderr, "bb_i2c_%s_reg failed: %s\n", val < 0 ? "read" : "write", strerror(-i));
							goto bail;
						}
						if ( val < 0 ) {
							buf[0] = (uint8_t) i ;
							rdl    = 1;
						}
					}
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

			case TEST_FEG:
				if ( val < 0 ) {
					buf[0] = fegRegRead( fw );
				} else {
					buf[0] = val;
					fegRegWrite( fw, buf[0] );
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
	if ( flash ) {
		at25_close( flash );
	}
	if ( h5d ) {
		scope_h5_close( h5d );
		if ( h5st ) {
			unlink( h5nam );
		}
	}
	if ( scope ) {
		scope_close( scope );
	}
	if ( fw ) {
		fw_close( fw );
	}
	if ( (void*)progMap != MAP_FAILED ) {
		munmap( (void*)progMap, progSize );
	}
	if ( buf ) {
		free( buf );
	}
	if ( test_spi ) {
		free( test_spi );
	}
	return rval;
}
