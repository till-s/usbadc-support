#ifndef USBADC_DAC47CX_SUP
#define USBADC_DAC47CX_SUP

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct FWInfo;

typedef struct FWInfo FWInfo;

void
dac47cxReset(FWInfo *fw);

uint16_t
dac47cxReadReg(FWInfo *fw, unsigned reg);

void
dac47cxWriteReg(FWInfo *fw, unsigned reg, uint16_t val);

void
dac47cxInit(FWInfo *fw);

void
dac46cxGetRange(int *tickMin, int *tickMax, float *voltMin, float *voltMax);

void
dac47cxSet(FWInfo *fw, unsigned channel, int val);

#ifdef __cplusplus
}
#endif

#endif
