#ifndef USBADC_MAX195XX_SUP_H
#define USBADC_MAX195XX_SUP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FWInfo FWInfo;

int
max195xxReadReg( FWInfo *fw, unsigned reg, uint8_t *val );

int
max195xxWriteReg( FWInfo *fw, unsigned reg, uint8_t val );

int
max195xxReset( FWInfo *fw );

/* Return 0 if DLL is locked, -3 if unlocked, other negative value if 
 * read error occurs
 */
int
max195xxDLLLocked( FWInfo *fw );

int
max195xxInit( FWInfo *fw );

typedef enum { NO_TEST, RAMP_TEST, AA55_TEST } Max195xxTestMode;

int
max195xxSetTestMode(FWInfo *fw, Max195xxTestMode m);

typedef enum { CM_0450mV = 7, CM_0600mV = 6, CM_0750mV = 5, CM_0900mV = 0, CM_1050mV = 1, CM_1200mV = 2, CM_1350mV = 3 } Max195xxCMVolt;

int
max195xxSetCMVolt( FWInfo *fw, Max195xxCMVolt cmA, Max195xxCMVolt cmB );

#ifdef __cplusplus
}
#endif


#endif
