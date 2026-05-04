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

int
dac47cxSet(FWInfo *fw, unsigned channel, int val);

int
dac47cxGet(FWInfo *fw, unsigned channel, uint16_t *val);

typedef enum { DAC47XX_VREF_INTERNAL_X1 } DAC47CXRefSelection;

int
dac47cxSetRefSelection(FWInfo *fw, DAC47CXRefSelection sel);

/* Detect the max. range supported by the device;
 * NOTE: Uses a HARD-RESET; all settings are lost !!!
 *       The other settings are not modified, i.e.,
 *       the state of the DAC is identical to the one
 *       produced by dac47cxReset().
 *       In particular: the value is set to 1/2 FS.
 */
int
dac47cxDetectMax(FWInfo *fw);

#ifdef __cplusplus
}
#endif
