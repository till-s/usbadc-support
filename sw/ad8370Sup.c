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
	if ( bb_spi_xfer( fw, (channel ? SPI_VGB : SPI_VGA), buf, 0, 0, sizeof(buf) ) < 0 ) {
		return -1;
	}
	return 0;
}

int
ad8370Read(FWInfo *fw, int channel)
{
uint8_t rbuf[1];
uint8_t zbuf[1];
	zbuf[0] = 0xff;
	if ( bb_spi_xfer( fw, (channel ? SPI_VGB : SPI_VGA), zbuf, rbuf, zbuf, sizeof(rbuf) ) < 0 ) {
		return -1;
	}
	return (int)rbuf[0];
}


static const double VERNIER = 0.055744; /* magic values from datasheet */
static const double PREGAIN = 7.079458;

static const double MAXGAIN = 34.0; /* dB */

int
ad8370SetAtt(FWInfo *fw, unsigned channel, float att)
{
uint8_t        v;

/* Max. gain is 34dB */
double         gain    =  pow( 10.0,  (MAXGAIN - att) / 20.0 );
unsigned       cod;

	if ( channel > 1 ) {
		return -2;
	}
	if ( att < 0.0 ) {
		fprintf(stderr, "ad837028SetAtt: value out of range (0..20)\n");
		return -2;
	}

	if ( att >= 65.0 ) {
		gain = 0.0;
	}

	if ( gain > 127.0*VERNIER ) {
		v   = 0x80;
		cod = round( gain/VERNIER/(1.0 + (PREGAIN - 1.0)) );
	} else {
		v   = 0x00;
		cod = round( gain/VERNIER );
	}
	if ( cod > 127 ) {
		cod = 127;
	}
	v |= cod;

	printf("ATT: ch %d, att %f, gain %f, cod 0x%02x\n", channel, att, gain, v);

	return ad8370Write( fw, channel, v );
}

float
ad8370GetAtt(FWInfo *fw, unsigned channel)
{
int    v  = ad8370Read( fw, channel );
int    hi;
double gain;

	if ( v < 0 )
		return 0./0.;

	if ( ( v & 0x7f) == 0 ) {
		/* artificial max */
		return 65.0;
	}

	hi = ((v & 0x80) ? 1 : 0 );

    gain = VERNIER * ((double)(v & 0x7f));
	if ( hi ) {
    	gain *= PREGAIN;
	}
	
    return 34.0 - 20.0*log10( gain );
}
