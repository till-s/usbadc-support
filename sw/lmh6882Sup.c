#include <stdint.h>

#include <stdio.h>
#include <math.h>

#include "lmh6882Sup.h"
#include "fwComm.h"

int
lmh6882ReadReg(FWInfo *fw, uint8_t reg)
{
uint8_t buf[2];
	buf[0] = 0x80 | ( reg & 0xf );
	buf[1] = 0x00;
	if ( bb_spi_xfer( fw, SPI_MODE0, SPI_PGA, buf, buf, 0, sizeof(buf) ) < 0 ) {
		return -1;
	}
	return buf[1];	
}

int
lmh6882WriteReg(FWInfo *fw, uint8_t reg, uint8_t val)
{
uint8_t buf[2];
	buf[0] = 0x00 | ( reg & 0xf );
	buf[1] = val;
	if ( bb_spi_xfer( fw, SPI_MODE0, SPI_PGA, buf, buf, 0, sizeof(buf) ) < 0 ) {
		return -1;
	}
	return 0;
}

#define PGA_REG_PWR_CTL 2
#define PGA_REG_ATT_CHA 3
#define PGA_REG_ATT_CHB 4
#define PGA_PWR_MASK    0x3c

/* Attenuation in dB or negative value on error */
float
lmh6882GetAtt(FWInfo *fw, unsigned channel)
{
int v;
	if ( channel > 1 ) {
		return -2.0;
	}
	if ( ( v = lmh6882ReadReg( fw, PGA_REG_ATT_CHA + channel ) ) < 0 ) {
		return -1.0;
	}
	return ((float)v)/4.0;
}

int
lmh6882SetAtt(FWInfo *fw, unsigned channel, float att)
{
uint8_t v;
	if ( channel > 1 ) {
		return -2;
	}
	if ( att < 0.0 || att > 20.0 ) {
		fprintf(stderr, "lmh6228SetAtt: value out of range (0..20)\n");
		return -2;
	}
	v = round( att * 4.0 );
	return lmh6882WriteReg( fw, PGA_REG_ATT_CHA + channel, v );
}

/* RETURNS: previous power state (on: 1, off: 0) or -1 on error.
 *          Sets new power state: on (state > 0), off (state == 0),
 *          unchanged (state < 0).
 *
 *          Note that all register bits are returned (see datasheet);
 *          nonzero: some stages on, zero: power off.
 */
int
lmh6882Power(FWInfo *fw, int state)
{
int v, ov;

	ov = lmh6882ReadReg( fw, PGA_REG_PWR_CTL );
	if ( ov < 0 ) {
		return ov;
	}
	if ( state >= 0 ) {
		v = ov & ~ PGA_PWR_MASK;
		if ( state ) {
			v |= PGA_PWR_MASK;
		}
		if ( lmh6882WriteReg( fw, PGA_REG_PWR_CTL, v ) < 0 ) {
			return -1;
		}
	}

	return ( ov & PGA_PWR_MASK );
}
