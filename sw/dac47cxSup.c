
#include "dac47cxSup.h"
#include "fwComm.h"
#include <math.h>
#include <stdio.h>

/* could go into the FWInfo struct */
#define SLA 0xc2

int
dac47cxReadReg(FWInfo *fw, unsigned reg, uint16_t *val)
{
uint8_t     buf[2];

	buf[0] = SLA;
	buf[1] = ( 0x06 | ( (reg & 0x1f) << 3 ) );
	if ( bb_i2c_start( fw, 0      ) < 0 ) return -1;
	if ( bb_i2c_write( fw, buf, 2 ) < 0 ) return -1;
	if ( bb_i2c_start( fw, 1      ) < 0 ) return -1;
	buf[0] = SLA | I2C_READ;
	if ( bb_i2c_write( fw, buf, 1 ) < 0 ) return -1;
	if ( bb_i2c_read ( fw, buf, 2 ) < 0 ) return -1;
	if ( bb_i2c_stop ( fw         ) < 0 ) return -1;
	*val = ( buf[0] << 8 ) | buf[1];
	return 0;
}

int
dac47cxWriteReg(FWInfo *fw, unsigned reg, uint16_t val)
{
uint8_t     buf[4];

	buf[0] = SLA;
	buf[1] = ( 0x00 | ( (reg & 0x1f) << 3 ) );
	buf[2] = ( val >> 8 ) & 0xff;
	buf[3] = ( val >> 0 ) & 0xff;
	if ( bb_i2c_start( fw, 0      ) < 0 ) return -1;
	if ( bb_i2c_write( fw, buf, 4 ) < 0 ) return -1;
	if ( bb_i2c_stop ( fw         ) < 0 ) return -1;
	return 0;
}

int
dac47cxDetectMax(DAC47CXDev *dac)
{
uint16_t val;

	if ( dac47cxReset( dac )          < 0 ) return -1;
	if ( dac47cxGet  ( dac, 0, &val ) < 0 ) return -1;
	dac->dacMax = val;
	dac->dacMax = ((dac->dacMax + 1) << 1) - 1;
	return 0;
}

int
dac47cxReset(DAC47CXDev *dac)
{
uint8_t     buf[2];

	buf[0] = 0x00; /* special address */
	buf[1] = 0x06;
	if ( bb_i2c_start( fw, 0 )       < 0 ) return -1;
	if ( bb_i2c_write( fw, buf, 2 )  < 0 ) return -1;
	if ( bb_i2c_stop ( fw         )  < 0 ) return -1;
	return 0;
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
				_dacMax = 0xfff; break;
			default:
				fprintf(stderr, "Error -- dac47cxGetRange(): unknown -- DAC not initialized?\n");
				return 0;
		}
	}
	return _dacMax;
}

int
dac47cxInit(FWInfo *fw)
{
uint16_t val;

	if ( dac47cxReset( fw )                   < 0 ) return -1;
	if ( dac47cxReadReg( fw, REG_VAL0, &val ) < 0 ) return -1;
	_dacMax = val;
	_dacMax = ((_dacMax + 1) << 1) - 1;
	/* select internal bandgap (leave at gain 1)
	 * according to datasheet, if we use the internal
	 * bandgap then
	 *  - all channels must use it
	 *  - select on ch0
	 *  - other channels must be in external, buffered mode
	 */
	if ( dac47cxWriteReg( fw, REG_VREF, VREF_CH1(VREF_EXT_BUF) | VREF_CH0(VREF_INTERNAL) ) < 0 ) return -1;
	return 0;
}

/* Analog circuit: Vamp = - Vref/2 + Vdac * 2 */

#define VOLT_REF   1.214
#define VOLT_MIN   (-VOLT_REF/2.0)
#define VOLT(tick) (VOLT_MIN + VOLT_REF * ((float)(tick)) / (float)(maxDac + 1))
#define VOLT_MAX   VOLT(maxDac)
#define TICK(volt) round( (volt - VOLT_MIN)/(VOLT_MAX - VOLT_MIN) * (float)maxDac )

void
dac47cxGetRange(FWInfo *fw, int *tickMin, int *tickMax, float *voltMin, float *voltMax)
{
int maxDac = dacMax(fw);

	if ( tickMin ) *tickMin = 0;
	if ( tickMax ) *tickMax = maxDac;
	if ( voltMin ) *voltMin = VOLT( 0      );
	if ( voltMax ) *voltMax = VOLT( maxDac );
}

int
dac47cxSet(FWInfo *fw, unsigned channel, int val)
{
int maxDac = 0;

	if ( channel > 1 ) {
		fprintf(stderr, "Error -- dac47cxSet(): invalid channel\n");
		return -2;
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
	if ( dac47cxWriteReg( fw, REG_VAL0 + channel, val ) < 0 ) return -1;
	return 0;
}

int
dac47cxGet(FWInfo *fw, unsigned channel, uint16_t *val)
{
	if ( channel > 1 ) {
		fprintf(stderr, "Error -- dac47cxGet(): invalid channel\n");
		return -2;
	}
	if ( dac47cxReadReg( fw, REG_VAL0 + channel, val ) < 0 ) return -1;
	return 0;
}
int
dac47cxSetVolt(FWInfo *fw, unsigned channel, float val)
{
int ival, maxDac = dacMax(fw);

	if ( val < VOLT_MIN ) {
		fprintf(stderr, "dac47cxSetVolt(): value out of range; clipping to %f\n", VOLT_MIN);
		val = VOLT_MIN;
	}
	if ( val > VOLT_MAX ) {
		fprintf(stderr, "dac47cxSetVolt(): value out of range; clipping to %f\n", VOLT_MAX);
		val = VOLT_MAX;
	}


	ival = TICK( val );

	if ( -1         == ival ) ival = 0;
	if ( maxDac + 1 == ival ) ival = maxDac;
	return dac47cxSet( fw, channel, ival );
}

int
dac47cxGetVolt(FWInfo *fw, unsigned channel, float *valp)
{
uint16_t val;
int      maxDac = dacMax(fw);

	if ( channel > 1 ) {
		fprintf(stderr, "dac47xxGetVolt(): invalid channel\n");
		return -2;
	}
	if ( dac47cxGet( fw, channel, &val ) < 0 ) return -1;

	*valp = VOLT( val );

	return 0;
}

int
dac47cxSetRefSelection( DAC47CXDev *dac, DAC47CXRefSelection sel)
{
	switch ( sel ) {
		case DAC47XX_VREF_INTERNAL_X1:
			/* select internal bandgap (leave at gain 1)
			 * according to datasheet, if we use the internal
			 * bandgap then
			 *  - all channels must use it
			 *  - select on ch0
			 *  - other channels must be in external, buffered mode
			 */
			if ( dac47cxWriteReg( dac, REG_VREF, VREF_CH1(VREF_EXT_BUF) | VREF_CH0(VREF_INTERNAL) ) < 0 ) {
				return -1;
			}
			break;
		default:
			fprintf(stderr, "Error -- dac47cxSetRefSelection(): invalid/unsupported choice\n");
			return -1;
	}
	return 0;
}

#ifdef __cplusplus
}
#endif

