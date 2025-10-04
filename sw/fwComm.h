#pragma once

#include <sys/types.h>
#include <stdint.h>

#include "cmdXfer.h"

struct FWInfo;

typedef struct FWInfo FWInfo;

typedef enum   FWCmd  { FW_CMD_VERSION, FW_CMD_ADC_BUF, FW_CMD_ADC_FLUSH, FW_CMD_BB_I2C, FW_CMD_BB_SPI, FW_CMD_ACQ_PARMS, FW_CMD_SPI, FW_CMD_REG_RD8, FW_CMD_REG_WR8 } FWCmd;

typedef enum   SPIDev { SPI_NONE, SPI_FLASH, SPI_ADC, SPI_PGA, SPI_FEG, SPI_VGA, SPI_VGB } SPIDev;

/* Error return codes of this library are negative ERRNO numbers */

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

uint32_t
fw_get_version(FWInfo *fw);

uint8_t
fw_get_board_version(FWInfo *fw);

#define FW_API_VERSION_1 (1)
#define FW_API_VERSION_2 (2)
#define FW_API_VERSION_3 (3)

uint8_t
fw_get_api_version(FWInfo *fw);

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

/* Firmware registers; read access fails if not implemented */
#define FW_USR_CSR_REG               4
#define FW_USR_CSR_ADC_PLL_LOCKED    (1<<0)  /* read-only bit that maps to the PLL status  */
/* other bits in USR_CSR are RESERVED for internal use and must not be modifed */


/* RETURN: number of bytes read or negative error code */
int
fw_reg_read(FWInfo *fw, uint32_t addr, uint8_t *buf, size_t len, unsigned flags);

/* RETURN: number of bytes written or negative error code */
int
fw_reg_write(FWInfo *fw, uint32_t addr, const uint8_t *buf, size_t len, unsigned flags);

/*
 * Send 'invalid' command - can be used to trigger ILAs
 */
int
fw_inv_cmd(FWInfo *fw);

int    eepromGetSize(FWInfo *);
int    eepromRead(FWInfo *, unsigned off, uint8_t *buf, size_t len);
int    eepromWrite(FWInfo *, unsigned off, uint8_t *buf, size_t len);

#define BUF_SIZE_FAILED ((long)-1L)
#define BUF_SIZE_NOTSUP ((long)-2L)
int
__fw_has_buf(FWInfo *fw, size_t *psz, unsigned *pflg);

int
__fw_get_sampling_freq_mhz(FWInfo *fw);

#ifdef __cplusplus
}
#endif
