#pragma once

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#include "scopeCalData.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ScopePvt ScopePvt;
typedef struct FWInfo   FWInfo;
typedef struct UnitData UnitData;


/* Return 1 if the the firmware has been initialized
 * already, 0 otherwise.
 */

int
scope_is_initialized(FWInfo *fw);

ScopePvt *
scope_open(FWInfo *fw);

void
scope_close(ScopePvt *scp);

/* If 'force' is nonzero the board is initialized
 * even if it appears to have been initialized already.
 *
 * RETURNS: zero on success, negative errno on error.
 */
int
scope_init(ScopePvt *scp, int force);

/* Scope Support */

unsigned
scope_get_num_channels(ScopePvt *scp);

/* full-scale at zero attenuation */
int
scope_get_full_scale_volt(ScopePvt *scp, unsigned channel, double *fullScaleVolt);

int
scope_set_full_scale_volt(ScopePvt *scp, unsigned channel, double fullScaleVolt);

int
scope_get_current_scale(ScopePvt *scp, unsigned channel, double *scl);

/* Reference frequency of ADC PLL; NaN if there is no PLL */
double
scope_get_reference_freq(ScopePvt *scp);

/* Return
 * 0        if the ADC PLL is locked,
 * -EBUSY   if not locked
 * -ENOTSUP if functionality is not implemented,
 * ...      other negative errno if other errors are
 *          encountered.
 */
int
scope_adc_pll_locked(ScopePvt *scp);

int
scope_get_cal_data(ScopePvt *scp, ScopeCalData *calDataArray, unsigned nelms);

/* A NULL pointer may be passed for calDataArray which causes
 * all calibration to be reset (offsetVolt => 0.0, scaleRelat => 1.0)
 */
int
scope_set_cal_data(ScopePvt *scp, const ScopeCalData *calDataArray, unsigned nelms);

/*
 * A NULL pointer may be passed for 'unitData' in which case
 * the non-volatile storage is cleared/erased w/o writing any
 * new data.
 */
int
scope_write_unit_data_nonvolatile(ScopePvt *scp, UnitData *unitData);

/*
 * ADC Buffer / acquisition readout
 */

unsigned long
buf_get_size(ScopePvt *);

/* buffer uses 16-bit samples */
#define FW_BUF_FLG_16B (1<<0)

unsigned
buf_get_flags(ScopePvt *);

/* Requres API vers. 3; returns -ENOTSUP if API is older */
int
buf_get_sample_size(ScopePvt *);

/* Requres API vers. 3; returns NaN if not supported */
double
buf_get_sampling_freq(ScopePvt *);

/* Full-scale ADC counts (may be different from buf_get_sample_size
 * if the samples are left-adjusted in a longer word).
 */
int
buf_get_full_scale_ticks(ScopePvt *);

int
buf_flush(ScopePvt *);

#define FW_BUF_HDR_FLG_OVR(ch) (1<<(ch))
#define FW_BUF_HDR_FLG_AUTO_TRIGGERED (1<<8)

/* All buf_read variants return number of bytes read or negative error
 * all buf_read variants also take care of swapping multi-byte samples
 * to host-byte order.
 */
int
buf_read(ScopePvt *, uint16_t *hdr, uint8_t *buf, size_t len);

int
buf_read_flt(ScopePvt *scp, uint16_t *hdr, float *buf, size_t nelms);

int
buf_read_int16(ScopePvt *scp, uint16_t *hdr, int16_t *buf, size_t nelms);

/* enum value = channel index */
typedef enum TriggerSource { CHA = 0, CHB = 1, EXT = 10 } TriggerSource;

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
#define ACQ_PARAM_MSK_TGO (1<<8) /* ext. trigger-output enable    */

#define ACQ_LD_SCALE_ONE 30
#define ACQ_SCALE_ONE (1L<<ACQ_LD_SCALE_ONE)


#define ACQ_PARAM_MSK_GET (0)
#define ACQ_PARAM_MSK_ALL (0xff)

#define ACQ_PARAM_TIMEOUT_INF (0xffff)

typedef struct AcqParams {
	uint32_t      mask;
	/* note that when the source is switched to external then
	 * the 'trigOutEn' feature is automatically switched off
	 * by firmware
	 */
	TriggerSource src;
	int           trigOutEn;
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

typedef struct AFEParams {
	double        fullScaleVolt;
	double        currentScaleVolt;
	double        pgaAttDb;
	double        fecAttDb;
	double        fecTerminationOhm;
	double        postGainOffsetTick;
	int           fecCouplingAC;
	double        dacVolt;
	int           dacRangeHi;
} AFEParams;

struct ScopeParams;

void
scope_copy_params(struct ScopeParams *to, const struct ScopeParams *from);


/* Keys for storing parameters (json, hdf5, ...) */
#define SCOPE_KEY_VERSION    "settingsVersion"
#define SCOPE_SETTINGS_VERSION_1 1
#define SCOPE_KEY_NUM_CHNLS  "numChannels"
#define SCOPE_KEY_CLOCK_F_HZ "clockFrequencyHz"
#define SCOPE_KEY_DATE       "date"

#define SCOPE_KEY_TRG_SRC    "triggerSource"
#define SCOPE_KEY_TRG_OUT_EN "triggerOutEnable"
#define SCOPE_KEY_TRG_MODE   "triggerMode"
#define SCOPE_KEY_TRG_EDGE   "triggerEdge"
#define SCOPE_KEY_TRG_L_VOLT "triggerLevelVolt"
#define SCOPE_KEY_TRG_H_VOLT "triggerHysteresisVolt"
#define SCOPE_KEY_TRG_L_PERC "triggerLevelPercent"
#define SCOPE_KEY_TRG_H_PERC "triggerHysteresisPercent"
#define SCOPE_KEY_NPTS       "numPreTriggerSamples"
#define SCOPE_KEY_NSAMPLES   "numSamples"
#define SCOPE_KEY_TRG_AUTO   "autoTriggered"
#define SCOPE_KEY_AUTOTRG_MS "autoTriggerMilliSeconds"
#define SCOPE_KEY_DECIMATION "decimation"

#define SCOPE_KEY_FULSCL_VLT "fullScaleVolt"
#define SCOPE_KEY_CURSCL_VLT "scaleVolt"
#define SCOPE_KEY_PGA_ATT_DB "pgaAttenuationDB"
#define SCOPE_KEY_FEC_ATT_DB "fecAttenuationDB"
#define SCOPE_KEY_FEC_TERM   "fecTerminationOhm"
#define SCOPE_KEY_FEC_CPLING "fecCouplingAC"
#define SCOPE_KEY_DAC_VOLT   "dacVolt"
#define SCOPE_KEY_DAC_RNG_HI "dacRangeHigh"
#define SCOPE_KEY_OVERRANGE  "overRange"


typedef struct ScopeParams {
#ifdef __cplusplus
	ScopeParams() = delete;
	ScopeParams(const ScopeParams &rhs) = delete;
	ScopeParams &operator=(const ScopeParams &rhs) {
		scope_copy_params(this, &rhs);
		return *this;
	}
#endif
	AcqParams     acqParams;
	double        samplingFreqHz;
	unsigned      numChannels;
	/* Trigger Mode - such as 'continuous, single-shot etc.' is
	 * a software-feature, i.e., this field is provided for convenience;
	 * the semantics are not defined by this library and the value
	 * is not updated (initialized to -1).
	 */
	int           trigMode;
	/* 'numChannels' AFE params attached */
	AFEParams     afeParams[];
} ScopeParams;

ScopeParams *
scope_alloc_params(ScopePvt *);

void
scope_free_params(ScopeParams *);

void
scope_init_params(ScopePvt *scp, ScopeParams *p);

size_t
scope_sizeof_params(ScopePvt *scp);

int
scope_get_params(ScopePvt *, ScopeParams *);

int
scope_set_params(ScopePvt *, ScopeParams *);


/* Set new parameters and obtain previous parameters.
 * A new acquisition is started if any mask bit is set.
 *
 * Either 'set' or 'get' may be NULL with obvious semantics.
 *
 * NOTE: Superseded by 'scope_set_params()'.
 */

int
acq_set_params(ScopePvt *, AcqParams *set, AcqParams *get);

double
scope_trig_level_volt(const ScopeParams *p);

double
scope_trig_hysteresis_volt(const ScopeParams *p);

/*
 * Helpers
 */
int
acq_manual(ScopePvt *);

int
acq_set_level(ScopePvt *, int16_t level, uint16_t hysteresis);

double
acq_level_to_percent(int16_t level);

int16_t
acq_percent_to_level(double percent);

int
acq_set_npts(ScopePvt *, uint32_t npts);

int
acq_set_nsamples(ScopePvt *, uint32_t nsamples);

int32_t
acq_default_cic1Scale(uint32_t cic1Decimation);

int
acq_set_decimation(ScopePvt *, uint8_t cic0Decimation, uint32_t cic1Decimation);

/* auto-compute the cic0Decimation and cic1Decimation;
 *
 * RETURN: 0 on success, -EINVAL or -ERANGE if computation fails (leaving params unchanged)
 */
int
acq_auto_decimation(ScopePvt *, unsigned decimation, uint8_t *cic0Decimation, uint32_t *cic1Decimation);

int
acq_set_scale(ScopePvt *, uint8_t cic0RShift, uint8_t cic1RShift, int32_t scale);

/* rising: 1, falling: -1, leave previous value: 0 */
int
acq_set_source(ScopePvt *, TriggerSource src, int rising);

/* Note that this feature is automatically switched off by firmware
 * if the trigger-source is set to 'external' (uses the same wire)
 */
int
acq_set_trig_out_en(ScopePvt *, int on);

int
acq_set_autoTimeoutMs(ScopePvt *, uint32_t timeout);

typedef struct PGAOps {
	int    (*readReg)(FWInfo *, unsigned ch, unsigned reg);
	int    (*writeReg)(FWInfo *, unsigned ch, unsigned reg, unsigned val);
	/*  min-max attenuation in db; return 0 on success */
	int    (*getAttRangeDb)(FWInfo*, double *min, double *max);
	int    (*getAttDb)(FWInfo *, unsigned channel, double *att);
	int    (*setAttDb)(FWInfo *, unsigned channel, double att);
} PGAOps;

typedef struct FECOps FECOps;

struct FECOps {
	/* returns 0, 1, negative error */
	int    (*getACMode)(FECOps *, unsigned channel);
	int    (*setACMode)(FECOps *, unsigned channel, unsigned on);
	int    (*getTermination)(FECOps *, unsigned channel);
	int    (*setTermination)(FECOps *, unsigned channel, unsigned on);
	/* assume 2-step attenuator on/off */
	int    (*getAttRangeDb)(FECOps*, double *min, double *max);
	int    (*getAttDb)(FECOps *, unsigned channel, double *att);
	int    (*setAttDb)(FECOps *, unsigned channel, double att);
	int    (*getDACRangeHi)(FECOps*, unsigned channel);
	int    (*setDACRangeHi)(FECOps*, unsigned channel, unsigned on);
	void   (*close)(FECOps *);
};

int    pgaReadReg(ScopePvt *, unsigned ch, unsigned reg);
int    pgaWriteReg(ScopePvt *, unsigned ch, unsigned reg, unsigned val);
/* at min-att
 *  min-max attenuation in db; return 0 on success; stage 0 is closest to ADC
 */
int    pgaGetAttRangeDb(ScopePvt*, double *min, double *max);
int    pgaGetAttDb(ScopePvt *, unsigned channel, double *att);
int    pgaSetAttDb(ScopePvt *, unsigned channel, double att);

/* returns 0, 1, negative error */
int    fecGetACMode(ScopePvt *, unsigned channel);
int    fecSetACMode(ScopePvt *, unsigned channel, unsigned on);
int    fecGetTermination(ScopePvt *, unsigned channel);
int    fecGetTerminationOhm(ScopePvt *, unsigned channel, double *val);
int    fecSetTerminationOhm(ScopePvt *, unsigned channel, double val);
int    fecSetTermination(ScopePvt *, unsigned channel, unsigned on);
int    fecGetDACRangeHi(ScopePvt *, unsigned channel);
int    fecSetDACRangeHi(ScopePvt *, unsigned channel, unsigned on);
int    fecGetAttRangeDb(ScopePvt*, double *min, double *max);
int    fecGetAttDb(ScopePvt *, unsigned channel, double *att);
int    fecSetAttDb(ScopePvt *, unsigned channel, double att);
void   fecClose(ScopePvt *);

/* return negative error code or 0 on success */
int    dacGetVoltRange(ScopePvt *, double *pVoltMin, double *pVoltMax);
int    dacGetVolt(ScopePvt *, unsigned channel, double *pvolt);
int    dacSetVolt(ScopePvt *, unsigned channel, double volt);

#ifdef __cplusplus
}
#endif
