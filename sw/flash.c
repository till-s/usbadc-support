#include <flash.h>
#include <at25Sup.h>
#include <errno.h>
#include <fwUtil.h>
#include <unistd.h>
#include <string.h>

void
flash_stdio_progress_data_init(FlashStdioProgressData *pd) {
	memset(pd, 0, sizeof(*pd));
	pd->fp = stdout;
}

int
flash_stdio_progress(void *flash, void *closure, int flag, unsigned addr, unsigned remain)
{
	FlashStdioProgressData *pd = closure;
	if ( !! (flag & FLASH_ERASE ) ) {
		if ( pd->iter < 0 ) {
			fprintf(pd->fp, "Erasing 0x%x/%d bytes from address 0x%x\n", remain, remain, addr);
			if ( pd->iter < -1 ) {
				return -EACCES;
			}
		} else {
			fprintf(pd->fp, "e"); fflush(stdout);
		}
	} else {
		if ( pd->iter >= 0 ) {
			if ( !! (flag & FLASH_CHECK_ERASED) ) {
				fprintf(pd->fp, "z"); fflush(stdout);
			}
			if ( !! (flag & FLASH_PROGRAM) ) {
				fprintf(pd->fp, "."); fflush(stdout);
			}
			if ( !! (flag & FLASH_CHECK_VERIFY) ) {
				fprintf(pd->fp, "v"); fflush(stdout);
			}
		}
	}
	pd->iter++;
	if ( 0 == remain || (pd->iter > 0 && (0 == pd->iter % 64))) {
		fprintf(pd->fp, "\n");
	}
	if ( 0 == remain ) {
		pd->iter = -1;
	}
	return 0;
}

int
flash_write_from_file(FWInfo *fw, const char *filename, unsigned flashAddr, FlashProgress progress, void *progressState)
{
uint8_t     *map = NULL;
off_t         sz = 0;
AT25Flash *flash = NULL;
int     status, flags;

	if ( (status = fileMap(filename,  &map, &sz, 0, 1)) ) {
		goto bail;
	}
	if ( (status = at25_open1( fw, &flash, 0 )) ) {
		goto bail;
	}

	if ( (status = at25_write_ena( flash )) ) {
		goto bail;
	}

	status = at25_area_erase( flash, flashAddr, sz, progress, progressState );
	if ( status < 0 ) {
		goto bail;
	}
	flags = ( AT25_CHECK_ERASED | AT25_EXEC_PROG | AT25_CHECK_VERIFY );
	status = at25_prog( flash, flashAddr, map, sz, flags, progress, progressState );
	if ( status < 0 ) {
		goto bail;
	}
	status = 0;
bail:
	if ( flash ) {
		at25_write_dis( flash );
		at25_close( flash );
	}
	if ( map ) {
		fileUnmap( map, sz );
	}
	return status;
}

int
flash_read_to_file(FWInfo *fw, const char *filename, unsigned flashAddr, unsigned size)
{
uint8_t     *map = NULL;
off_t         sz = 0;
AT25Flash *flash = NULL;
int     status;
	if ( (status = fileMap(filename,  &map, &sz, size, 0)) ) {
		goto bail;
	}
	if ( (status = at25_open1( fw, &flash, 0 )) ) {
		goto bail;
	}

	status = at25_spi_read(flash, flashAddr, map, sz);
	if ( status > 0 ) {
		status = 0;
	}

bail:
	if ( flash ) {
		at25_close( flash );
	}
	if ( map ) {
		fileUnmap( map, sz );
		/* unlink only if map succeeded; don't unlink an existing file to which we could not write! */
		if ( status < 0 ) {
			unlink( filename );
		}
	}
	return status;
}
