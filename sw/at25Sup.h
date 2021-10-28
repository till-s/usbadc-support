#ifndef USBADC_AT25_SUP_H
#define USBADC_AT25_SUP_H

#include <sys/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct FWInfo;

typedef struct FWInfo FWInfo;

int
at25_spi_read(FWInfo *fw, unsigned addr, uint8_t *rbuf, size_t len);

int
at25_id(FWInfo *fw);

int
at25_status(FWInfo *fw);

int
at25_cmd_2(FWInfo *fw, uint8_t cmd, int arg);

int
at25_cmd_1(FWInfo *fw, uint8_t cmd);

int
at25_write_ena(FWInfo *fw);

int
at25_write_dis(FWInfo *fw);

int
at25_global_unlock(FWInfo *fw);

int
at25_global_lock(FWInfo *fw);

int
at25_status_poll(FWInfo *fw);

int
at25_block_erase(FWInfo *fw, unsigned addr, size_t sz);

#define AT25_CHECK_ERASED 1
#define AT25_CHECK_VERIFY 2
#define AT25_EXEC_PROG    4

int
at25_prog(FWInfo *fw, unsigned addr, const uint8_t *data, size_t len, int check);

#ifdef __cplusplus
}
#endif

#endif
