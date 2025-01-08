#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <stdio.h>

#include "hdf5Sup.h"

#ifdef CONFIG_WITH_HDF5
#include <hdf5.h>

struct ScopeH5Data {
	hid_t               type_id;
	hid_t               file_id;
	hid_t               dset_prop_id;
	hid_t               dspace_id;
	hid_t               dset_id;
	hid_t               att1_id;
	hid_t               scal_id;
	ScopeH5SampleType   smpl_type;
	hsize_t            *dims;
	hsize_t             ndims;
	unsigned            bitShift;
};

static herr_t
scope_h5_add_attrs(ScopeH5Data *h5d, const char *name, hid_t typ_id, const void *val, unsigned nval)
{
	herr_t hstat  = -1;
	hid_t  spc_id = H5I_INVALID_HID;
	hid_t  att_id = -1;

	if ( nval > 1 ) {
		hsize_t dim = nval;
		spc_id = H5Screate_simple( 1, &dim, &dim );
		if ( H5I_INVALID_HID == spc_id ) {
			fprintf(stderr, "H5Screate_simple failed\n");
		}
	} else if ( 1 == nval ) {
		spc_id = h5d->scal_id;
	}
	if ( H5I_INVALID_HID == spc_id ) {
		/* reject nval == 0 */
		goto cleanup;
	}
	att_id = H5Acreate2( h5d->dset_id, name, typ_id, spc_id, H5P_DEFAULT, H5P_DEFAULT );
	if ( H5I_INVALID_HID == att_id ) {
		fprintf(stderr, "H5Acreate2 failed\n");
		return att_id;
	}

	if ( (hstat = H5Awrite( att_id, typ_id, val )) < 0 ) {
		fprintf(stderr, "H5Awrite(%s) failed\n", name);
		goto cleanup;
	}

cleanup:
	if ( H5Aclose( att_id ) < 0 ) {
		fprintf(stderr, "WARNING - scope_h5_add_attrs: H5Aclose(%s) failed\n", name);
	}
	if ( H5I_INVALID_HID != spc_id && h5d->scal_id != spc_id ) {
		if ( H5Sclose( spc_id ) < 0 ) {
			fprintf(stderr, "WARNING - scope_h5_add_attrs: H5Sclose failed\n");
		}
	}
	return hstat;
}
#endif

ScopeH5Data *
scope_h5_create(const char *fnam, ScopeH5SampleType dtype, unsigned bitShift, size_t *dims, size_t ndims, const void *data)
{
#ifndef CONFIG_WITH_HDF5
	fprintf(stderr, "scope_h5_open -- HDF5 support not compiled in, sorry\n");
	return NULL;
#else
ScopeH5Data *h5d = NULL;
herr_t       hstat;
size_t       i;
int          compression = 4; /* compression level; 0..9; set to < to disable */
hid_t        baseType;
unsigned     baseTypePrec;

	if ( ! (h5d = calloc( sizeof(*h5d), 1 )) ) {
		fprintf(stderr, "scope_h5_create: No memory\n");
		return h5d;
	}
	h5d->type_id      = H5I_INVALID_HID;
	h5d->file_id      = H5I_INVALID_HID;
	h5d->dset_prop_id = H5I_INVALID_HID;
	h5d->dspace_id    = H5I_INVALID_HID;
	h5d->dset_id      = H5I_INVALID_HID;
	h5d->att1_id      = H5I_INVALID_HID;
	h5d->scal_id      = H5I_INVALID_HID;

	if ( ! (h5d->dims = calloc( sizeof(*h5d->dims), ndims )) ) {
		fprintf(stderr, "scope_h5_create: No memory\n");
		goto cleanup;
	}
	h5d->ndims = ndims;
	for ( i = 0; i < ndims; ++i ) {
		h5d->dims[i] = dims[i];
	}

	switch ( dtype ) {
		case INT8_T    :   baseType = H5T_NATIVE_INT8;    baseTypePrec = sizeof(int8_t)*8;     break;
		case INT16_T   :   baseType = H5T_NATIVE_INT16;   baseTypePrec = sizeof(int16_t)*8;    break;
		case INT16LE_T :   baseType = H5T_STD_I16LE;      baseTypePrec = sizeof(int16_t)*8;    break;
		case INT16BE_T :   baseType = H5T_STD_I16BE;      baseTypePrec = sizeof(int16_t)*8;    break;
		case FLOAT_T   :   baseType = H5T_NATIVE_FLOAT;   baseTypePrec = sizeof(float)*8;      break;
		case DOUBLE_T  :   baseType = H5T_NATIVE_DOUBLE;  baseTypePrec = sizeof(double)*8;     break;
		default:
			fprintf(stderr, "scope_h5_create INTERNAL ERROR -- unsupported datatype\n");
			abort();
	}

	h5d->type_id = H5Tcopy( baseType );
	if ( H5I_INVALID_HID == h5d->type_id ) {
		fprintf(stderr, "H5Tcopy failed\n");
		goto cleanup;
	}
	if ( bitShift ) {
		if ( bitShift >= baseTypePrec ) {
			fprintf(stderr, "Invalid bitShift\n");
			goto cleanup;
		}
		if ( (hstat = H5Tset_precision( h5d->type_id, baseTypePrec - bitShift )) < 0 ) {
			fprintf(stderr, "H5Tset_precision failed\n");
			goto cleanup;
		}
		if ( (hstat = H5Tset_offset( h5d->type_id,  bitShift )) < 0 ) {
			fprintf(stderr, "H5Tset_offset failed\n");
			goto cleanup;
		}
	}

	h5d->file_id = H5Fcreate( fnam, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT );

	if ( H5I_INVALID_HID == h5d->file_id ) {
		fprintf(stderr, "H5Fcreate failed\n");
		goto cleanup;
	}

	/* NOTE: deflate or any other filter requires chunked layout */
	h5d->dset_prop_id = H5Pcreate( H5P_DATASET_CREATE );
	if ( H5I_INVALID_HID == h5d->dset_prop_id ) {
		fprintf(stderr, "H5Pcreate failed\n");
		goto cleanup;
	}

	if ( (hstat = H5Pset_chunk( h5d->dset_prop_id, h5d->ndims, h5d->dims )) < 0 ) {
		fprintf(stderr, "H5Pset_chunk failed\n");
		goto cleanup;
	}

	if ( bitShift ) {
		if ( (hstat = H5Pset_nbit( h5d->dset_prop_id )) < 0 ) {
			fprintf(stderr, "H5Pset_nbit failed\n");
			goto cleanup;
		}
	}

	/* order of filter installment matters! */
	if ( compression >= 0 ) {
		if ( (hstat = H5Pset_shuffle( h5d->dset_prop_id )) < 0 ) {
			fprintf(stderr, "H5Pset_shuffle failed\n");
			goto cleanup;
		}
		if ( (hstat = H5Pset_deflate( h5d->dset_prop_id, compression )) < 0 ) {
			fprintf(stderr, "H5Pset_deflate failed\n");
			goto cleanup;
		}
	}

	h5d->dspace_id = H5Screate_simple( h5d->ndims, h5d->dims, h5d->dims );
	if ( H5I_INVALID_HID == h5d->dspace_id ) {
		fprintf(stderr, "H5Screate_simple failed\n");
		goto cleanup;
	}

	h5d->dset_id = H5Dcreate( h5d->file_id, "/scopeData", h5d->type_id, h5d->dspace_id, H5P_DEFAULT, h5d->dset_prop_id, H5P_DEFAULT );
	if ( H5I_INVALID_HID == h5d->dset_id ) {
		fprintf(stderr, "H5Dcreate failed\n");
		goto cleanup;
	}

	/* create a scalar dataspace for multiple reuse */
	h5d->scal_id = H5Screate( H5S_SCALAR );
	if ( H5I_INVALID_HID == h5d->scal_id ) {
		fprintf(stderr, "H5Screate failed\n");
		goto cleanup;
	}

	if ( (hstat = H5Dwrite( h5d->dset_id, h5d->type_id, H5S_ALL, H5S_ALL, H5P_DEFAULT, data ) ) < 0 ) {
		fprintf(stderr, "H5Dwrite failed\n");
		goto cleanup;
	}

	return h5d;

cleanup:
	scope_h5_close( h5d );
	return NULL;
#endif
}

void
scope_h5_close(ScopeH5Data *h5d)
{
#ifdef CONFIG_WITH_HDF5
herr_t hstat;

	if ( ! h5d ) {
		return;
	}

	if ( H5I_INVALID_HID != h5d->scal_id ) {
		if ( (hstat = H5Sclose( h5d->scal_id )) ) {
			fprintf(stderr, "H5Sclose failed\n");
		}
	}

	if ( H5I_INVALID_HID != h5d->att1_id ) {
		if ( (hstat = H5Aclose( h5d->att1_id )) ) {
			fprintf(stderr, "H5Aclose failed\n");
		}
	}

	if ( H5I_INVALID_HID != h5d->dset_id ) {
		if ( (hstat = H5Dclose( h5d->dset_id )) ) {
			fprintf(stderr, "H5Dclose failed\n");
		}
	}
	if ( H5I_INVALID_HID != h5d->dspace_id ) {
		if ( (hstat = H5Sclose( h5d->dspace_id )) ) {
			fprintf(stderr, "H5Sclose failed\n");
		}
	}
	if ( H5I_INVALID_HID != h5d->dset_prop_id && H5P_DEFAULT != h5d->dset_prop_id ) {
		if ( (hstat = H5Pclose( h5d->dset_prop_id ) ) < 0 ) {
			fprintf(stderr, "H5Pclose failed\n");
		}
	}

	if ( H5I_INVALID_HID != h5d->file_id ) {
		hstat = H5Fclose( h5d->file_id );
		if ( hstat < 0 ) {
			fprintf(stderr, "H5Fclose failed\n");
		}
	}

	if ( H5I_INVALID_HID != h5d->type_id ) {
		hstat = H5Tclose( h5d->type_id );
		if ( hstat < 0 ) {
			fprintf(stderr, "H5Tclose failed\n");
		}
	}

	free( h5d->dims );

	free( h5d );
#endif
}

long
scope_h5_add_uint_attrs( ScopeH5Data *h5d, const char *name, unsigned *val, size_t nval )
{
#ifndef CONFIG_WITH_HDF5
	return -ENOTSUP;
#else
	return scope_h5_add_attrs( h5d, name, H5T_NATIVE_UINT, val, nval );
#endif
}

long
scope_h5_add_int_attrs( ScopeH5Data *h5d, const char *name, int *val, size_t nval )
{
#ifndef CONFIG_WITH_HDF5
	return -ENOTSUP;
#else
	return scope_h5_add_attrs( h5d, name, H5T_NATIVE_INT, val, nval );
#endif
}

long
scope_h5_add_double_attrs( ScopeH5Data *h5d, const char *name, double *val, size_t nval )
{
#ifndef CONFIG_WITH_HDF5
	return -ENOTSUP;
#else
	return scope_h5_add_attrs( h5d, name, H5T_NATIVE_DOUBLE, val, nval );
#endif
}

long
scope_h5_add_string_attr( ScopeH5Data *h5d, const char *name, const char *val)
{
#ifndef CONFIG_WITH_HDF5
	return -ENOTSUP;
#else
	hid_t typ_id     = H5I_INVALID_HID;
	herr_t hstat     = -1;
	hsize_t len      = val ? strlen(val) : 0;
	const void *strp = val;

/* variable length string could also occupy a 1-dimensional array
 * of 1 element of char *
	hsize_t dim = 1;
	const char *stra[1] = { val };
 */

#define VAR_LEN_STR
#ifdef VAR_LEN_STR
    len  = H5T_VARIABLE;
	/* H5T_C_S1 strings of H5T_VARIABLE length are char *, i.e.,
	*  one scalar element is a char *, the address of an element
	*  is char **
	*/
	strp = &val;
#endif
	typ_id = H5Tcopy( H5T_C_S1 );
	if ( H5I_INVALID_HID == typ_id ) {
		fprintf(stderr, "addStringAttr: H5Tcopy failed\n");
		goto cleanup;
	}
	hstat = H5Tset_size( typ_id, len );
	if ( hstat < 0 ) {
		fprintf(stderr, "addStringAttr: H5Tset_size failed\n");
		goto cleanup;
	}
	hstat = scope_h5_add_attrs( h5d, name, typ_id, strp, 1 );
cleanup:
	if ( H5I_INVALID_HID != typ_id && H5Tclose( typ_id ) < 0 ) {
		fprintf(stderr, "addStringAttr: H5Tclose failed (ignored)\n");
	}
	return hstat;
#endif
}
