#pragma once

#include "fwComm.h"

#ifdef __cplusplus
extern "C" {
#endif

int
fegRegRead(FWInfo *fw);

int
fegRegWrite(FWInfo *fw, uint8_t v);

#ifdef __cplusplus
}
#endif
