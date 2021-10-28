#ifndef USBADC_DAC47CX_SUP
#define USBADC_DAC47CX_SUP

#include "dac47cxSup.h"
#include "fwComm.h"
#include <math.h>
#include <stdio.h>

/* could go into the FWInfo struct */
#define SLA 0xc2

uint16_t
dac47cxReadReg(FWInfo *fw, unsigned reg)
{
uint8_t     buf[2];

	buf[0] = SLA;
	buf[1] = ( 0x06 | ( (reg & 0x1f) << 3 ) );
	bb_i2c_start( fw, 0      );
	bb_i2c_write( fw, buf, 2 );
	bb_i2c_start( fw, 1      );
	buf[0] = SLA | I2C_READ;
	bb_i2c_write( fw, buf, 1 );
	bb_i2c_read ( fw, buf, 2 );
	bb_i2c_stop ( fw         );
	return ( buf[0] << 8 ) | buf[1];
}

void
dac47cxWriteReg(FWInfo *fw, unsigned reg, uint16_t val)
{
uint8_t     buf[4];

	buf[0] = SLA;
	buf[1] = ( 0x00 | ( (reg & 0x1f) << 3 ) );
	buf[2] = ( val >> 8 ) & 0xff;
	buf[3] = ( val >> 0 ) & 0xff;
	bb_i2c_start( fw, 0      );
	bb_i2c_write( fw, buf, 4 );
	bb_i2c_stop ( fw         );
}

void
dac47cxReset(FWInfo *fw)
{
uint8_t     buf[2];

	buf[0] = 0x00; /* special address */
	buf[1] = 0x06;
	bb_i2c_start( fw, 0 );
	bb_i2c_write( fw, buf, 2 );
	bb_i2c_stop ( fw         );
}

#define REG_VREF 8
#define REG_VAL0 0
#define REG_VAL1 1

#define VREF_INTERNAL_CH0 0x01
#define VREF_INTERNAL_CH1 0x04

/* ugly but we don't have context implemented for now and there
 * is only a single device..
 */
static int dacMax = 0;

void
dac47cxInit(FWInfo *fw)
{
	dac47cxReset( fw );
	dacMax = (dac47cxReadReg( fw, REG_VAL0 ) + 1) << 1;
	dac47cxWriteReg( fw, REG_VREF, VREF_INTERNAL_CH0 | VREF_INTERNAL_CH1 );
}

#define VOLT_MAX (1.214/2.0)
#define VOLT_MIN (-(VOLT_MAX))

void
dac46cxGetRange(int *tickMin, int *tickMax, float *voltMin, float *voltMax)
{
	if ( 0 == dacMax ) {
		fprintf(stderr, "Error -- dac47cxGetRange(): unknown -- DAC not initialized?\n");
	}
	if ( tickMin ) *tickMin = 0;
	if ( tickMax ) *tickMax = dacMax;
	if ( voltMin ) *voltMin = VOLT_MIN;
	if ( voltMax ) *voltMax = VOLT_MAX;
}

void
dac47cxSet(FWInfo *fw, unsigned channel, int val)
{
	if ( channel > 1 ) {
		fprintf(stderr, "Error -- dac47cxSet(): invalid channel\n");
		return;
	}
	if ( val > dacMax ) {
		fprintf(stderr,"Warning -- dac47cxSet(): big value clipped to %d\n", dacMax);
		val = dacMax;
	}
	if ( val < 0      ) {
		fprintf(stderr,"Warning -- dac47cxSet(): small value clipped to %d\n", 0);
		val = 0;
	}
	dac47cxWriteReg( fw, REG_VAL0 + channel, val );
}

void
dac47cxSetVolt(FWInfo *fw, unsigned channel, float val)
{
int ival = round(val/(2.0*VOLT_MAX) - (VOLT_MIN/VOLT_MAX));

	if ( -1         == ival ) ival = 0;
	if ( dacMax + 1 == ival ) ival = dacMax;
	dac47cxSet( fw, channel, ival );
}

#ifdef __cplusplus
}
#endif

#endif
