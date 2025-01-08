#pragma once

#include <inttypes.h>

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
scope_h5_add_uint_attrs( ScopeH5Data *h5d, const char *name, const unsigned *val, size_t nval );

long
scope_h5_add_int_attrs( ScopeH5Data *h5d, const char *name, const int *val, size_t nval );

long
scope_h5_add_double_attrs( ScopeH5Data *h5d, const char *name, const double *val, size_t nval );

long
scope_h5_add_string_attr( ScopeH5Data *h5d, const char *name, const char *val);

#ifdef __cplusplus
}
#endif
