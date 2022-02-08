#include "fegRegSup.h"

int
fegRegRead(FWInfo *fw)
{
uint8_t v;
uint8_t z = 0xff;
int     rv;
	rv = bb_spi_xfer( fw, SPI_FEG, 0, &v, &z, sizeof(v) );
	return rv < 0 ? rv : v;
}

int
fegRegWrite(FWInfo *fw, uint8_t v)
{
uint8_t z = 0x00;
int     rv;
	rv = bb_spi_xfer( fw, SPI_FEG, &v, 0, &z, sizeof(v) );
	return rv < 0 ? rv : 0;
}
