#ifndef USBADC_LMH6882_SUP_H
#define USBADC_LMH6882_SUP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FWInfo FWInfo;

int
lmh6882ReadReg(FWInfo *fw, uint8_t reg);

int
lmh6882WriteReg(FWInfo *fw, uint8_t reg, uint8_t val);

/* Attenuation in dB or negative value on error */
float
lmh6882GetAtt(FWInfo *fw, unsigned channel);

int
lmh6882SetAtt(FWInfo *fw, unsigned channel, float att);

/* RETURNS: previous power state or -1 on error.
 *          Sets new power state: off (state > 0), on (state == 0),
 *          unchanged (state < 0).
 *
 *          Note that all register bits are returned (see datasheet);
 *          nonzero: some stages off, zero: power on.
 */
int
lmh6882Power(FWInfo *fw, int state);

#ifdef __cplusplus
}
#endif

#endif


