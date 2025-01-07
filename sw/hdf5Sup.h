#pragma once

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ScopeH5Data ScopeH5Data;

typedef enum ScopeH5SampleType { INT8_T, INT16_T, INT16LE_T, INT16BE_T, FLOAT_T, DOUBLE_T } ScopeH5SampleType;

void
scope_h5_close(ScopeH5Data *h5d);

ScopeH5Data *
scope_h5_create(const char *fnam, ScopeH5SampleType dtype, unsigned bitShift, size_t *dims, size_t ndims, const void *data);

long
scope_h5_add_uint_attrs( ScopeH5Data *h5d, const char *name, unsigned *val, size_t nval );

long
scope_h5_add_int_attrs( ScopeH5Data *h5d, const char *name, int *val, size_t nval );

long
scope_h5_add_double_attrs( ScopeH5Data *h5d, const char *name, double *val, size_t nval );

long
scope_h5_add_string_attr( ScopeH5Data *h5d, const char *name, const char *val);

#ifdef __cplusplus
}
#endif
