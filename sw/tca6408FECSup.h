#pragma once

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
	unsigned       numChannels,
	uint8_t        sla,
	double         attMinDb,
	double         attMaxDb,
	/* returns bit mask for selected bit/channel or negative status if not supported */
	int          (*getBit)(struct FWInfo *fw, unsigned channel, I2CFECSupBitSelect which)
);

#ifdef __cplusplus
}
#endif
