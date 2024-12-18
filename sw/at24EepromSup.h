#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct FWInfo;

typedef struct AT24EEPROM AT24EEPROM;

AT24EEPROM *at24EepromCreate(
	struct FWInfo *fw,
	uint8_t        sla,    /* 7-bit address (e.g., 0x50) */
	unsigned       size,   /* size in bytes              */
    unsigned       pgSize  /* page size                  */
);

void
at24EepromDestroy(AT24EEPROM *);

int
at24EepromGetSize(AT24EEPROM *eeprom);

/* return bytes read or negative status on error */
int
at24EepromRead(AT24EEPROM *eeprom, unsigned off, uint8_t *buf, size_t len);

/* return bytes written or negative status on error */
int
at24EepromWrite(AT24EEPROM *eeprom, unsigned off, uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif
