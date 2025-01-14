#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ScopePvt ScopePvt;
typedef struct FWInfo   FWInfo;
typedef struct UnitData UnitData;

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
scope_get_full_scale_volts(ScopePvt *scp, unsigned channel, double *fullScaleVolts);

int
scope_set_full_scale_volts(ScopePvt *scp, unsigned channel, double fullScaleVolts);

int
scope_get_current_scale(ScopePvt *scp, unsigned channel, double *scl);

/* Reference frequency of ADC PLL; NaN if there is no PLL */
double
scope_get_reference_freq(ScopePvt *scp);

/* volts = counts/maxCounts * scaleVolts * scaleRelat - offsetVolts
 * relative gain differences (scaleRelat) may be compensated/calibrated
 * by tuning the PGA.
 * 'scaleVolts' is controlled with scope_get/set_full_scale_volts.
 */
typedef struct ScopeCalData {
	double offsetVolts;
	double scaleRelat;
} ScopeCalData;

int
scope_get_cal_data(ScopePvt *scp, ScopeCalData *calDataArray, unsigned nelms);

/* A NULL pointer may be passed for calDataArray which causes
 * all calibration to be reset (offsetVolts => 0.0, scaleRelat => 1.0)
 */
int
scope_set_cal_data(ScopePvt *scp, ScopeCalData *calDataArray, unsigned nelms);

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

int
buf_flush(ScopePvt *);

#define FW_BUF_HDR_FLG_OVR(ch) (1<<(ch))
#define FW_BUF_HDR_FLG_AUTO_TRIGGERED (1<<8)

int
buf_read(ScopePvt *, uint16_t *hdr, uint8_t *buf, size_t len);

int
buf_read_flt(ScopePvt *scp, uint16_t *hdr, float *buf, size_t nelms);

int
buf_read_int16(ScopePvt *scp, uint16_t *hdr, int16_t *buf, size_t nelms);

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

/* Set new parameters and obtain previous parameters.
 * A new acquisition is started if any mask bit is set.
 *
 * Either 'set' or 'get' may be NULL with obvious semantics.
 */

int
acq_set_params(ScopePvt *, AcqParams *set, AcqParams *get);

/*
 * Helpers
 */
int
acq_manual(ScopePvt *);

int
acq_set_level(ScopePvt *, int16_t level, uint16_t hysteresis);

int
acq_set_npts(ScopePvt *, uint32_t npts);

int
acq_set_nsamples(ScopePvt *, uint32_t nsamples);

int32_t
acq_default_cic1Scale(uint32_t cic1Decimation);

int
acq_set_decimation(ScopePvt *, uint8_t cic0Decimation, uint32_t cic1Decimation);

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
	int    (*getAttRange)(FWInfo*, double *min, double *max);
	int    (*getAtt)(FWInfo *, unsigned channel, double *att);
	int    (*setAtt)(FWInfo *, unsigned channel, double att);
} PGAOps;

typedef struct FECOps FECOps;

struct FECOps {
	/* returns 0, 1, negative error */
	int    (*getACMode)(FECOps *, unsigned channel);
	int    (*setACMode)(FECOps *, unsigned channel, unsigned on);
	int    (*getTermination)(FECOps *, unsigned channel);
	int    (*setTermination)(FECOps *, unsigned channel, unsigned on);
	/* assume 2-step attenuator on/off */
	int    (*getAttRange)(FECOps*, double *min, double *max);
	int    (*getAtt)(FECOps *, unsigned channel, double *att);
	int    (*setAtt)(FECOps *, unsigned channel, double att);
	int    (*getDACRangeHi)(FECOps*, unsigned channel);
	int    (*setDACRangeHi)(FECOps*, unsigned channel, unsigned on);
	void   (*close)(FECOps *);
};

int    pgaReadReg(ScopePvt *, unsigned ch, unsigned reg);
int    pgaWriteReg(ScopePvt *, unsigned ch, unsigned reg, unsigned val);
/* at min-att
 *  min-max attenuation in db; return 0 on success; stage 0 is closest to ADC
 */
int    pgaGetAttRange(ScopePvt*, double *min, double *max);
int    pgaGetAtt(ScopePvt *, unsigned channel, double *att);
int    pgaSetAtt(ScopePvt *, unsigned channel, double att);

/* returns 0, 1, negative error */
int    fecGetACMode(ScopePvt *, unsigned channel);
int    fecSetACMode(ScopePvt *, unsigned channel, unsigned on);
int    fecGetTermination(ScopePvt *, unsigned channel);
int    fecSetTermination(ScopePvt *, unsigned channel, unsigned on);
int    fecGetDACRangeHi(ScopePvt *, unsigned channel);
int    fecSetDACRangeHi(ScopePvt *, unsigned channel, unsigned on);
int    fecGetAttRange(ScopePvt*, double *min, double *max);
int    fecGetAtt(ScopePvt *, unsigned channel, double *att);
int    fecSetAtt(ScopePvt *, unsigned channel, double att);
void   fecClose(ScopePvt *);

/* return negative error code or 0 on success */
int    dacGetVoltsRange(ScopePvt *, double *pVoltsMin, double *pVoltsMax);
int    dacGetVolts(ScopePvt *, unsigned channel, double *pvolts);
int    dacSetVolts(ScopePvt *, unsigned channel, double volts);

#define H5K_SCALE_VOLT "scaleVolt"
#define H5K_DECIMATION "decimation"
#define H5K_CLOCK_F_HZ "clockFrequencyHz"
#define H5K_NPTS       "numPreTriggerSamples"
#define H5K_TRG_L_VOLT "trigLevelVolt"
#define H5K_TRG_SRC    "trigSource"
#define H5K_TRG_EDGE   "trigEdge"
#define H5K_FEC_CPLING "fecCouplingAC"
#define H5K_FEC_TERM   "fecTerminationOhm"
#define H5K_FEC_ATT_DB "fecAttenuationDB"
#define H5K_PGA_ATT_DB "pgaAttenuationDB"
#define H5K_OVERRANGE  "overRange"
#define H5K_TRG_AUTO   "autoTriggered"
#define H5K_DATE       "date"

#include <hdf5Sup.h>

int    scope_h5_write_parameters(ScopePvt *, ScopeH5Data*, unsigned bufHdr);

#ifdef __cplusplus
}
#endif
