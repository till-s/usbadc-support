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

typedef enum { DAC47XX_VREF_INTERNAL_X1 } DAC47CXRefSelection;

int
dac47cxSetVolt(FWInfo *fw, unsigned channel, float val);
dac47cxSetRefSelection( DAC47CXDev *dac, DAC47CXRefSelection sel);

/* Detect the max. range supported by the device;
 * NOTE: Uses a HARD-RESET; all settings are lost !!!
 */
int
dac47cxGetVolt(FWInfo *fw, unsigned channel, float *val);
dac47cxDetectMax(DAC47CXDev *dac);

#ifdef __cplusplus
}
#endif

#endif
