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
ad8370SetAtt(FWInfo *fw, unsigned channel, float att);

#ifdef __cplusplus
}
#endif

#endif


