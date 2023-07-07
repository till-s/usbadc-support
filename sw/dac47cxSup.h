#ifndef USBADC_DAC47CX_SUP
#define USBADC_DAC47CX_SUP

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct FWInfo;

typedef struct FWInfo FWInfo;

int
dac47cxReset(FWInfo *fw);

int
dac47cxReadReg(FWInfo *fw, unsigned reg, uint16_t *val);

int
dac47cxWriteReg(FWInfo *fw, unsigned reg, uint16_t val);

int
dac47cxInit(FWInfo *fw);

void
dac47cxGetRange(FWInfo *fw, int *tickMin, int *tickMax, float *voltMin, float *voltMax);

int
dac47cxSet(FWInfo *fw, unsigned channel, int val);

int
dac47cxGet(FWInfo *fw, unsigned channel, uint16_t *val);

int
dac47cxSetVolt(FWInfo *fw, unsigned channel, float val);

int
dac47cxGetVolt(FWInfo *fw, unsigned channel, float *val);

#ifdef __cplusplus
}
#endif

#endif
