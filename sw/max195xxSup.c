#include "max195xxSup.h"
#include "fwComm.h"
#include <stdio.h>

static int
do_io( FWInfo *fw, int rdnwr, unsigned reg, uint8_t *val )
{
uint8_t buf[4];
int     rv;

	if ( reg > 0xa || 9 == reg ) {
		return -2;
	}

	buf[2] = 0x00;
	buf[3] = (rdnwr ? 0xff : 0x00);
	buf[0] = (rdnwr ? 0x80 : 0x00 ) | (reg & 0x7f);
	buf[1] = (rdnwr ? 0x00 : *val );
	rv = bb_spi_xfer(fw, SPI_ADC, buf, buf, buf+2, 2);
	if ( rdnwr ) {
		*val = buf[1];
	}
	return rv;
}

int
max195xxReadReg( FWInfo *fw, unsigned reg, uint8_t *val )
{
	return do_io( fw, 1, reg, val );
}

int
max195xxWriteReg( FWInfo *fw, unsigned reg, uint8_t val )
{
	return do_io( fw, 0, reg, &val );
}

int
max195xxReset( FWInfo *fw )
{
	return max195xxWriteReg( fw, 0xa, 0x5a );
}

int
max195xxDLLLocked( FWInfo *fw )
{
int     rv;
uint8_t val;

	rv = max195xxReadReg( fw, 0xa, &val ); 
	if ( rv < 0 ) return rv;

	return (val & 0x11) == 0x11 ? 0 : -3;
}

int
max195xxInit( FWInfo *fw )
{
int     rv;

	rv = max195xxDLLLocked( fw );
	if ( rv < 0 ) {
		if ( -3 == rv ) {
			fprintf(stderr, "max195xxInit(): DLL not locked -- probably no ADC clock!");
		}
		return rv;
	}

	/* Set muxed-mode on channel B */
	rv = max195xxWriteReg( fw, 0x1, 0x6 );
	if ( rv < 0 ) return rv;

	/* Empirically found setting for the prototype board */
	rv = max195xxSetTiming( fw, -1, 3 );
	if ( rv < 0 ) return rv;

	/* set common-mode voltage (also important for PGA output)
	 *
	 * ADC: common mode input voltage range 0.4..1.4V
	 * ADC: controls common mode voltage of PGA
	 * PGA: output common mode voltage: 2*OCM
	 * Resistive divider 232/(232+178)
	 *
	 * PGA VOCM = 2*ADC_VCM
	 *
	 * Valid range for PGA: 2..3V (2.5V best)
	 *
	 * Common-mode register 8:
	 *   bit 6..4, 2..0:
	 *         000       -> 0.9 V
	 *         001       -> 1.05V
	 *         010       -> 1.2V
	 *
	 * With 1.2V -> VOCM of PGA becomes 2.4V   (near optimum)
	 *           -> VICM of ADC becomes 1.358V (close to max)
	 * With 1.05 -> VOCM of PGA becomes 2.1V   (close to min)
	 *           -> VICM of ADC becomes 1.188V (OK)
	 */

	return max195xxSetCMVolt( fw, CM_1050mV, CM_1050mV );
}

#define TIMING_REG 3

#define DA_BYPASS   (1<<7)
#define DLY_HALF_T  (1<<6)
#define DCLK_TIME_SHIFT 3
#define DATA_TIME_SHIFT 0

static int delay2bits(int delay)
{
	if ( delay > 3 || delay < -3 ) {
		return -1;
	}
	return delay >= 0 ? delay : (4 - delay);
}

int
max195xxSetTiming( FWInfo *fw, int dclkDelay, int dataDelay)
{
uint8_t val;
	if ( ( dclkDelay = delay2bits( dclkDelay )) < 0 ) return -2;
	if ( ( dataDelay = delay2bits( dataDelay )) < 0 ) return -2;
	val = (dclkDelay << DCLK_TIME_SHIFT) | (dataDelay << DATA_TIME_SHIFT);
	return max195xxWriteReg( fw, TIMING_REG, val );
}

int
max195xxSetTestMode(FWInfo *fw, Max195xxTestMode m)
{
uint8_t val;
int     rv;
	rv = max195xxReadReg( fw, 0x06, &val );
	if ( rv < 0 ) {
		return rv;
	}
	val &= 0x0f;
	/* For either test pattern it seems (empirically determined) that
	 * we must use offset-binary mode!
	 */
	switch ( m ) {
		case RAMP_TEST: val |= 0x50; break;
		case AA55_TEST: val |= 0xD0; break;
		default:        val |= 0x00; break; /* two's complement */
	}
	return max195xxWriteReg( fw, 0x06, val );
}

int
max195xxSetCMVolt( FWInfo *fw, Max195xxCMVolt cmA, Max195xxCMVolt cmB )
{
uint8_t val;
	/* Don't apply common-mode voltage to inputs (voltage controls the
	 * LM6882 which in turn supplies CM voltage to the ADC
	 */
	val = (cmB << 4 ) | cmA;
	return max195xxWriteReg( fw, 0x08, val );	
}
