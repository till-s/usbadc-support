#ifndef USBADC_FW_COMM_H
#define USBADC_FW_COMM_H

#include <sys/types.h>
#include <stdint.h>

#include "cmdXfer.h"

struct FWInfo;

typedef struct FWInfo FWInfo;

typedef enum   FWCmd  { FW_CMD_VERSION, FW_CMD_ADC_BUF, FW_CMD_BB_I2C, FW_CMD_BB_SPI } FWCmd;

#ifdef __cplusplus
extern "C" {
#endif

uint8_t
fw_get_cmd(FWCmd abstractCmd);

void
fw_set_debug(FWInfo *fw, int level);

FWInfo *
fw_open(const char *devn, unsigned speed);

FWInfo *
fw_open_fd(int fd);

void
fw_close(FWInfo *fw);

int
bb_i2c_start(FWInfo *fw, int restart);

/* set 'I2C_READ' when writing the i2c address */
#define I2C_READ (1<<0)

int
bb_i2c_write(FWInfo *fw, uint8_t *buf, size_t len);

int
bb_i2c_read(FWInfo *fw, uint8_t *buf, size_t len);

int
bb_i2c_stop(FWInfo *fw);

int
bb_spi_cs(FWInfo *fw, int val);

int
bb_spi_xfer_nocs(FWInfo *fw, const uint8_t *tbuf, uint8_t *rbuf, size_t len);

int
bb_spi_xfer(FWInfo *fw, const uint8_t *tbuf, uint8_t *rbuf, size_t len);

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

int
fileMap(const char *fnam,  uint8_t **mapp, off_t *sizp, off_t creatsz);

#ifdef __cplusplus
}
#endif

#endif
