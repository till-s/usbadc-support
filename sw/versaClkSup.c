#include <stdio.h>
#include "fwComm.h"
#include "versaClkSup.h"
#include <math.h>
#include <errno.h>

#define CLK_I2C_SLA 0xd4

/* INT_MODE does not seem to work in otherwise 'normal' mode;
 * when I enable this (with a purely integer divider) the output
 * simply stops ticking.
 * It is possible that the bit could be swapped with with selb_norm
 * (in the documentation) because when I set the control bits to 0x9
 * then the clock ticks...
 * -> New tests: doesn't seem to be the case. It's not entirely clear
 *    what the individual bits do (except for en_fod) but integer
 *    mode does not seem to work in 'normal' mode but *required* otoh
 *    in cascaded mode.
 */
#undef  USE_INT_MODE

static int
readReg(FWInfo *fw, unsigned reg)
{
	return bb_i2c_read_reg(fw, CLK_I2C_SLA, reg);
}

static int
writeReg(FWInfo *fw, unsigned reg, uint8_t val)
{
	return bb_i2c_write_reg(fw, CLK_I2C_SLA, reg, val);
}

int
versaClkReadReg(FWInfo *fw, unsigned reg)
{
	return readReg( fw, reg );
}

int
versaClkWriteReg(FWInfo *fw, unsigned reg, uint8_t val)
{
	return writeReg( fw, reg, val );
}

int
versaClkSetFODRoute(FWInfo *fw, unsigned outp, VersaClkFODRoute rte);


int
versaClkSetFBDiv(FWInfo *fw, unsigned idiv, unsigned fdiv)
{
	int st;
	if ( (st = writeReg( fw, 0x17, (idiv >> 4 )         )) < 0 ) return st;
	if ( (st = writeReg( fw, 0x18, (idiv << 4 )  & 0xf0 )) < 0 ) return st;
	if ( (st = writeReg( fw, 0x19, (fdiv >> 16)         )) < 0 ) return st;
	if ( (st = writeReg( fw, 0x1A, (fdiv >>  8)         )) < 0 ) return st;
	if ( (st = writeReg( fw, 0x1B, (fdiv >>  0)         )) < 0 ) return st;
	return versaClkVCOCal( fw );
}

/* recalibrate the VCO; seems necessary when loop parameters
 * are changed. We do this internally from versaClkSetFBDiv() & friends.
 *
 * Note that we can still change the divider without recalibrating
 * but the new setting (of the FB divider) will not take effect;
 * also note that recalibrating the VCO has the (undocumented) side-effect
 * of stopping the ref-clock output:
 * The docs say toggling the cal bit from 0->1 triggers calibration.
 * We noticed that taking it from 1->0 effectively stops all clocks...
 * Thus: to reprogram the PLL some sort of other clock *must* be available!
 */
int
versaClkVCOCal(FWInfo *fw)
{
int           val,st;
unsigned      calRegRsvd = 0x1c;
	if ( (val = readReg( fw, calRegRsvd )) < 0 ) return val;
    val &= 0x7f;
	if ( (st  = writeReg( fw, calRegRsvd , val ))  < 0 ) return val;
    /* raising bit 7 triggers calibration */
    val |= 0x80;
	if ( (st  = writeReg( fw, calRegRsvd , val ))  < 0 ) return val;
	return 0;
}


static int
getFltDiv(FWInfo *fw, unsigned idivReg, unsigned fdivReg, int lstShft, double *div)
{
int           val;
unsigned      idiv = 0;
unsigned long fdiv = 0;

	if ( (val = readReg( fw, idivReg + 0 )) < 0 ) return val;
	idiv = ((uint8_t) val) & 0xf;
	if ( (val = readReg( fw, idivReg + 1 )) < 0 ) return val;
	idiv = (idiv << 4) | ((((uint8_t)val) & 0xf0) >> 4);
	if ( (val = readReg( fw, fdivReg + 0 )) < 0 ) return val;
	fdiv = (uint8_t) val;
	if ( (val = readReg( fw, fdivReg + 1 )) < 0 ) return val;
	fdiv = (fdiv << 8) | (uint8_t)val;
	if ( (val = readReg( fw, fdivReg + 2 )) < 0 ) return val;
	fdiv = (fdiv << 8) | (uint8_t)val;
	if ( (val = readReg( fw, fdivReg + 3 )) < 0 ) return val;
	fdiv = (fdiv << lstShft) | ((uint8_t)val >> (8 - lstShft));

	*div = (double)idiv + (double)fdiv / exp2(24.0);

	return 0;
}


int
versaClkGetFBDivFlt(FWInfo *fw, double *div)
{
	return getFltDiv( fw, 0x17, 0x19, 8, div );
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
		return -EINVAL;
	}
	return 0;
}

int
versaClkSetOutCfg(FWInfo *fw, unsigned outp, VersaClkOutMode mode, VersaClkOutSlew slew, VersaClkOutLevel level)
{
unsigned outCReg = OUTP1_CR + ((outp - 1) * 2);
uint8_t  val;
int      st;

	if ( (st = checkOut( "versaClkSetOutCfg()", outp ) ) < 0 ) return st;

	val = ( ( mode & 0x7 ) << 5 ) | ( ( level & 0x3 ) << 3 ) | ( (slew & 0x3) << 0 );

	if ( (st = writeReg( fw, outCReg , val ))  < 0       ) return st;

	return 0;
}

#define PSRC_CR            0x10
#define PSRC_CR_EN_REFMODE (1<<2)

#define ODIV_CR_RSTB       (1<<7)
#define ODIV_CR_SELB_NORM  (1<<3)
#define ODIV_CR_SEL_EXT    (1<<2)
#define ODIV_CR_INT_MODE   (1<<1)
#define ODIV_CR_EN_FOD     (1<<0)

#define OUTP_CR1_EN_CLKBUF (1<<0)

#define SKEW_INT_CR1       0x2c
#define SKEW_INT_CR_EN_AUX (1<<0)

/* In the versaclock 6e family register descriptions and programming guide
 * there is a picture that shows all the muxes that route clocks around.
 * This seems more accurate than the register description that says
 * 'this bit is used to enable the clock output'. Instead, this bit seems
 * to control a mux into the FOD that selects either the pll ('1') or the
 * previous output. Note that the previous output also must be routed to
 * this connection with the previous output's 'aux_en' or 'en_refmode'
 * for output 1, respectively.
 *
 *                     
 *  REF  -[SI1]-o----------------------\
 *               \----\             MO1| -o---> OUT1
 *                 MI1| --> FOD1 -> ---/  |
 *  PLL -o------------/                   |
 *       |                              [SO1]
 *       |                          ------|
 *       |                          |
 *       |       -------------------o--\
 *       |       \----\             MO2| -o---> OUT2
 *       |         MI2| --> FOD2 -> ---/  |
 *       o------------/                   |
 *       |                              [SO2]
 *       |                          ------|
 *       |                          |
 *      cascade to further FOD / OUT Stages
 *
 *  The switches 'SOi' enable cascading the output
 *  to the next stage and are enabled by 'en_aux'
 *  in the output divider 'skew integer part' register (0x2c, 0x3c, ...).
 *
 *  The switch SI1 which enables routing the reference to muxes
 *  MI1 and MO1 is enabled by en_refmode in the primary source and
 *  shutdown register (0x10)
 *
 *  The FOD input muxes MIi are switched by en_clkbuf in
 *  output control register (0x22, 0x32, ...) and select the
 *  PLL when '1' and the previous output when '0' (requires
 *  SO(i-1) to be enabled, too)
 *
 *  The output muxes MOi are controlled by sel_ext, selb_norm, en_fod
 *  in the output control register (0x21, 0x31, ...)
 */

int
versaClkSetFODRoute(FWInfo *fw, unsigned outp, VersaClkFODRoute rte)
{
unsigned mux1Reg = OUTP1_CR + ((outp - 1) * 2) + 1;
unsigned mux2Reg = ODIV1_CR + ((outp - 1) * 0x10);
unsigned spreReg = ( outp == 1 ) ? PSRC_CR            : SKEW_INT_CR1 + ((outp - 2) * 0x10);
uint8_t  spreBit = ( outp == 1 ) ? PSRC_CR_EN_REFMODE : SKEW_INT_CR_EN_AUX;

uint8_t  mux1Val, mux2Val, spreVal;
int      val, st;

	if ( (val = checkOut( "versaClkSetFODRoute()", outp )) < 0 ) return val;

    if ( (val = readReg( fw, mux1Reg )) < 0 ) return val;
    mux1Val = (uint8_t)val;
    if ( (val = readReg( fw, mux2Reg )) < 0 ) return val;
    mux2Val = (uint8_t)val;
    if ( (val = readReg( fw, spreReg )) < 0 ) return val;
    spreVal = (uint8_t)val;

    /* switch mux2 off (enable select bits as required below) */
    mux2Val &= ~(ODIV_CR_SELB_NORM | ODIV_CR_SEL_EXT | ODIV_CR_INT_MODE | ODIV_CR_EN_FOD);
	/* assert resetb (deassert reset) */
    mux2Val |=  ODIV_CR_RSTB;

    switch ( rte ) {
      case OFF:
        spreVal &= ~spreBit;            /* disable cascade  */
        mux1Val &= ~OUTP_CR1_EN_CLKBUF; /* FOD uses cascade; should not matter but perhaps lower power */
        break;
      case CASC_FOD:
        spreVal |=  spreBit;            /* enable  cascade  */
        mux1Val &= ~OUTP_CR1_EN_CLKBUF; /* FOD uses cascade */
        mux2Val |=  (ODIV_CR_SELB_NORM | ODIV_CR_SEL_EXT | ODIV_CR_INT_MODE | ODIV_CR_EN_FOD);
        break;
      case CASC_OUT:
        spreVal |=  spreBit;            /* enable  cascade  */
        mux1Val &= ~OUTP_CR1_EN_CLKBUF; /* FOD uses cascade; should not matter but perhaps lower power */
        mux2Val |=  (ODIV_CR_SELB_NORM | ODIV_CR_SEL_EXT);
        break;
      default: /* includes NORMAL */
#ifdef USE_INT_MODE
		{
			uint8_t  fdivOr  = 0;
            unsigned fdivReg = mux2Reg  + 1;
			int      i;
			for ( i = 0; i < 4; i++ ) {
				if ( (val = readReg( fw, fdivReg + i )) < 0 ) return val;
				if ( 3 == i ) {
					val &= 0xfc;
				}
				fdivOr  |= (uint8_t)val;
			}
			if ( 0 == fdivOr ) {
				mux2Val |=  ODIV_CR_INT_MODE;
			}
		}
#endif
        spreVal &= ~spreBit;            /* disable cascade  */
        mux1Val |=  OUTP_CR1_EN_CLKBUF; /* FOD uses PLL     */
        mux2Val |=  ODIV_CR_EN_FOD;
        break;
	}

    if ( (st = writeReg( fw, spreReg, spreVal )) < 0 ) return st;
    if ( (st = writeReg( fw, mux1Reg, mux1Val )) < 0 ) return st;
    if ( (st = writeReg( fw, mux2Reg, mux2Val )) < 0 ) return st;

	return 0;
}

int
versaClkSetOutDiv(FWInfo *fw, unsigned outp, unsigned idiv, unsigned long fdiv)
{
unsigned divCReg = ODIV1_CR + ((outp - 1) * 0x10);
unsigned fdivReg = divCReg  + 1;
unsigned idivReg = fdivReg  + 0xb;
int      val, st;
uint8_t  divCVal;

	if ( ( val = checkOut( "versaClkSetOutDiv()", outp ) ) < 0 ) return val;

	if ( ( val = readReg( fw, divCReg ) ) < 0 )    return val;
    divCVal = (uint8_t)val;

	if ( 0 == idiv && 0 == fdiv ) {
        divCVal &= ~ ODIV_CR_RSTB;
		return writeReg( fw, divCReg, 0x01 );
	}

	divCVal |= ODIV_CR_RSTB;

    if ( (divCVal & ODIV_CR_SEL_EXT) ) {
		if ( ! (divCVal & ODIV_CR_EN_FOD) ) {
			fprintf(stderr, "versaClkSetOutDiv: WARNING: output %d is chained and not using FOD\n", outp);
			return -EFAULT;
		}
		if ( fdiv != 0 ) {
			fprintf(stderr, "versaClkSetOutDiv: WARNING: output %d is chained; truncating fractional part\n", outp);
			if ( fdiv >= (1<<29) && idiv < 0xfff ) {
				idiv++;
			}
			fdiv = 0;
		}
	}

	if ( (st = writeReg( fw, idivReg + 0, (idiv >> 4 )        )) < 0 ) return st;
	if ( (st = writeReg( fw, idivReg + 1, (idiv << 4 ) & 0xf0 )) < 0 ) return st;
	if ( (st = writeReg( fw, fdivReg + 0, (fdiv >> 22)        )) < 0 ) return st;
	if ( (st = writeReg( fw, fdivReg + 1, (fdiv >> 14)        )) < 0 ) return st;
	if ( (st = writeReg( fw, fdivReg + 2, (fdiv >>  6)        )) < 0 ) return st;
	if ( (st = writeReg( fw, fdivReg + 3, (fdiv <<  2) & 0xfc )) < 0 ) return st;

#ifdef USE_INT_MODE
	if ( 0 == fdiv ) {
		/* integer mode */
		divCVal |= ODIV_CR_INT_MODE;
	}
#endif
    /* In non-chained mode we explicitly enable the FOD */
    divCVal |= ODIV_CR_EN_FOD;

	if ( (st = writeReg( fw, divCReg, divCVal )) < 0 ) return st;
	
	return 0;
}

int
versaClkGetOutDivFlt(FWInfo *fw, unsigned outp, double *div)
{
unsigned divCReg = ODIV1_CR + ((outp - 1) * 0x10);
unsigned fdivReg = divCReg  + 1;
unsigned idivReg = fdivReg  + 0xb;
int      st;

	if ( (st = checkOut( "versaClkGetOutDiv()", outp )) < 0 ) return st;

	/* FIXME - deal with routing ? */

	return getFltDiv( fw, idivReg, fdivReg, 6, div );
}

int
versaClkSetOutDivFlt(FWInfo *fw, unsigned outp, double div)
{
unsigned      idiv;
unsigned long fdiv;
double        intg;

	fdiv = (unsigned long) round( exp2(24.0) * modf( fabs( div ), & intg ) );
    idiv = (unsigned)      intg;
	return versaClkSetOutDiv( fw, outp, idiv, fdiv );
}
