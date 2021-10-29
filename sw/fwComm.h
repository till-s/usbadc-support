#ifndef USBADC_FW_COMM_H
#define USBADC_FW_COMM_H

#include <sys/types.h>
#include <stdint.h>

#include "cmdXfer.h"

struct FWInfo;

typedef struct FWInfo FWInfo;

typedef enum   FWCmd  { FW_CMD_VERSION, FW_CMD_ADC_BUF, FW_CMD_BB_I2C, FW_CMD_BB_SPI } FWCmd;

typedef enum   SPIDev { SPI_NONE, SPI_FLASH, SPI_ADC, SPI_PGA } SPIDev;

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

/*
 * RETURNS: read value, -1 on error;
 */
int
bb_i2c_read_reg(FWInfo *fw, uint8_t sla, uint8_t reg);

/*
 * RETURNS: 0 on success, -1 on error;
 */
int
bb_i2c_write_reg(FWInfo *fw, uint8_t sla, uint8_t reg, uint8_t val);

int
bb_spi_cs(FWInfo *fw, SPIDev type, int val);

/* for bidirectional transfers (where SDI/SDO share a single line, e.g., max19507) the
 * optinal zbuf controls the direction (1:  s->m, 0: m->s) of the (bidirectional) SIO line
 */
int
bb_spi_xfer_nocs(FWInfo *fw, SPIDev type, const uint8_t *tbuf, uint8_t *rbuf, uint8_t *zbuf, size_t len);

int
bb_spi_xfer(FWInfo *fw, SPIDev type, const uint8_t *tbuf, uint8_t *rbuf, uint8_t *zbuf, size_t len);

#ifdef __cplusplus
}
#endif

#endif
