/**LB-MIT
 *
 * MIT License
 *
 * Copyright (c) 2026 Till Straumann
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **LE-MIT*/

#pragma once

#include <sys/types.h>
#include <stdint.h>

#include "cmdXfer.h"

struct FWInfo;

typedef struct FWInfo FWInfo;

/* FW_CMD_APP_REG_xx addresses application-register space */
/* FW_CMD_GEN_REG_xx addresses generic-register space */
typedef enum   FWCmd  { FW_CMD_VERSION, FW_CMD_ADC_BUF, FW_CMD_ADC_FLUSH, FW_CMD_BB_OFF, FW_CMD_BB_I2C, FW_CMD_BB_SPI, FW_CMD_ACQ_PARMS, FW_CMD_SPI, FW_CMD_APP_REG_RD8, FW_CMD_APP_REG_WR8, FW_CMD_GEN_REG_RD8, FW_CMD_GEN_REG_WR8 } FWCmd;

typedef enum   SPIDev { SPI_NONE, SPI_FLASH, SPI_ADC, SPI_PGA, SPI_FEG, SPI_VGA, SPI_VGB } SPIDev;

/* Error return codes of this library are negative ERRNO numbers */

#ifdef __cplusplus
extern "C" {
#endif

uint8_t
fw_get_cmd(FWInfo *fw, FWCmd abstractCmd);

void
fw_set_debug(FWInfo *fw, int level);

FWInfo *
fw_open(const char *devn, unsigned speed);

FWInfo *
fw_open_fd(int fd);

void
fw_close(FWInfo *fw);

uint32_t
fw_get_version(FWInfo *fw);

uint8_t
fw_get_board_version(FWInfo *fw);

#define FW_API_VERSION_1 (1)
#define FW_API_VERSION_2 (2)
#define FW_API_VERSION_3 (3)
#define FW_API_VERSION_4 (4)

uint8_t
fw_get_api_version(FWInfo *fw);

#define FW_API_FUNCTION_GENERIC 0
#define FW_API_FUNCTION_SCOPE   1
uint8_t
fw_get_api_function(FWInfo *fw);

#define FW_FEATURE_SPI_CONTROLLER (1ULL<<0)
#define FW_FEATURE_ADC            (1ULL<<1)

uint64_t
fw_get_features(FWInfo *fw);

/* Disable features selected by 'mask' */
void
fw_disable_features(FWInfo *fw, uint64_t mask);

/* set 'I2C_READ' when writing the i2c address */
#define I2C_READ (1<<0)

/* Low-level i2c commands */
int
bb_i2c_start(FWInfo *fw, int restart);

int
bb_i2c_write(FWInfo *fw, uint8_t *buf, size_t len);

int
bb_i2c_read(FWInfo *fw, uint8_t *buf, size_t len);

int
bb_i2c_stop(FWInfo *fw);

/*
 * RETURNS: read value, -1 on error;
 *
 * SLA is 7-bit address LEFT SHIFTED by 1 bit
 */
int
bb_i2c_read_reg(FWInfo *fw, uint8_t sla, uint8_t reg);

/*
 * RETURNS: 0 on success, -1 on error;
 *
 * SLA is 7-bit address LEFT SHIFTED by 1 bit
 */
int
bb_i2c_write_reg(FWInfo *fw, uint8_t sla, uint8_t reg, uint8_t val);

/* Transfer 'data' buffer to/from destination 'addr'; the direction
 * is encoded in the slave address (read if I2C_READ is set, write
 * otherwise) which is the LEFT SHIFTED 7-bit address.
 * RETURN: number of data (payload) bytes transferred or negative value on
 *         error.
 */
int
bb_i2c_rw_a8(FWInfo *fw, uint8_t sla, uint8_t addr, uint8_t *data, size_t len);

/* Access to raw bit-bang states (for board debugging) */
int
bb_spi_raw(FWInfo *fw, SPIDev type, int clk, int mosi, int cs, int hiz);

typedef enum SPIMode { SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3 } SPIMode;

/* for bidirectional transfers (where SDI/SDO share a single line, e.g., max19507) the
 * optinal zbuf controls the direction (1:  s->m, 0: m->s) of the (bidirectional) SIO line
 */
int
bb_spi_xfer(FWInfo *fw, SPIMode mode, SPIDev type, const uint8_t *tbuf, uint8_t *rbuf, uint8_t *zbuf, size_t len);

typedef struct bb_vec {
	const uint8_t *tbuf;
	uint8_t       *rbuf;
	const uint8_t *zbuf;
	size_t         len;
} bb_vec;

int
bb_spi_xfer_vec(FWInfo *fw, SPIMode mode, SPIDev type, const struct bb_vec *vec, size_t nelms);

/*
 * Low-level transfer for debugging
 */
int
fw_xfer(FWInfo *fw, uint8_t cmd, const uint8_t *tbuf, uint8_t *rbuf, size_t len);

int
fw_xfer_vec(FWInfo *fw, uint8_t cmd, const tbufvec *tbuf, size_t tcnt, const rbufvec *rbuf, size_t rcnt);

uint8_t
fw_spireg_cmd_read(unsigned ch);

uint8_t
fw_spireg_cmd_write(unsigned ch);

/* RETURN: number of bytes read or negative error code */
/* Flags: */
#define REG_FLG_APP      0  /* application register space */
#define REG_FLG_GEN  (1<<0) /* generic register     space */
int
fw_reg_read(FWInfo *fw, uint32_t addr, uint8_t *buf, size_t len, unsigned flags);

/* RETURN: number of bytes written or negative error code */
int
fw_reg_write(FWInfo *fw, uint32_t addr, const uint8_t *buf, size_t len, unsigned flags);

/* Check if FPGA reconfiguration is supported by firmware;
 * RETURN 0 if support is available, negative status otherwise
 */
int
fw_reconfigure_fpga_supported(FWInfo *fw);

/* Request FPGA reconfiguration on closing the connection;
 * returns immediatly with -ENOTSUP if this feature is not supported.
 * A pending reconf. request may be cancelled by providing a zero value.
 * This sets merely a flag; the actual request is sent (unless previously
 * cancelled) by fw_close().
 *
 * Returns 0 if succesfully scheduled or cancelled.
 */
#define FW_RECONFIGURE_FPGA        1
#define FW_CANCEL_RECONFIGURE_FPGA 0
int
fw_reconfigure_fpga_on_close(FWInfo *fw, int val);

/*
 * Send 'invalid' command - can be used to trigger ILAs
 */
int
fw_inv_cmd(FWInfo *fw);

#define BUF_SIZE_FAILED ((long)-1L)
#define BUF_SIZE_NOTSUP ((long)-2L)
int
__fw_has_buf(FWInfo *fw, size_t *psz, unsigned *pflg);

int
__fw_get_sampling_freq_mhz(FWInfo *fw);

#ifdef __cplusplus
}
#endif
