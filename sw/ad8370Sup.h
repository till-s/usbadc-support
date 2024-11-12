#ifndef USBADC_AD8370_SUP_H
#define USBADC_AD8370_SUP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FWInfo FWInfo;

int
ad8370Write(FWInfo *fw, int channel, uint8_t val);

int
ad8370Read(FWInfo *fw, int channel);

int
ad8370SetAtt(FWInfo *fw, unsigned channel, float att);

/* returns NaN if there is an error */
float
ad8370GetAtt(FWInfo *fw, unsigned channel);

struct PGAOps;
extern struct PGAOps ad8370PGAOps;

#ifdef __cplusplus
}
#endif

#endif


