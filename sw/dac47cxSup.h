#pragma once

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

void
dac47cxGetRange(FWInfo *fw, int *tickMin, int *tickMax, float *voltMin, float *voltMax);

int
dac47cxSet(FWInfo *fw, unsigned channel, int val);

int
dac47cxGet(FWInfo *fw, unsigned channel, uint16_t *val);

int
dac47cxSetVolt(FWInfo *fw, unsigned channel, float val);

typedef enum { DAC47XX_VREF_INTERNAL_X1 } DAC47CXRefSelection;

int
dac47cxSetRefSelection(FWInfo *fw, DAC47CXRefSelection sel);

int
dac47cxGetVolt(FWInfo *fw, unsigned channel, float *val);

/* Detect the max. range supported by the device;
 * NOTE: Uses a HARD-RESET; all settings are lost !!!
 */
int
dac47cxDetectMax(FWInfo *fw);

#ifdef __cplusplus
}
#endif
