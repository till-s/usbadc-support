#ifndef USBADC_FW_COMM_H
#define USBADC_FW_COMM_H

#include <sys/types.h>
#include <stdint.h>

struct FWInfo;

typedef struct FWInfo FWInfo;

typedef enum   BBMode { BB_MODE_I2C, BB_MODE_SPI } BBMode;

#ifdef __cplusplus
extern "C" {
#endif

void
fw_set_debug(FWInfo *fw, int level);

void
fw_set_mode(FWInfo *fw, BBMode mode);

FWInfo *
fw_open(const char *devn, unsigned speed, BBMode mode);

void
fw_close(FWInfo *fw);

int
bb_i2c_start(FWInfo *fw, int restart);

int
bb_i2c_read(FWInfo *fw, uint8_t *buf, size_t len);

int
bb_i2c_write(FWInfo *fw, uint8_t *buf, size_t len);

int
bb_i2c_stop(FWInfo *fw);

int
bb_spi_cs(FWInfo *fw, int val);

int
bb_spi_xfer_nocs(FWInfo *fw, const uint8_t *tbuf, uint8_t *rbuf, size_t len);

int
bb_spi_xfer(FWInfo *fw, const uint8_t *tbuf, uint8_t *rbuf, size_t len);

int
bb_spi_read(FWInfo *fw, unsigned addr, uint8_t *rbuf, size_t len);

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

int
fileMap(const char *fnam,  uint8_t **mapp, off_t *sizp, off_t creatsz);

#ifdef __cplusplus
}
#endif

#endif
