#ifndef TCA_6408_FEC_SUP_H
#define TCA_6408_FEC_SUP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	ACMODE,
	TERMINATION,
	ATTENUATOR,
	DACRANGE
} I2CFECSupBitSelect;

struct FECOps;
struct FWInfo;

struct FECOps *tca6408FECSupCreate(
	struct FWInfo *fw,
	uint8_t        sla,
	double         attMin,
	double         attMax,
	/* returns bit mask for selected bit/channel or negative status if not supported */
	int          (*getBit)(struct FWInfo *fw, unsigned channel, I2CFECSupBitSelect which)
);

#ifdef __cplusplus
}
#endif

#endif
