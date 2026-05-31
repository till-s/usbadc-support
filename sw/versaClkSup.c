/**LB-MIT
 *
 * MIT License
 *
 * Copyright (c) 2026 Till Straumann
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **LE-MIT*/

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
	idiv = ((uint8_t) (val & 0xff));
	if ( (val = readReg( fw, idivReg + 1 )) < 0 ) return val;
	idiv = (idiv << 4) | ((((uint8_t)val) & 0xf0) >> 4);
	if ( (val = readReg( fw, fdivReg + 0 )) < 0 ) return val;
	fdiv = (uint8_t) val;
	if ( (val = readReg( fw, fdivReg + 1 )) < 0 ) return val;
	fdiv = (fdiv << 8) | (uint8_t)val;
	if ( (val = readReg( fw, fdivReg + 2 )) < 0 ) return val;
	fdiv = (fdiv << 8) | (uint8_t)val;
	if ( lstShft ) {
		if ( (val = readReg( fw, fdivReg + 3 )) < 0 ) return val;
		fdiv = (fdiv << lstShft) | ((uint8_t)val >> (8 - lstShft));
	}

	*div = (double)idiv + (double)fdiv / exp2(24.0);

	return 0;
}


int
versaClkGetFBDivFlt(FWInfo *fw, double *div)
{
	return getFltDiv( fw, 0x17, 0x19, 0, div );
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

#define MUX2_MODE_MASK (ODIV_CR_SELB_NORM | ODIV_CR_SEL_EXT | ODIV_CR_INT_MODE | ODIV_CR_EN_FOD)

static unsigned
mux1RegOff(unsigned outp)
{
	return ( OUTP1_CR + ((outp - 1) * 2) + 1 );
}

static unsigned
mux2RegOff(unsigned outp)
{
	return ( ODIV1_CR + ((outp - 1) * 0x10) );
}

static unsigned
spreRegOff(unsigned outp)
{
	return ( ( outp == 1 ) ? PSRC_CR            : SKEW_INT_CR1 + ((outp - 2) * 0x10) );
}

int
versaClkSetFODRoute(FWInfo *fw, unsigned outp, VersaClkFODRoute rte)
{
unsigned mux1Reg = mux1RegOff( outp );
unsigned mux2Reg = mux2RegOff( outp );
unsigned spreReg = spreRegOff( outp );

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
    mux2Val &= ~ MUX2_MODE_MASK;
	/* assert reset; if the FOD is enabled then 'SetOutDiv' shall
	 * be used to program the dividers and deassert reset
	 */
    mux2Val &= ~ ODIV_CR_RSTB;

	/* NOTE: WHEN CHANGING BITS IN mux2 MODIFY versaClkGetFODRoute ACCORDINGLY */
	/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */
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
        mux2Val |=  (ODIV_CR_SELB_NORM | ODIV_CR_SEL_EXT); /* FOD disabled; output driven by cascade */
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
versaClkGetFODRoute(FWInfo *fw, unsigned outp, VersaClkFODRoute *rte)
{
unsigned mux2Reg = mux2RegOff( outp );
uint8_t  mux2Val;
int      val;

	if ( (val = checkOut( "versaClkSetFODRoute()", outp )) < 0 ) {
		return val;
	}

	if ( ! rte ) {
		return -EINVAL;
	}

	if ( (val = readReg( fw, mux2Reg )) < 0 ) {
		return val;
	}

	mux2Val = ( val & MUX2_MODE_MASK );

	switch ( mux2Val ) {
		case   0:
			*rte = OFF;
		break;
        case  (ODIV_CR_SELB_NORM | ODIV_CR_SEL_EXT | ODIV_CR_INT_MODE | ODIV_CR_EN_FOD):
			*rte = CASC_FOD;
		break;
        case  (ODIV_CR_SELB_NORM | ODIV_CR_SEL_EXT):
			*rte = CASC_OUT;
		break;
		case  ( ODIV_CR_EN_FOD
#ifdef USE_INT_MODE
				| ODIV_CR_INT_MODE
#endif
		      ):
			*rte = NORMAL;
		break;

		default:
			return -ENOSYS;
	}
	return 0;
}

int
versaClkSetOutDiv(FWInfo *fw, unsigned outp, unsigned idiv, unsigned long fdiv)
{
unsigned divCReg = ODIV1_CR + ((outp - 1) * 0x10);
unsigned fdivReg = divCReg  + 1;
unsigned idivReg = fdivReg  + 0xb;
int      val, st;
uint8_t  divCVal, regVal;

	if ( idiv > 4095 ) {
		return -EINVAL;
	}

	if ( ( val = checkOut( "versaClkSetOutDiv()", outp ) ) < 0 ) return val;

	if ( ( val = readReg( fw, divCReg ) ) < 0 )    return val;
    divCVal = (uint8_t)val;

    if ( (divCVal & ODIV_CR_SEL_EXT) ) {
		if ( fdiv != 0 ) {
			fprintf(stderr, "versaClkSetOutDiv: WARNING: output %d is chained; truncating fractional part\n", outp);
			if ( fdiv >= (1<<29) && idiv < 0xfff ) {
				idiv++;
			}
			fdiv = 0;
		}
	}

	if ( (st = writeReg( fw, idivReg + 0, (idiv >> 4 )        )) < 0 ) return st;
	if ( (st = readReg ( fw, idivReg + 1                      )) < 0 ) return st;
	regVal = (st & 0x0f) | ( (idiv<<4) & 0xf0 );
	if ( (st = writeReg( fw, idivReg + 1, regVal              )) < 0 ) return st;
	if ( (st = writeReg( fw, fdivReg + 0, (fdiv >> 22)        )) < 0 ) return st;
	if ( (st = writeReg( fw, fdivReg + 1, (fdiv >> 14)        )) < 0 ) return st;
	if ( (st = writeReg( fw, fdivReg + 2, (fdiv >>  6)        )) < 0 ) return st;
	if ( (st = readReg ( fw, fdivReg + 3                      )) < 0 ) return st;
	regVal = (st & 0x03) | ((fdiv << 2) & 0xfc);
	if ( (st = writeReg( fw, fdivReg + 3, regVal              )) < 0 ) return st;

#ifdef USE_INT_MODE
	if ( 0 == fdiv ) {
		/* integer mode */
		divCVal |= ODIV_CR_INT_MODE;
	}
#endif

	/* SetFODRoute enabled the FOD; take it out of reset */
	if ( !! (divCVal & ODIV_CR_EN_FOD) ) {
		divCVal |= ODIV_CR_RSTB;
	}

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
