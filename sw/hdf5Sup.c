#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <stdio.h>
#include <math.h>

#include "hdf5Sup.h"

#ifdef CONFIG_WITH_HDF5
#include <hdf5.h>

struct ScopeH5DSpace {
	hid_t               type_id;
	hid_t               dspace_id;
	hsize_t            *dims;
	hsize_t            *offs;
	hsize_t            *cnts;
	hsize_t             rank;
};

struct ScopeH5Data {
	hid_t               file_id;
	hid_t               dset_prop_id;
	hid_t               dset_id;
	hid_t               att1_id;
	hid_t               scal_id;
	ScopeH5SampleType   smpl_type;
	H5T_order_t         native_order;
	ScopeH5DSpace      *dspace;
};


static hid_t
map_type(ScopeH5SampleType typ, unsigned bitShift, unsigned precision)
{
herr_t       hstat;
hid_t        baseType;
unsigned     baseTypePrec;
hid_t        type_id = H5I_INVALID_HID;

	switch ( typ ) {
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

	type_id = H5Tcopy( baseType );
	if ( H5I_INVALID_HID == type_id ) {
		fprintf(stderr, "H5Tcopy failed\n");
		goto cleanup;
	}

	if ( (bitShift || precision) && (FLOAT_T != typ) && (DOUBLE_T != typ) ) {
		if ( 0 == precision ) {
			precision = baseTypePrec - bitShift;
		}
		if ( bitShift + precision > baseTypePrec ) {
			fprintf(stderr, "Invalid bitShift or precision\n");
			goto cleanup;
		}
		if ( (hstat = H5Tset_precision( type_id, precision)) < 0 ) {
			fprintf(stderr, "H5Tset_precision failed\n");
			goto cleanup;
		}
		if ( (hstat = H5Tset_offset( type_id,  bitShift )) < 0 ) {
			fprintf(stderr, "H5Tset_offset failed\n");
			goto cleanup;
		}
	}

	return type_id;

cleanup:
	if ( H5I_INVALID_HID != type_id ) {
		H5Tclose( type_id );
	}
	return H5I_INVALID_HID;
}
#endif

size_t
scope_h5_space_get_rank(const ScopeH5DSpace *spc)
{
#ifndef CONFIG_WITH_HDF5
	return 0;
#else
	return spc ? spc->rank : 0;
#endif
}

ScopeH5DSpace *
scope_h5_get_dspace( ScopeH5Data *h5d)
{
#ifndef CONFIG_WITH_HDF5
	return NULL;
#else
	return h5d ? h5d->dspace : NULL;
#endif
}

ScopeH5DSpace *
scope_h5_space_create(ScopeH5SampleType typ, unsigned bitShift, unsigned precision, const ScopeDataDimension *dims, size_t rank)
{
#ifndef CONFIG_WITH_HDF5
	return NULL;
#else
	ScopeH5DSpace *spc, *rv = NULL;
	size_t         i;

	if ( (spc = calloc( sizeof(*spc), 1 )) ) {
		spc->type_id   = H5I_INVALID_HID;
		spc->dspace_id = H5I_INVALID_HID;
		spc->rank      = rank;
		if ( ! (spc->dims = calloc( sizeof(*spc->dims), rank )) ) {
			fprintf(stderr, "ERROR: scope_h5_space_create() - no memory\n");
			goto bail;
		}
		if ( ! (spc->cnts = calloc( sizeof(*spc->cnts), rank )) ) {
			fprintf(stderr, "ERROR: scope_h5_space_create() - no memory\n");
			goto bail;
		}
		if ( ! (spc->offs = calloc( sizeof(*spc->offs), rank )) ) {
			fprintf(stderr, "ERROR: scope_h5_space_create() - no memory\n");
			goto bail;
		}
		for ( i = 0; i < rank; ++i ) {
			spc->dims[i] = dims[i].maxlen;
			spc->cnts[i] = dims[i].actlen;
			spc->offs[i] = dims[i].offset;
		}	
		spc->type_id = map_type(typ, bitShift, precision);
		if ( H5I_INVALID_HID == spc->type_id ) {
			goto bail;
		}

		spc->dspace_id = H5Screate_simple( spc->rank, spc->dims, spc->dims );
		if ( H5I_INVALID_HID == spc->dspace_id ) {
			fprintf(stderr, "H5Screate_simple failed\n");
			goto bail;
		}
	}
	rv  = spc;
	spc = NULL;

bail:
	scope_h5_space_destroy( spc ); /* NULL if rv holds pointer to successfully created obj */
	return rv;
#endif
}

void
scope_h5_space_destroy(ScopeH5DSpace *spc)
{
#ifdef CONFIG_WITH_HDF5
	if ( spc ) {
		if ( H5I_INVALID_HID != spc->type_id ) {
			if ( H5Tclose( spc->type_id ) < 0 ) {
				fprintf(stderr, "WARNING: scope_h5_space_destroy(): H5Tclose() failed\n");
			}
		}
		if ( H5I_INVALID_HID != spc->dspace_id ) {
			if ( H5Sclose( spc->dspace_id ) < 0 ) {
				fprintf(stderr, "WARNING: scope_h5_space_destroy(): H5Sclose() failed\n");
			}
		}
		free( spc->dims );
		free( spc->offs );
		free( spc->cnts );
		free( spc );
	}
#endif
}

long
scope_h5_space_select(ScopeH5DSpace *spc, const ScopeDataDimension *dims, size_t rank)
{
#ifndef CONFIG_WITH_HDF5
	return -ENOTSUP;
#else
	size_t i;
	herr_t hstat;
	if ( spc->rank != rank ) {
		fprintf(stderr, "scope_h5_space_select: invalid rank\n");
		return -1;
	}
	for ( i = 0; i < rank; ++i ) {
		spc->cnts[i] = dims[i].actlen;
		spc->offs[i] = dims[i].offset;
	}

	if ( (hstat = H5Sselect_none( spc->dspace_id )) < 0 ) {
		fprintf(stderr, "scope_h5_space_select: H5Sselect_none() failed\n");
		return hstat;
	}

	if ( (hstat = H5Sselect_hyperslab( spc->dspace_id, H5S_SELECT_SET, spc->offs, NULL, spc->cnts, NULL )) < 0 ) {
		fprintf(stderr, "scope_h5_space_select: H5Sselect_hyperslab() failed\n");
		return hstat;
	}
	return 0;
#endif
}

#ifdef CONFIG_WITH_HDF5
static herr_t
scope_h5_add_attr(ScopeH5Data *h5d, const char *name, hid_t typ_id, const void *val, unsigned nval)
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
		fprintf(stderr, "WARNING - scope_h5_add_attr: H5Aclose(%s) failed\n", name);
	}
	if ( H5I_INVALID_HID != spc_id && h5d->scal_id != spc_id ) {
		if ( H5Sclose( spc_id ) < 0 ) {
			fprintf(stderr, "WARNING - scope_h5_add_attr: H5Sclose failed\n");
		}
	}
	return hstat;
}
#endif


static ScopeH5Data *
scope_h5_do_create(const char *fnam, ScopeH5SampleType dset_type, unsigned precision, unsigned bitShift, ScopeH5SampleType mem_type, const size_t *dims, const ScopeDataDimension *hdims, size_t ndims, const void *data)
{
#ifndef CONFIG_WITH_HDF5
	fprintf(stderr, "scope_h5_open -- HDF5 support not compiled in, sorry\n");
	return NULL;
#else
ScopeH5Data        *h5d = NULL;
ScopeH5Data        *ret = NULL;
herr_t              hstat;
size_t              i;
int                 compression        = 4; /* compression level; 0..9; set to < to disable */
hid_t               mem_type_id        = H5I_INVALID_HID;
ScopeDataDimension *tmpDim             = NULL;

	if ( ! (h5d = calloc( sizeof(*h5d), 1 )) ) {
		fprintf(stderr, "scope_h5_create: No memory\n");
		return h5d;
	}
	h5d->file_id      = H5I_INVALID_HID;
	h5d->dset_prop_id = H5I_INVALID_HID;
	h5d->att1_id      = H5I_INVALID_HID;
	h5d->scal_id      = H5I_INVALID_HID;

	if ( dims ) {
		if ( ! (tmpDim = calloc( sizeof(*tmpDim), ndims )) ) {
			goto cleanup;
		}
		
		for ( i = 0; i < ndims; ++i ) {
			tmpDim[i].maxlen = tmpDim[i].actlen = dims[i];
		}
		hdims = tmpDim;
	}

	h5d->dspace = scope_h5_space_create( dset_type, bitShift, precision, hdims, ndims );
	if ( ! h5d->dspace ) {
		goto cleanup;
	}

	if ( H5T_ORDER_ERROR == (h5d->native_order = H5Tget_order( H5T_NATIVE_INT )) ) {
		fprintf(stderr, "H5Tget_order(H5T_NATIVE_INT) failed\n");
		goto cleanup;
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

	if ( (hstat = H5Pset_chunk( h5d->dset_prop_id, h5d->dspace->rank, h5d->dspace->dims )) < 0 ) {
		fprintf(stderr, "H5Pset_chunk failed\n");
		goto cleanup;
	}

	if ( bitShift ) {
		/* The nbit filter does not seem to be applied in the way the 'tech notes' suggest (there it says
		 * that the filters are applied after data-type conversion, before writing the output.
		 * It seems that the 'shift/offset' part of the nbit filter is actually part of the datatype
		 * itself and gets applied when reading from the memory.
		 * a MEM: int16/shift=x/precision => DATASET: int16/shift/precision transformation results
		 * in the dumped contents (h5dump) showing just 'precision'; the shift is not reported and
		 * not applied; e.g., if the MEM holds a value of  0x0010 and is reported to have type int16
         * with shift 4, precision 8 and the DATASET type has the same shift and precision then the
		 * value extracted from the dataset is 0x04 with a precision of 8 (shift has disappeared).
		 *
		 * OTOH, if the DATASET is int16/shift=0/precision=16 and the MEM type is as above then
		 * in the data set we still find the value 0x0004 (albeit with a precision of 16).
		if ( (hstat = H5Pset_nbit( h5d->dset_prop_id )) < 0 ) {
			fprintf(stderr, "H5Pset_nbit failed\n");
			goto cleanup;
		}
		 */
		if ( (hstat = H5Pset_scaleoffset( h5d->dset_prop_id, H5Z_SO_INT, 0 )) < 0 ) {
			fprintf(stderr, "H5Pset_scaleoffset failed\n");
			goto cleanup;
		}
printf("Applied scaleoffset (%d)\n", hstat);
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

	h5d->dset_id = H5Dcreate( h5d->file_id, "/scopeData", h5d->dspace->type_id, h5d->dspace->dspace_id, H5P_DEFAULT, h5d->dset_prop_id, H5P_DEFAULT );
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

	if ( data ) {
		for ( i = 0; i < ndims; ++i ) {
			if ( 0 == h5d->dspace->cnts[i] ) {
				h5d->dspace->cnts[i] = h5d->dspace->dims[i];
			}
		}
		if ( H5Sselect_hyperslab( h5d->dspace->dspace_id, H5S_SELECT_SET, h5d->dspace->offs, NULL, h5d->dspace->cnts, NULL ) < 0 ) {
			fprintf(stderr, "H5Sselect_hyperslab failed\n");
			goto cleanup;
		}
		mem_type_id = map_type(mem_type, bitShift, precision);
		if ( H5I_INVALID_HID == mem_type_id ) {
			goto cleanup;
		}
		if ( (hstat = H5Dwrite( h5d->dset_id, mem_type_id, H5S_ALL, H5S_ALL, H5P_DEFAULT, data ) ) < 0 ) {
			fprintf(stderr, "H5Dwrite failed\n");
			goto cleanup;
		}
	}

	ret = h5d;
	h5d = NULL;

cleanup:
	if ( h5d ) {
		scope_h5_close( h5d );
	}
	if ( H5I_INVALID_HID != mem_type_id ) {
		if ( H5Tclose( mem_type_id ) < 0 ) {
			fprintf(stderr, "WARNING: scope_h5_do_create(): H5Tclose() failed\n");
		}
	}
	free( tmpDim );
	return ret;
#endif
}

long
scope_h5_add_hslab( ScopeH5Data *h5d, const ScopeDataDimension *file_selection, const ScopeH5DSpace *mem_spc , const void *data )
{
#ifndef CONFIG_WITH_HDF5
	return -ENOTSUP;
#else
	herr_t  hstat;
	size_t  i;
	hid_t   file_space_id = H5S_ALL;
	hid_t   mem_space_id  = H5S_ALL;
	hid_t   mem_type_id   = h5d->dspace->type_id;

	if ( file_selection ) {

		if ( (hstat = H5Sselect_none( h5d->dspace->dspace_id )) < 0 ) {
			fprintf(stderr, "H5Sselect_none failed\n");
			goto bail;
		}

		for ( i = 0; i < h5d->dspace->rank; ++i ) {
			h5d->dspace->offs[i] = file_selection[i].offset;
			h5d->dspace->cnts[i] = file_selection[i].actlen;
		}
		if ( (hstat = H5Sselect_hyperslab( h5d->dspace->dspace_id, H5S_SELECT_SET, h5d->dspace->offs, NULL, h5d->dspace->cnts, NULL )) < 0 ) {
			fprintf(stderr, "H5Sselect_hyperslab (file dataspace) failed\n");
			goto bail;
		}

		file_space_id = h5d->dspace->dspace_id;
	}

	if ( mem_spc ) {
		mem_space_id = mem_spc->dspace_id;
		mem_type_id  = mem_spc->type_id;
	}

	if ( (hstat = H5Dwrite( h5d->dset_id, mem_type_id, mem_space_id, file_space_id, H5P_DEFAULT, data ) ) < 0 ) {
		fprintf(stderr, "H5Dwrite failed\n");
		goto bail;
	}
	hstat = 0;

bail:
	return hstat;
#endif
}


ScopeH5Data *
scope_h5_create(const char *fnam, ScopeH5SampleType dtype, unsigned bitShift, const size_t *dims, size_t ndims, const void *data)
{
ScopeH5SampleType dsetType = dtype;

	if ( DOUBLE_T == dtype || FLOAT_T == dtype ) {
		if ( 32 == bitShift ) {
			dsetType = FLOAT_T;
			bitShift = 0;
		}
	} else {
		if ( INT16LE_T == dtype || INT16BE_T == dtype ) {
			dsetType = INT16_T;
		}
	}
	return scope_h5_do_create( fnam, dsetType, 0, bitShift, dtype, dims, NULL, ndims, data );
}

ScopeH5Data *
scope_h5_create_from_hslab(const char *fnam, ScopeH5SampleType dset_type, unsigned precision, unsigned bitShift, ScopeH5SampleType mem_type, const ScopeDataDimension *hdims, size_t ndims, const void *data)
{
	return scope_h5_do_create( fnam, dset_type, precision, bitShift, mem_type, NULL, hdims, ndims, data );
}

ScopeH5Data *
scope_h5_create_only(const char *fnam, ScopeH5SampleType dset_type, unsigned precision, unsigned bitShift, const ScopeDataDimension *hdims, size_t ndims) {
	return scope_h5_do_create( fnam, dset_type, precision, bitShift, H5I_INVALID_HID, NULL, hdims, ndims, NULL );
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
	if ( H5I_INVALID_HID != h5d->dset_prop_id && H5P_DEFAULT != h5d->dset_prop_id ) {
		if ( (hstat = H5Pclose( h5d->dset_prop_id ) ) < 0 ) {
			fprintf(stderr, "H5Pclose failed\n");
		}
	}

	scope_h5_space_destroy( h5d->dspace );

	if ( H5I_INVALID_HID != h5d->file_id ) {
		hstat = H5Fclose( h5d->file_id );
		if ( hstat < 0 ) {
			fprintf(stderr, "H5Fclose failed\n");
		}
	}

	free( h5d );
#endif
}

long
scope_h5_add_uint_attr( ScopeH5Data *h5d, const char *name, const unsigned *val, size_t nval )
{
#ifndef CONFIG_WITH_HDF5
	return -ENOTSUP;
#else
	return scope_h5_add_attr( h5d, name, H5T_NATIVE_UINT, val, nval );
#endif
}

long
scope_h5_add_int_attr( ScopeH5Data *h5d, const char *name, const int *val, size_t nval )
{
#ifndef CONFIG_WITH_HDF5
	return -ENOTSUP;
#else
	return scope_h5_add_attr( h5d, name, H5T_NATIVE_INT, val, nval );
#endif
}

long
scope_h5_add_double_attr( ScopeH5Data *h5d, const char *name, const double *val, size_t nval )
{
#ifndef CONFIG_WITH_HDF5
	return -ENOTSUP;
#else
	return scope_h5_add_attr( h5d, name, H5T_NATIVE_DOUBLE, val, nval );
#endif
}

int
scope_h5_add_date(ScopeH5Data *h5d, time_t when)
{
#ifndef CONFIG_WITH_HDF5
	return -ENOTSUP;
#else
char        whenstr[128];
size_t      whenl;
int         st;

	ctime_r( &when, whenstr );
	/* strip trailing '\n' */
	whenl = strlen( whenstr );
	if ( whenl >= 1 ) {
		whenstr[whenl-1] = 0;
	}
	if ( (st = scope_h5_add_string_attr( h5d, SCOPE_KEY_DATE, whenstr )) < 0 ) {
		return st;
	}
	return 0;
#endif
}

long
scope_h5_add_comment( ScopeH5Data *h5d, const char *comment)
{
	return scope_h5_add_string_attr( h5d, "comment", comment );
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
	hstat = scope_h5_add_attr( h5d, name, typ_id, strp, 1 );
cleanup:
	if ( H5I_INVALID_HID != typ_id && H5Tclose( typ_id ) < 0 ) {
		fprintf(stderr, "addStringAttr: H5Tclose failed (ignored)\n");
	}
	return hstat;
#endif
}

int
scope_h5_add_trigger_source( ScopeH5Data *h5d, TriggerSource src, int rising )
{
#ifndef CONFIG_WITH_HDF5
	return -ENOTSUP;
#else
const char *s;
int         st;

	switch ( src ) {
		case CHA : s = "CHA"; break;
		case CHB : s = "CHB"; break;
		default  : s = "EXT"; break;
	}

	if ( (st = scope_h5_add_string_attr( h5d, SCOPE_KEY_TRG_SRC, s )) < 0 ) {
		return st;
	}

	s =rising ? "rising" : "falling";
	if ( (st = scope_h5_add_string_attr( h5d, SCOPE_KEY_TRG_EDGE, s )) < 0 ) {
		return st;
	}
	return 0;
#endif
}

#define CPY_DBL(d,p,fld) \
	cpy_dbl((d),(p), &((AFEParams*)0)->fld)

static int cpy_dbl(double *d, const ScopeParams *p, double *off) {
	unsigned  ch;
	for ( ch = 0; ch < p->numChannels; ++ch ) {
		if ( isnan( (d[ch] = *(const double*)((uintptr_t)(&p->afeParams[ch]) + (uintptr_t)off))) ) {
			return -1;
		}
	}
	return 0;
}

#define CPY_UNS(d,p,fld) \
	cpy_uns((d),(p), &((AFEParams*)0)->fld)

static int cpy_uns(unsigned *u, const ScopeParams *p, int *off) {
	unsigned  ch;
	for ( ch = 0; ch < p->numChannels; ++ch ) {
		int v = * (const int*) ((uintptr_t)(&p->afeParams[ch]) + (uintptr_t)off);
		if ( v < 0 ) {
			return -1;
		}
		u[ch] = (unsigned)v;
	}
	return 0;
}

int
scope_h5_add_scope_parameters(ScopeH5Data *h5d, const ScopeParams *p) {
#ifndef CONFIG_WITH_HDF5
	return -ENOTSUP;
#else
time_t      now = time( NULL );
int         st;
unsigned    u[p->numChannels];
double      d[p->numChannels];

	if ( CPY_DBL(d, p, currentScaleVolt) ) {
		return -EINVAL;
	}

	printf("ScaleVolt: d[0] %g, d[1] %g, scl[0] %g, scl[1] %g\n", d[0], d[1], p->afeParams[0].currentScaleVolt, p->afeParams[1].currentScaleVolt);

	if ( (st = scope_h5_add_double_attr( h5d, SCOPE_KEY_SCALE_VOLT, d, p->numChannels )) < 0 ) {
		return st;
	}

	u[0] = p->acqParams.cic0Decimation * p->acqParams.cic1Decimation;
	if ( (st = scope_h5_add_uint_attr( h5d, SCOPE_KEY_DECIMATION, u, 1 )) < 0 ) {
		return st;
	}

	d[0] = p->samplingFreqHz;
	if ( (st = scope_h5_add_double_attr( h5d, SCOPE_KEY_CLOCK_F_HZ, d, 1 )) < 0 ) {
		return st;
	}

	u[0] = p->acqParams.npts;
	if ( (st = scope_h5_add_uint_attr( h5d, SCOPE_KEY_NPTS, u, 1 )) < 0 ) {
		return st;
	}

	if ( (st = scope_h5_add_trigger_source( h5d, p->acqParams.src, p->acqParams.rising )) < 0 ) {
		return st;
	}

	if ( (unsigned)p->acqParams.src < p->numChannels ) {
		d[0]  = p->afeParams[p->acqParams.src].currentScaleVolt;
		d[0] *= acq_level_to_percent( p->acqParams.level )/100.0;
	} else {
		d[0]  = 0.0;
	}

	if ( (st = scope_h5_add_double_attr( h5d, SCOPE_KEY_TRG_L_VOLT, d, 1 )) < 0 ) {
		return st;
	}

	if ( 0 == CPY_DBL(d, p, pgaAttDb) ) {
		if ( (st = scope_h5_add_double_attr( h5d, SCOPE_KEY_PGA_ATT_DB, d, p->numChannels )) < 0 ) {
			return st;
		}
	}

	if ( 0 == CPY_DBL(d, p, fecAttDb) ) {
		if ( (st = scope_h5_add_double_attr( h5d, SCOPE_KEY_FEC_ATT_DB, d, p->numChannels )) < 0 ) {
			return st;
		}
	}

	if ( 0 == CPY_DBL(d, p, fecTerminationOhm) ) {
		if ( (st = scope_h5_add_double_attr( h5d, SCOPE_KEY_FEC_TERM, d, p->numChannels )) < 0 ) {
			return st;
		}
	}

	if ( 0 == CPY_UNS(u, p, fecCouplingAC) ) {
		if ( (st = scope_h5_add_uint_attr( h5d, SCOPE_KEY_FEC_CPLING, u, p->numChannels )) < 0 ) {
			return st;
		}
	}

	if ( 0 == CPY_UNS(u, p, dacRangeHi) ) {
		if ( (st = scope_h5_add_uint_attr( h5d, SCOPE_KEY_FEC_DAC_HI, u, p->numChannels )) < 0 ) {
			return st;
		}
	}

	if ( 0 == CPY_DBL(d, p, dacVolt) ) {
		if ( (st = scope_h5_add_double_attr( h5d, SCOPE_KEY_DAC_VOLT, d, p->numChannels )) < 0 ) {
			return st;
		}
	}


	if ( (st = scope_h5_add_date( h5d, now )) < 0 ) {
		return st;
	}

	return 0;
#endif
}

int
scope_h5_add_acq_parameters(ScopePvt *scp, ScopeH5Data *h5d)
{
#ifndef CONFIG_WITH_HDF5
	return -ENOTSUP;
#else
	ScopeParams *parms = scope_alloc_params( scp );
	int          st;
	if ( ! parms ) {
		return -ENOMEM;
	}

	st = scope_get_params( scp, parms );

	if ( 0 == st ) {
		st = scope_h5_add_scope_parameters( h5d, parms );
	}

	scope_free_params( parms );

	return st;
#endif
}

int
scope_h5_add_bufhdr(ScopeH5Data *h5d, unsigned bufHdr, unsigned numChannels)
{
#ifndef CONFIG_WITH_HDF5
	return -ENOTSUP;
#else
int      chnl;
int      st;
	if ( numChannels >= 8 ) {
		return -EINVAL;
	} else {
		unsigned u[numChannels];
		for ( chnl = 0; chnl < numChannels; ++chnl ) {
			u[chnl] = !! (FW_BUF_HDR_FLG_OVR( chnl ) & bufHdr);
		}
		if ( (st = scope_h5_add_uint_attr( h5d, SCOPE_KEY_OVERRANGE, u, chnl )) < 0 ) {
			return st;
		}

		u[0] = !! (FW_BUF_HDR_FLG_AUTO_TRIGGERED & bufHdr);
		if ( (st = scope_h5_add_uint_attr( h5d, SCOPE_KEY_TRG_AUTO, u, 1 )) < 0 ) {
			return st;
		}
	}
	return 0;
#endif
}

