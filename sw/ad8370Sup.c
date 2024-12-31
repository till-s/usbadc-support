#include <stdint.h>

#include <stdio.h>
#include <math.h>
#include <errno.h>

#include "ad8370Sup.h"
#include "fwComm.h"
#include "scopeSup.h"

int
ad8370Write(FWInfo *fw, int channel, uint8_t val)
{
uint8_t buf[2];
int     st;
	buf[0] = fw_spireg_cmd_write( channel );
	buf[1] = val;
	if ( (st = bb_spi_xfer( fw, SPI_MODE0, SPI_PGA, buf, 0, 0, sizeof(buf)) ) < 0 ) {
		return st;
	}
	return 0;
}

int
ad8370Read(FWInfo *fw, int channel)
{
uint8_t buf[2];
int     st;
	buf[0] = fw_spireg_cmd_read( channel );
	buf[1] = 0xff;
	if ( (st = bb_spi_xfer( fw, SPI_MODE0, SPI_PGA, buf, buf, 0, sizeof(buf)) ) < 0 ) {
		return st;
	}
	return (int)buf[1];
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
		return -EINVAL;
	}
	if ( att < 0.0 ) {
		fprintf(stderr, "ad837028SetAtt: value out of range (0..20)\n");
		return -EINVAL;
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

/*
	printf("ATT: ch %d, att %f, gain %f, cod 0x%02x\n", channel, att, gain, v);
 */

	return ad8370Write( fw, channel, v );
}

float
ad8370GetAtt(FWInfo *fw, unsigned channel)
{
int     v  = ad8370Read( fw, channel );
int     hi;
double  gain, att;
uint8_t cod;

	if ( v < 0 )
		return 0./0.;

	cod = v & 0x7f;
	if ( 0 == cod ) {
		/* artificial max */
		return 65.0;
	}

	hi = ((v & 0x80) ? 1 : 0 );

    gain = VERNIER * ((double)cod);
	if ( hi ) {
    	gain *= PREGAIN;
	}

    att = 34.0 - 20.0*log10( gain );

/*
	printf("reg: %x; gain %g, att %g\n", v, gain, att);
 */
	
	return (float)att;
}

static	int
opReadReg(FWInfo *fw, unsigned ch, unsigned reg)
{
	return ad8370Read( fw, reg );
}

static	int
opWriteReg(FWInfo *fw, unsigned ch, unsigned reg, unsigned val)
{
	return ad8370Write( fw, reg, val );
}

static	int
opGetAttRange(FWInfo *fw, double *min, double *max)
{
	if ( min ) *min = 0;
	if ( max ) *max = 40;
	return 0;
}

static	int
opGetAtt(FWInfo *fw, unsigned channel, double *att)
{
	double val = (double)ad8370GetAtt( fw, channel );
	if ( isnan( val ) ) {
		return -EINVAL;
	}
	if ( val < 0.0 ) {
		return (int)val;
	}
	if ( att ) *att = val;
	return 0;
}

static	int
opSetAtt(FWInfo *fw, unsigned channel, double att)
{
	return ad8370SetAtt( fw, channel, (float)att );
}

PGAOps ad8370PGAOps = {
	.readReg     = opReadReg,
	.writeReg    = opWriteReg,
	.getAttRange = opGetAttRange,
	.getAtt      = opGetAtt,
	.setAtt      = opSetAtt
};
