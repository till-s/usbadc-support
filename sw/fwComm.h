#ifndef USBADC_FW_COMM_H
#define USBADC_FW_COMM_H

#include <sys/types.h>
#include <stdint.h>

#include "cmdXfer.h"

struct FWInfo;

typedef struct FWInfo FWInfo;

typedef enum   FWCmd  { FW_CMD_VERSION, FW_CMD_ADC_BUF, FW_CMD_BB_I2C, FW_CMD_BB_SPI, FW_CMD_ACQ_PARMS, FW_CMD_SPI, FW_CMD_REG_RD8, FW_CMD_REG_WR8 } FWCmd;

typedef enum   SPIDev { SPI_NONE, SPI_FLASH, SPI_ADC, SPI_PGA, SPI_FEG, SPI_VGA, SPI_VGB } SPIDev;

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

uint8_t
fw_get_api_version(FWInfo *fw);

int
bb_i2c_start(FWInfo *fw, int restart);

#define FW_FEATURE_SPI_CONTROLLER (1ULL<<0)
uint64_t
fw_get_features(FWInfo *fw);

/* Disable features selected by 'mask' */
void
fw_disable_features(FWInfo *fw, uint64_t mask);

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

/* Unspecified error   */
#define FW_CMD_ERR         (-1)
/* Timeout             */
#define FW_CMD_ERR_TIMEOUT (-2)
/* Unsupported command */
#define FW_CMD_ERR_NOTSUP  (-3)
/* Invalid arguments   */
#define FW_CMD_ERR_INVALID (-4)
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

int
fw_reg_read(FWInfo *fw, uint32_t addr, uint8_t *buf, size_t len, unsigned flags);

int
fw_reg_write(FWInfo *fw, uint32_t addr, const uint8_t *buf, size_t len, unsigned flags);

/*
 * ADC Buffer / acquisition readout
 */

unsigned long
buf_get_size(FWInfo *);

/* buffer uses 16-bit samples */
#define FW_BUF_FLG_16B (1<<0)
uint8_t
buf_get_flags(FWInfo *);

int
buf_flush(FWInfo *);

int
buf_read(FWInfo *, uint16_t *hdr, uint8_t *buf, size_t len);

int
buf_read_flt(FWInfo *fw, uint16_t *hdr, float *buf, size_t nelms);

/*
 * Send 'invalid' command - can be used to trigger ILAs
 */
int
fw_inv_cmd(FWInfo *fw);

typedef enum TriggerSource { CHA, CHB, EXT } TriggerSource;

/* Immediate (manual) trigger can be achieved by
 * setting the auto-timeout to 0
 */

#define ACQ_PARAM_MSK_SRC (1<<0) /* trigger source                */
#define ACQ_PARAM_MSK_EDG (1<<1) /* trigger edge                  */
#define ACQ_PARAM_MSK_LVL (1<<2) /* trigger level and hysteresis  */
#define ACQ_PARAM_MSK_NPT (1<<3) /* number of pre-trigger samples */
#define ACQ_PARAM_MSK_AUT (1<<4) /* auto timeout                  */
#define ACQ_PARAM_MSK_DCM (1<<5) /* decimation                    */
#define ACQ_PARAM_MSK_SCL (1<<6) /* scale                         */
/* number of samples requires firmware V2 */
#define ACQ_PARAM_MSK_NSM (1<<7) /* number of samples to acquire  */

#define ACQ_LD_SCALE_ONE 30
#define ACQ_SCALE_ONE (1L<<ACQ_LD_SCALE_ONE)


#define ACQ_PARAM_MSK_GET (0)
#define ACQ_PARAM_MSK_ALL (0xff)

#define ACQ_PARAM_TIMEOUT_INF (0xffff)

typedef struct AcqParams {
    uint32_t      mask;
	TriggerSource src;
	int           rising;
	int16_t       level;
    uint16_t      hysteresis;
	uint32_t      npts;
	uint32_t      nsamples;
	uint32_t      autoTimeoutMS;
	uint8_t       cic0Decimation;
	uint32_t      cic1Decimation;
	uint8_t       cic0Shift;
	uint8_t       cic1Shift;
	int32_t       scale;
} AcqParams;

/* Set new parameters and obtain previous parameters.
 * A new acquisition is started if any mask bit is set.
 *
 * Either 'set' or 'get' may be NULL with obvious semantics.
 */

int
acq_set_params(FWInfo *, AcqParams *set, AcqParams *get);

/*
 * Helpers
 */
int
acq_manual(FWInfo *);

int
acq_set_level(FWInfo *, int16_t level, uint16_t hysteresis);

int
acq_set_npts(FWInfo *, uint32_t npts);

int
acq_set_nsamples(FWInfo *, uint32_t nsamples);

int32_t
acq_default_cic1Scale(uint32_t cic1Decimation);

int
acq_set_decimation(FWInfo *, uint8_t cic0Decimation, uint32_t cic1Decimation);

int
acq_set_scale(FWInfo *, uint8_t cic0RShift, uint8_t cic1RShift, int32_t scale);

/* rising: 1, falling: -1, leave previous value: 0 */
int
acq_set_source(FWInfo *, TriggerSource src, int rising);

int
acq_set_autoTimeoutMs(FWInfo *, uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif
