#pragma once

#include <sys/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct AT25Flash;
struct FWInfo;

typedef struct AT25Flash AT25Flash;
typedef struct FWInfo    FWInfo;

AT25Flash *
at25FlashOpen(FWInfo *fw, unsigned instance);

void
at25FlashClose(AT25Flash *flash);

size_t
at25FlashGetBlockSize(AT25Flash *flash);

size_t
at25FlashGetSizeBytes(AT25Flash *flash);

int
at25_spi_read(AT25Flash *flash, unsigned addr, uint8_t *rbuf, size_t len);

int
at25_print_id(AT25Flash *flash);

int64_t
at25_id(AT25Flash *flash);

#define AT25_ST_BUSY       0x01
#define AT25_ST_WEL        0x02
#define AT25_ST_EPE        0x20

int
at25_status(AT25Flash *flash);

int
at25_cmd_2(AT25Flash *flash, uint8_t cmd, int arg);

int
at25_cmd_1(AT25Flash *flash, uint8_t cmd);

int
at25_write_ena(AT25Flash *flash);

int
at25_write_dis(AT25Flash *flash);

int
at25_global_unlock(AT25Flash *flash);

int
at25_global_lock(AT25Flash *flash);

int
at25_status_poll(AT25Flash *flash);

int
at25_block_erase(AT25Flash *flash, unsigned addr, size_t sz);

#define AT25_CHECK_ERASED 1
#define AT25_CHECK_VERIFY 2
#define AT25_EXEC_PROG    4

int
at25_prog(AT25Flash *flash, unsigned addr, const uint8_t *data, size_t len, int check);

/* Send soft reset sequence */
int
at25_reset(AT25Flash *flash);

/* Send wakeup from deep sleep */
int
at25_resume_updwn(AT25Flash *flash);

#ifdef __cplusplus
}
#endif
