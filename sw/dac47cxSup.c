
#include "dac47cxSup.h"
#include "fwComm.h"
#include <math.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

/* could go into the FWInfo struct */
#define SLA 0xc2

int
dac47cxReadReg(FWInfo *fw, unsigned reg, uint16_t *val)
{
uint8_t     buf[2];
int         st;

	buf[0] = SLA;
	buf[1] = ( 0x06 | ( (reg & 0x1f) << 3 ) );
	if ( (st = bb_i2c_start( fw, 0      )) < 0 ) return st;
	if ( (st = bb_i2c_write( fw, buf, 2 )) < 0 ) goto bail;
	if ( 0 == st ) {
		/* NAK on first byte */
		st = -ENODEV;
		goto bail;
	}
	if ( (st = bb_i2c_start( fw, 1      )) < 0 ) goto bail;
	buf[0] = SLA | I2C_READ;
	if ( (st = bb_i2c_write( fw, buf, 1 )) < 0 ) goto bail;
	if ( (st = bb_i2c_read ( fw, buf, 2 )) < 0 ) goto bail;

	st = 0;

bail:
	bb_i2c_stop ( fw );
	*val = ( buf[0] << 8 ) | buf[1];
	return st;
}

int
dac47cxWriteReg(FWInfo *fw, unsigned reg, uint16_t val)
{
uint8_t     buf[4];
int         st;

	buf[0] = SLA;
	buf[1] = ( 0x00 | ( (reg & 0x1f) << 3 ) );
	buf[2] = ( val >> 8 ) & 0xff;
	buf[3] = ( val >> 0 ) & 0xff;
	if ( (st = bb_i2c_start( fw, 0      )) < 0 ) return st;
	if ( (st = bb_i2c_write( fw, buf, 4 )) < 0 ) return st;
	if ( 0 == st ) {
		/* NAK on first byte */
		st = -ENODEV;
		goto bail;
	}
	st = 0;
bail:
	bb_i2c_stop ( fw );
	return st;
}

int
dac47cxReset(FWInfo *fw)
{
uint8_t     buf[2];
int         st;

	buf[0] = 0x00; /* special address */
	buf[1] = 0x06;
	if ( (st = bb_i2c_start( fw, 0 ))       < 0 ) return st;
	if ( (st = bb_i2c_write( fw, buf, 2 ))  < 0 ) goto bail;
	if ( 0 == st ) {
		/* NAK on first byte */
		st = -ENODEV;
		goto bail;
	}
	st = 0;

bail:
	bb_i2c_stop ( fw );
	return st;
}

#define REG_VREF 8
#define REG_VAL0 0
#define REG_VAL1 1

#define VREF_VDD          0x0 /* VDD; unbuffered, Vref buffer disabled  */
#define VREF_INTERNAL     0x1 /* internal band-gap, Vref buffer enabled */
#define VREF_EXT_NOBUF    0x2 /* Vref pin, unbuffered (buffer disabled) */
#define VREF_EXT_BUF      0x3 /* Vref pin, buffered (buffer enabled)    */

#define VREF_CH0(choice)  ((choice)<<0)
#define VREF_CH1(choice)  ((choice)<<2)

int
dac47cxDetectMax(FWInfo *fw)
{
uint16_t val;
int      st;

	if ( (st = dac47cxReset( fw ))          < 0 ) return st;
	if ( (st = dac47cxGet  ( fw, 0, &val )) < 0 ) return st;
	return ((val + 1) << 1) - 1;
}

int
dac47cxSet(FWInfo *fw, unsigned channel, int val)
{
int maxDac = 0;
int st;

	if ( channel > 1 ) {
		fprintf(stderr, "Error -- dac47cxSet(): invalid channel\n");
		return -EINVAL;
	}

	maxDac = 0xfff;

	if ( val > maxDac ) {
		fprintf(stderr,"Warning -- dac47cxSet(): big value clipped to %d (or less if not a 12-bit model)\n", maxDac);
		val = maxDac;
	}
	if ( val < 0      ) {
		fprintf(stderr,"Warning -- dac47cxSet(): small value clipped to %d\n", 0);
		val = 0;
	}
	if ( (st = dac47cxWriteReg( fw, REG_VAL0 + channel, val )) < 0 ) return st;
	return 0;
}

int
dac47cxGet(FWInfo *fw, unsigned channel, uint16_t *val)
{
int st;
	if ( channel > 1 ) {
		fprintf(stderr, "Error -- dac47cxGet(): invalid channel\n");
		return -EINVAL;
	}
	if ( (st = dac47cxReadReg( fw, REG_VAL0 + channel, val )) < 0 ) return st;
	return 0;
}

int
dac47cxSetRefSelection(FWInfo *fw, DAC47CXRefSelection sel)
{
int st;
	switch ( sel ) {
		case DAC47XX_VREF_INTERNAL_X1:
			/* select internal bandgap (leave at gain 1)
			 * according to datasheet, if we use the internal
			 * bandgap then
			 *  - all channels must use it
			 *  - select on ch0
			 *  - other channels must be in external, buffered mode
			 */
			if ( (st = dac47cxWriteReg(fw, REG_VREF, VREF_CH1(VREF_EXT_BUF) | VREF_CH0(VREF_INTERNAL))) < 0 ) {
				return st;
			}
			break;
		default:
			fprintf(stderr, "Error -- dac47cxSetRefSelection(): invalid/unsupported choice\n");
			return -ENOTSUP;
	}
	return 0;
}
