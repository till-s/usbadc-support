#include <stdlib.h>
#include <stdio.h>

#include "fwComm.h"
#include "tca6408FECSup.h"

#define OUT_REG 0x01
#define DIR_REG 0x03
#define DIR_ALL_OUT 0x00

typedef struct TCA6408FECSup {
	FECOps       ops;
	FWInfo      *fw;
	uint8_t      sla;
	double       attMin;
	double       attMax;
	int          (*getBit)(struct FWInfo *fw, unsigned channel, I2CFECSupBitSelect which);
} TCA6408FECSup;

static int
readBit(FECOps *ops, unsigned channel, I2CFECSupBitSelect which)
{
	TCA6408FECSup *fec  = (TCA6408FECSup*)ops;
	uint8_t        sla8 = (fec->sla << 1);
	int            msk  = fec->getBit( fec->fw, channel, which );
	int            st;
	if ( msk < 0 ) {
		return msk;
	}
	if ( (st = bb_i2c_read_reg( fec->fw, sla8, OUT_REG )) < 0 ) {
		return st;
	}
	return !! (st & msk);
}

static int
writeBit(FECOps *ops, unsigned channel, I2CFECSupBitSelect which, unsigned on)
{
	TCA6408FECSup *fec  = (TCA6408FECSup*)ops;
	uint8_t        sla8 = (fec->sla << 1);
	int            msk  = fec->getBit( fec->fw, channel, which );
	int            st;
	if ( msk < 0 ) {
		return msk;
	}
	if ( (st = bb_i2c_read_reg( fec->fw, sla8, OUT_REG )) < 0 ) {
		return st;
	}
	if ( on ) {
		st |= msk;
	} else {
		st &= ~msk;
	}
	return bb_i2c_write_reg( fec->fw, sla8, OUT_REG, st );
}


static int
opGetACMode(FECOps *ops, unsigned channel)
{
	return readBit( ops, channel, ACMODE ); 
}

static int
opSetACMode(FECOps *ops, unsigned channel, unsigned on)
{
	return writeBit( ops, channel, ACMODE, on );
}

static int
opGetTermination(FECOps *ops, unsigned channel)
{
	return readBit( ops, channel, TERMINATION );
}

static int
opSetTermination(FECOps *ops, unsigned channel, unsigned on)
{
	return writeBit( ops, channel, TERMINATION, on );
}

static int
opGetDACRangeHi(FECOps *ops, unsigned channel)
{
	/* high-level enables analog switch that reduces the
	 * DAC output voltage range.
	 */
	return ! readBit( ops, channel, DACRANGE );
}

static int
opSetDACRangeHi(FECOps *ops, unsigned channel, unsigned on)
{
	return writeBit( ops, channel, DACRANGE, ! on );
}

static int
opGetAttRange(FECOps *ops, double *min, double *max)
{
	TCA6408FECSup *fec  = (TCA6408FECSup*)ops;
	if ( min ) {
		*min = fec->attMin;
	}
	if ( max ) {
		*max = fec->attMax;
	}
	return 0;
}

static int
opGetAtt(FECOps *ops, unsigned channel, double *att)
{
	TCA6408FECSup *fec = (TCA6408FECSup*)ops;
	int             st = readBit( ops, channel, ATTENUATOR );
	if ( st < 0 ) {
		return st;
	}
	if ( att ) {
		*att = !!st ? fec->attMax : fec->attMin;
	}
	return 0;
}

static int
opSetAtt(FECOps *ops, unsigned channel, double att)
{
	TCA6408FECSup *fec = (TCA6408FECSup*)ops;
	return writeBit( ops, channel, ATTENUATOR, (fec->attMin + fec->attMax) > 2.0 * att ? 0 : 1 );
}

static void
opClose(FECOps *ops)
{
	free( ops );
}

struct FECOps *tca6408FECSupCreate(
	struct FWInfo *fw,
	uint8_t        sla,
	double         attMin,
	double         attMax,
	/* returns bit mask for selected bit/channel or negative status if not supported */
	int          (*getBit)(struct FWInfo *fw, unsigned channel, I2CFECSupBitSelect which)
)
{
TCA6408FECSup *fec  = 0;
uint8_t        sla8 = (sla << 1);
unsigned       chn;
int            dir;

	if ( ! fw || ! getBit ) {
		fprintf(stderr, "tca6408FECSupCreate: missing arguments\n");
		return 0;
	}

	if ( (dir = bb_i2c_read_reg( fw, sla8, DIR_REG )) < 0 ) {
		fprintf(stderr, "tca6408FECSup ERROR -- reading GPIO direction FAILED\n");
		return 0;
	}

	fec = calloc( 1, sizeof(*fec) );
	if ( ! fec ) {
		fprintf(stderr, "tca6408FECSupCreate: no memory\n");
		return 0;
	}

	fec->fw                 = fw;
	fec->sla                = sla;
	fec->attMin             = attMin;
	fec->attMax             = attMax;
	fec->getBit             = getBit;
	fec->ops.getACMode      = opGetACMode;
	fec->ops.setACMode      = opSetACMode;
	fec->ops.getTermination = opGetTermination;
	fec->ops.setTermination = opSetTermination;
	fec->ops.getDACRangeHi  = opGetDACRangeHi;
	fec->ops.setDACRangeHi  = opSetDACRangeHi;
	fec->ops.getAttRange    = opGetAttRange;
	fec->ops.getAtt         = opGetAtt;
	fec->ops.setAtt         = opSetAtt;
	fec->ops.close          = opClose;

	/* Only initialize if it has not been done yet; otherwise leave alone */
	if ( DIR_ALL_OUT != dir ) {
		/* set output values before changing direction! */

		for ( chn = 0; chn < fw_get_num_channels( fw ); ++chn ) {
			opSetTermination( &fec->ops, chn, 0 );
			opSetACMode     ( &fec->ops, chn, 1 );
			opSetAtt        ( &fec->ops, chn, attMax );
			opSetDACRangeHi ( &fec->ops, chn, 1 );
		}

		if ( bb_i2c_write_reg( fw, sla8, DIR_REG, DIR_ALL_OUT ) < 0 ) {
			fprintf(stderr, "tca6408FECSup ERROR -- changing GPIO direction FAILED\n");
			free( fec );
			return 0;
		}
	}
	return &fec->ops;
}
