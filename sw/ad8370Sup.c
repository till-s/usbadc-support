#include <stdint.h>

#include <stdio.h>
#include <math.h>

#include "ad8370Sup.h"
#include "fwComm.h"

int
ad8370Write(FWInfo *fw, int channel, uint8_t val)
{
uint8_t buf[1];
	buf[0] = val;
	if ( bb_spi_xfer( fw, (channel ? SPI_VGB : SPI_VGA), buf, buf, 0, sizeof(buf) ) < 0 ) {
		return -1;
	}
	return 0;
}

int
ad8370SetAtt(FWInfo *fw, unsigned channel, float att)
{
uint8_t        v;
const   double vernier = 0.055744; /* magic values from datasheet */
const   double preGain = 7.079458;

/* Max. gain is 34dB */
double         gain    =  pow( 10.0,  (34.0 - att) / 20.0 );
unsigned       cod;

	if ( channel > 1 ) {
		return -2;
	}
	if ( att < 0.0 || att > 40.0 ) {
		fprintf(stderr, "ad837028SetAtt: value out of range (0..20)\n");
		return -2;
	}

	if ( gain > 127.0*vernier ) {
		v   = 0x80;
		cod = round( gain/vernier/(1.0 + (preGain - 1.0)) );
	} else {
		v   = 0x00;
		cod = round( gain/vernier );
	}
	if ( cod > 127 ) {
		cod = 127;
	}
	v |= cod;

	printf("ATT: ch %d, att %f, gain %f, cod 0x%02x\n", channel, att, gain, v);

	return ad8370Write( fw, channel, v );
}
