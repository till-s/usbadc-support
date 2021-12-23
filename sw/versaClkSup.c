#include <stdio.h>
#include "fwComm.h"
#include "versaClkSup.h"
#include <math.h>

#define CLK_I2C_SLA 0xd4

/*
static int
readReg(FWInfo *fw, unsigned reg)
{
	return bb_i2c_read_reg(fw, CLK_I2C_SLA, reg);
}
*/

static int
writeReg(FWInfo *fw, unsigned reg, uint8_t val)
{
	return bb_i2c_write_reg(fw, CLK_I2C_SLA, reg, val);
}

int
versaClkSetFBDiv(FWInfo *fw, unsigned idiv, unsigned fdiv)
{
	if ( writeReg( fw, 0x17, (idiv >> 4 )        ) < 0 ) return -1;
	if ( writeReg( fw, 0x18, (idiv << 4 ) & 0xf0 ) < 0 ) return -1;
	if ( writeReg( fw, 0x19, (fdiv >> 16)        ) < 0 ) return -1;
	if ( writeReg( fw, 0x1A, (fdiv >>  8)        ) < 0 ) return -1;
	if ( writeReg( fw, 0x1B, (fdiv >>  0)        ) < 0 ) return -1;
	return 0;
}

int
versaClkSetFBDivFlt(FWInfo *fw, double div)
{
unsigned idiv;
unsigned fdiv;
double   intg;

	fdiv = (unsigned) round( exp2(24.0) * modf( fabs( div ), &intg ) );
    idiv = (unsigned) intg;

	return versaClkSetFBDiv( fw, idiv, fdiv );
}

#define ODIV1_CR 0x21
#define OUTP1_CR 0x60

static int checkOut(const char *nm, unsigned outp)
{
	if ( outp > 4 || outp < 1 ) {
		fprintf(stderr, "%s: invalid output (must be 1..4)\n", nm);
		return -1;
	}
	return 0;
}

int
versaClkSetOutDiv(FWInfo *fw, unsigned outp, unsigned idiv, unsigned long fdiv)
{
unsigned divCReg = ODIV1_CR + ((outp - 1) * 0x10);
unsigned fdivReg = divCReg  + 1;
unsigned idivReg = fdivReg  + 0xb;
uint8_t  val;

	if ( checkOut( "versaClkSetOutDiv()", outp ) ) return -3;

	if ( 0 == idiv && 0 == fdiv ) {
		return writeReg( fw, divCReg, 0x01 );
	}
	if ( writeReg( fw, idivReg + 0, (idiv >> 4 )        ) < 0 ) return -1;
	if ( writeReg( fw, idivReg + 1, (idiv << 4 ) & 0xf0 ) < 0 ) return -1;
	if ( writeReg( fw, fdivReg + 0, (fdiv >> 22)        ) < 0 ) return -1;
	if ( writeReg( fw, fdivReg + 1, (fdiv >> 14)        ) < 0 ) return -1;
	if ( writeReg( fw, fdivReg + 2, (fdiv >>  6)        ) < 0 ) return -1;
	if ( writeReg( fw, fdivReg + 3, (fdiv <<  2) & 0xfc ) < 0 ) return -1;
	val = 0x81; /* enable output divider, don't assert RSTb */
	if ( 0 == fdiv ) {
		/* integer mode */
		val |= 0x40;
	}
	if ( writeReg( fw, divCReg, val ) < 0 ) return -1;
	
	return 0;
}

int
versaClkSetOutDivFlt(FWInfo *fw, unsigned outp, double div)
{
unsigned      idiv;
unsigned long fdiv;
double        intg;

	fdiv = (unsigned long) round( exp2(30.0) * modf( fabs( div ), & intg ) );
    idiv = (unsigned)      intg;
	return versaClkSetOutDiv( fw, outp, idiv, fdiv );
}

int
versaClkSetOutEna(FWInfo *fw, unsigned outp, int ena)
{
unsigned outCReg = OUTP1_CR + ((outp - 1) * 2);

	if ( checkOut( "versaClkSetOutEna()", outp )            ) return -3;

	if ( writeReg( fw, outCReg + 1, ena ? 0x01 : 0x00 ) < 0 ) return -1;

	return 0;
}

int
versaClkSetOutCfg(FWInfo *fw, unsigned outp, VersaClkOutMode mode, VersaClkOutSlew slew, VersaClkOutLevel level)
{
unsigned outCReg = OUTP1_CR + ((outp - 1) * 2);
uint8_t  val;

	if ( checkOut( "versaClkSetOutCfg()", outp ) ) return -3;

	val = ( ( mode & 0x7 ) << 5 ) | ( ( level & 0x3 ) << 3 ) | ( (slew & 0x3) << 0 );

	if ( writeReg( fw, outCReg , val )  < 0       ) return -1;

	return 0;
}
