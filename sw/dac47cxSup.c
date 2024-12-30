
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

/* ugly but we don't have context implemented for now and there
 * is only a single device..
 *
 * Initialize from board/hw version :-( unfortunately we can't detect
 * the device type w/o resetting it which we want to avoid
 * in order to not cause glitches.
 */
static int _dacMax = 0x00;

static int dacMax(FWInfo *fw)
{
	if ( 0 == _dacMax ) {
		/* horrible hack for now... */
		switch ( fw_get_board_version( fw ) ) {
			case 0:
				_dacMax = 0xff;  break;
			case 1:
			case 2:
				_dacMax = 0xfff; break;
			default:
				fprintf(stderr, "Error -- dac47cxGetRange(): unknown board version\n");
				abort();
				return 0;
		}
	}
	return _dacMax;
}

int
dac47cxDetectMax(FWInfo *fw)
{
uint16_t val;
int      st;

	if ( (st = dac47cxReset( fw ))          < 0 ) return st;
	if ( (st = dac47cxGet  ( fw, 0, &val )) < 0 ) return st;
	_dacMax = val;
	_dacMax = ((_dacMax + 1) << 1) - 1;
	return _dacMax;
}

/* Analog circuit:
 *  Board version 0:
 *   Vamp = - Vref/2 + Vdac
 *  Board version 1,2 (hi-range):
 *   Vamp = ( Vref/2 - Vdac ) / 2
 */

/* Board V1 */
#define VOLT_REF   1.214
#define VOLT_MIN   (-VOLT_REF/2.0)

static double tick2Volt(FWInfo *fw, int tick, int maxDac)
{
	double volt = (VOLT_MIN + VOLT_REF * ((double)(tick)) / (double)(maxDac + 1));
	switch ( fw_get_board_version( fw ) ) {
		case 0:
		break;

		case 1: /* fall through */
		case 2: volt *= -0.5;
		break;

		default:
			fprintf(stderr, "dac47cx tick2Volt: unsupported board version\n");
			abort();
	}
	return volt;
}

static int volt2Tick(FWInfo *fw, double volt, int maxDac)
{
	int tick;

	switch ( fw_get_board_version( fw ) ) {
		case 0:
		break;

		case 1: /* fall through */
		case 2: volt *= -2.0;
		break;

		default:
			fprintf(stderr, "dac47cx volt2Tick: unsupported board version\n");
			abort();
	}

	tick = round( (volt - VOLT_MIN)/VOLT_REF * (double)(maxDac + 1) );
	if ( tick < 0 ) {
		tick = 0;
	}
	if ( tick > maxDac ) {
		tick = maxDac;
	}
	return tick;
}

void
dac47cxGetRange(FWInfo *fw, int *tickMin, int *tickMax, float *voltMin, float *voltMax)
{
int maxDac = dacMax(fw);

	double voltLo = tick2Volt( fw,      0, maxDac );
	double voltHi = tick2Volt( fw, maxDac, maxDac );

	if ( tickMin ) *tickMin = 0;
	if ( tickMax ) *tickMax = maxDac;
	if ( voltMin ) *voltMin = voltLo < voltHi ? voltLo : voltHi;
	if ( voltMax ) *voltMax = voltHi > voltLo ? voltHi : voltLo;
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

	maxDac = dacMax(fw);

	if ( val > maxDac ) {
		fprintf(stderr,"Warning -- dac47cxSet(): big value clipped to %d\n", maxDac);
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
dac47cxSetVolt(FWInfo *fw, unsigned channel, float val)
{
int ival, maxDac = dacMax(fw);
float voltMin, voltMax;

	dac47cxGetRange(fw, NULL, NULL, &voltMin, &voltMax);

	if ( val < voltMin ) {
		fprintf(stderr, "dac47cxSetVolt(): value out of range; clipping to %f\n", voltMin);
		val = voltMin;
	}
	if ( val > voltMax ) {
		fprintf(stderr, "dac47cxSetVolt(): value out of range; clipping to %f\n", voltMax);
		val = voltMax;
	}


	ival = volt2Tick( fw, val, maxDac );

	return dac47cxSet( fw, channel, ival );
}

int
dac47cxGetVolt(FWInfo *fw, unsigned channel, float *valp)
{
uint16_t val;
int      maxDac = dacMax(fw);
int      st;

	if ( channel > 1 ) {
		fprintf(stderr, "dac47xxGetVolt(): invalid channel\n");
		return -EINVAL;
	}
	if ( (st = dac47cxGet( fw, channel, &val )) < 0 ) return st;

	*valp = tick2Volt( fw, val, maxDac );

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
