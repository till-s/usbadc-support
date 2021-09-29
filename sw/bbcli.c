#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <string.h>
#include <getopt.h>

#include "fwComm.h"

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
	printf("   -A                 : dump ADC buffer (raw).\n");
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

int main(int argc, char **argv)
{
const char        *devn      = "/dev/ttyUSB0";
FWInfo            *fw        = 0;
int                fd        = -1;
int                rval      = 1;
unsigned           speed     = 115200; /* not sure this really matters */
uint8_t           *buf       = 0;
unsigned           buflen    = 8192;
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
int                debug     = 0;
int                fwVersion = 0;
int                dumpAdc   = 0;

	while ( (opt = getopt(argc, argv, "hvAVd:DIS:a:f:!?")) > 0 ) {
		u_p = 0;
		switch ( opt ) {
            case 'h': usage(argv[0]);             return 0;
			default : fprintf(stderr, "Unknown option -%c (use -h for help)\n", opt); return 1;
			case 'd': devn = optarg;              break;
			case 'D': dac  = 1; test_i2c = 1;     break;
			case 'A': dumpAdc = 1;                break;
			case 'v': debug++;                    break;
			case 'V': fwVersion= 1;               break;
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

	if ( (fd = fifoOpen( devn, speed ) ) < 0 ) {
		goto bail;
	}

	if ( debug > 2 ) {
		fifoSetDebug( 1 );
	}

	if ( test_spi || test_i2c ) {
		if ( ! (fw = fw_open_fd(fd, BB_MODE_SPI)) ) {
			goto bail;
		}
		fw_set_debug( fw, debug );
	}

	if ( ! (buf = malloc(buflen)) ) {
		perror("No memory");
		goto bail;
	}

	if ( fwVersion ) {
		uint8_t cmd = fw_get_cmd( FW_CMD_VERSION );
		int     j;
		i = fifoXferFrame( fd, &cmd, 0, 0, buf, buflen );	
		if ( i > 0 ) {
			printf("Firmware version (cmd ret: 0x%02" PRIx8 ")\n", cmd);
			for ( j = 0; j < i; j++ ) {
				printf("0x%02" PRIx8 " ", buf[j]);
			}
			printf("\n");
		}
	}

	if ( dumpAdc ) {
		uint8_t cmd = 0x02;
		int     j;
		i = fifoXferFrame( fd, &cmd, 0, 0, buf, buflen );
		if ( i > 0 ) {
			printf("ADC Data (cmd ret: 0x%02" PRIx8 ")\n", cmd);
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

				if ( ( i = bb_spi_read( fw, flashAddr, maddr, msize ) ) < 0 ) {
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


	if ( test_i2c ) {

		sla = dac ? 0xc2 : 0xd4;

		fw_set_mode( fw, BB_MODE_I2C );

		bb_i2c_start( fw, 0 );

		if ( reg < 0 && dac ) {
			/* reset */
			buf[0] = 0x00;
			buf[1] = 0x06;
			bb_i2c_write( fw, buf, 2 );
		} else {
			if ( val < 0 ) {
				/* read */
				buf[0] = sla;
				buf[1] = dac ? ( 0x06 | ((reg&0x1f) << 3) ) : reg;
				bb_i2c_write( fw, buf, 2 );
				bb_i2c_start( fw, 1 );
				buf[0] = sla | I2C_READ;
				bb_i2c_write( fw, buf, 1 );
				rdl    = dac ? 2 : 1;
				bb_i2c_read( fw, buf, rdl );
			} else {
				wrl    = 0;
				buf[wrl++] = sla;
				buf[wrl++] = dac ? ( 0x00 | ((reg&0x1f) << 3) ) : reg;
				if ( dac ) {
					buf[wrl++] = (val >> 8) & 0xff;
				}
				buf[wrl++] = (val >> 0) & 0xff;
				bb_i2c_write( fw, buf, wrl );
			}
		}
		bb_i2c_stop( fw );

		if ( rdl ) {
			printf("reg: 0x%x: 0x", reg);
			for ( i = 0; i < rdl; i++ ) {
				printf("%02x", buf[i]);
			}
			printf("\n");
		}

		fw_set_mode( fw, BB_MODE_SPI );
	}

	rval = 0;

bail:
	if ( fw ) {
		fw_close( fw );
	}
	/* must close the fifo only AFTER FWInfo */
	if ( fd >= 0 ) {
		fifoClose( fd );
	}
	if ( (void*)progMap != MAP_FAILED ) {
		munmap( (void*)progMap, progSize );
	}
	if ( buf ) {
		free( buf );
	}
	return rval;
}
