#pragma once

#include <inttypes.h>
#include <time.h>

#include <scopeSup.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ScopeH5Data ScopeH5Data;

typedef enum ScopeH5SampleType { INT8_T, INT16_T, INT16LE_T, INT16BE_T, FLOAT_T, DOUBLE_T } ScopeH5SampleType;

void
scope_h5_close(ScopeH5Data *h5d);

// memory covers 0..maxlen - 1
// Data selected by this dimension ranges from offset..actlen-1.
// (actlen == 0 treated as actlen == maxlen)
typedef struct ScopeDataDimension {
	size_t maxlen;
	size_t offset;
	size_t actlen;
} ScopeDataDimension;

/*
 * Store sample data in a HDF5 file.
 *  'bitShift' indicates by how many bits integer samples can be right-shifted (w/o losing
 *  important information) in order to reduce file size.
 *
 * If dtype == DOUBLE_T then bitShift may be set to 32 to indicate that samples should be
 * stored as floats.
 */
ScopeH5Data *
scope_h5_create(const char *fnam, ScopeH5SampleType dtype, unsigned bitShift, const size_t *dims, size_t ndims, const void *data);

/* Save a ndims-dimensional sub-space out ('hyperslab')
 * the 'off' and 'count' arrays must have the same size as 'dims'
 */
ScopeH5Data *
scope_h5_create_from_hslab(const char *fnam, ScopeH5SampleType dset_type, unsigned precision, unsigned bitShift, ScopeH5SampleType mem_type, const ScopeDataDimension *dims, size_t ndims, const void *data);

long
scope_h5_add_uint_attr( ScopeH5Data *h5d, const char *name, const unsigned *val, size_t nval );

long
scope_h5_add_int_attr( ScopeH5Data *h5d, const char *name, const int *val, size_t nval );

long
scope_h5_add_double_attr( ScopeH5Data *h5d, const char *name, const double *val, size_t nval );

long
scope_h5_add_string_attr( ScopeH5Data *h5d, const char *name, const char *val);

#define H5K_SCALE_VOLT "scaleVolt"
#define H5K_DECIMATION "decimation"
#define H5K_CLOCK_F_HZ "clockFrequencyHz"
#define H5K_NPTS       "numPreTriggerSamples"
#define H5K_TRG_L_VOLT "triggerLevelVolt"
#define H5K_FEC_CPLING "fecCouplingAC"
#define H5K_FEC_TERM   "fecTerminationOhm"
#define H5K_FEC_ATT_DB "fecAttenuationDB"
#define H5K_PGA_ATT_DB "pgaAttenuationDB"
#define H5K_OVERRANGE  "overRange"
#define H5K_TRG_AUTO   "autoTriggered"
#define H5K_DATE       "date"
#define H5K_TRG_SRC    "triggerSource"
#define H5K_TRG_EDGE   "triggerEdge"

int
scope_h5_add_acq_parameters(ScopePvt *scp, ScopeH5Data *h5d);

int
scope_h5_add_bufhdr(ScopeH5Data *h5d, unsigned bufHdr, unsigned numChannels);

int
scope_h5_add_date(ScopeH5Data *h5d, time_t when );

int
scope_h5_add_trigger_source(ScopeH5Data *h5d, TriggerSource src, int rising);

#ifdef __cplusplus
}
#endif
