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

struct ScopeH5Data {
	hid_t               file_id;
	hid_t               dset_prop_id;
	hid_t               dspace_id;
	hid_t               dset_id;
	hid_t               att1_id;
	hid_t               scal_id;
	ScopeH5SampleType   smpl_type;
	hsize_t            *dims;
	hsize_t            *offs;
	hsize_t            *cnts;
	hsize_t             ndims;
	H5T_order_t         native_order;
};

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

static ScopeH5Data *
scope_h5_do_create(const char *fnam, ScopeH5SampleType dset_type, unsigned precision, unsigned bitShift, ScopeH5SampleType mem_type, const size_t *dims, const ScopeDataDimension *hdims, size_t ndims, const void *data)
{
#ifndef CONFIG_WITH_HDF5
	fprintf(stderr, "scope_h5_open -- HDF5 support not compiled in, sorry\n");
	return NULL;
#else
ScopeH5Data *h5d = NULL;
ScopeH5Data *ret = NULL;
herr_t       hstat;
size_t       i;
int          compression = 4; /* compression level; 0..9; set to < to disable */
hid_t        mem_type_id = H5I_INVALID_HID;
hid_t        set_type_id = H5I_INVALID_HID;

	if ( ! (h5d = calloc( sizeof(*h5d), 1 )) ) {
		fprintf(stderr, "scope_h5_create: No memory\n");
		return h5d;
	}
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
	if ( ! (h5d->offs = calloc( sizeof(*h5d->offs), ndims )) ) {
		fprintf(stderr, "scope_h5_create: No memory\n");
		goto cleanup;
	}
	if ( ! (h5d->cnts = calloc( sizeof(*h5d->cnts), ndims )) ) {
		fprintf(stderr, "scope_h5_create: No memory\n");
		goto cleanup;
	}
	h5d->ndims = ndims;
	if ( dims ) {
		for ( i = 0; i < ndims; ++i ) {
			h5d->dims[i] = h5d->cnts[i] = dims[i];
		}
	} else {
		for ( i = 0; i < ndims; ++i ) {
			h5d->dims[i] = hdims[i].maxlen;
			h5d->offs[i] = hdims[i].offset;
			if ( 0 == (h5d->cnts[i] = hdims[i].actlen) ) {
				h5d->cnts[i] = h5d->dims[i];
			}
		}
	}

	if ( H5T_ORDER_ERROR == (h5d->native_order = H5Tget_order( H5T_NATIVE_INT )) ) {
		fprintf(stderr, "H5Tget_order(H5T_NATIVE_INT) failed\n");
		goto cleanup;
	}

	mem_type_id = map_type( mem_type, bitShift, precision );
	if ( H5I_INVALID_HID == mem_type_id ) {
		goto cleanup;
	}

	set_type_id = map_type( dset_type, bitShift, precision );
	if ( H5I_INVALID_HID == set_type_id ) {
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

	if ( H5Sselect_hyperslab( h5d->dspace_id, H5S_SELECT_SET, h5d->offs, NULL, h5d->cnts, NULL ) < 0 ) {
		fprintf(stderr, "H5Sselect_hyperslab failed\n");
		goto cleanup;
	}

	h5d->dset_id = H5Dcreate( h5d->file_id, "/scopeData", set_type_id, h5d->dspace_id, H5P_DEFAULT, h5d->dset_prop_id, H5P_DEFAULT );
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

	if ( (hstat = H5Dwrite( h5d->dset_id, mem_type_id, H5S_ALL, H5S_ALL, H5P_DEFAULT, data ) ) < 0 ) {
		fprintf(stderr, "H5Dwrite failed\n");
		goto cleanup;
	}

	ret = h5d;
	h5d = NULL;

cleanup:
	if ( H5I_INVALID_HID != set_type_id && set_type_id != mem_type_id ) {
		if ( H5Tclose( set_type_id ) ) {
			fprintf(stderr, "WARNING: H5Tclose failed\n");
		}
	}
	if ( H5I_INVALID_HID != mem_type_id ) {
		if ( H5Tclose( mem_type_id ) ) {
			fprintf(stderr, "WARNING: H5Tclose failed\n");
		}
	}
	if ( h5d ) {
		scope_h5_close( h5d );
	}
	return ret;
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

	free( h5d->dims );
	free( h5d->offs );
	free( h5d->cnts );

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
	if ( (st = scope_h5_add_string_attr( h5d, H5K_DATE, whenstr )) < 0 ) {
		return st;
	}
	return 0;
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

	if ( (st = scope_h5_add_string_attr( h5d, H5K_TRG_SRC, s )) < 0 ) {
		return st;
	}

	s =rising ? "rising" : "falling";
	if ( (st = scope_h5_add_string_attr( h5d, H5K_TRG_EDGE, s )) < 0 ) {
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
AcqParams   acqParams;
unsigned    u[scope_get_num_channels( scp )];
double      d[scope_get_num_channels( scp )];
double      scaleVolts[scope_get_num_channels( scp )];
int         chnl;
time_t      now = time( NULL );
int         st;

	if ( (st = acq_set_params( scp, NULL, &acqParams )) < 0 ) {
		return st;
	}

	for ( chnl = 0; chnl < scope_get_num_channels( scp ); ++chnl ) {
		if ( (st = scope_get_current_scale( scp, chnl, scaleVolts + chnl )) < 0 ) {
			return st;
		}
	}
	if ( (st = scope_h5_add_double_attr( h5d, H5K_SCALE_VOLT, scaleVolts, chnl )) < 0 ) {
		return st;
	}

	u[0] = acqParams.cic0Decimation * acqParams.cic1Decimation;
	if ( (st = scope_h5_add_uint_attr( h5d, H5K_DECIMATION, u, 1 )) < 0 ) {
		return st;
	}

	d[0] = buf_get_sampling_freq( scp );
	if ( (st = scope_h5_add_double_attr( h5d, H5K_CLOCK_F_HZ, d, 1 )) < 0 ) {
		return st;
	}

	u[0] = acqParams.npts;
	if ( (st = scope_h5_add_uint_attr( h5d, H5K_NPTS, u, 1 )) < 0 ) {
		return st;
	}

	if ( (st = scope_h5_add_trigger_source( h5d, acqParams.src, acqParams.rising )) < 0 ) {
		return st;
	}

	d[0] = 0.0/0.0;
	switch ( acqParams.src ) {
		case CHA : d[0] = scaleVolts[0]; break;
		case CHB : d[0] = scaleVolts[1]; break;
		default  :                       break;
	}

	if ( ! isnan( d[0] ) ) {
		d[0] *= acqParams.level/32767.0 * (double)(1 << (buf_get_sample_size( scp ) - 1));
	}
	if ( (st = scope_h5_add_double_attr( h5d, H5K_TRG_L_VOLT, d, 1 )) < 0 ) {
		return st;
	}

	for ( chnl = 0; chnl < scope_get_num_channels( scp ); ++chnl ) {
		st = pgaGetAtt( scp, chnl, d + chnl );
		if ( st < 0 ) {
			if ( -ENOTSUP != st ) {
				return st;
			}
			break;
		}
	}
	if ( 0 == st ) {
		if ( (st = scope_h5_add_double_attr( h5d, H5K_PGA_ATT_DB, d, chnl )) < 0 ) {
			return st;
		}
	}


	for ( chnl = 0; chnl < scope_get_num_channels( scp ); ++chnl ) {
		st = fecGetAtt( scp, chnl, d + chnl );
		if ( st < 0 ) {
			if ( -ENOTSUP != st ) {
				return st;
			}
			break;
		}
	}
	if ( 0 == st ) {
		if ( (st = scope_h5_add_double_attr( h5d, H5K_FEC_ATT_DB, d, chnl )) < 0 ) {
			return st;
		}
	}

	for ( chnl = 0; chnl < scope_get_num_channels( scp ); ++chnl ) {
		st = fecGetTermination( scp, chnl );
		if ( st < 0 ) {
			if ( -ENOTSUP != st ) {
				return st;
			}
			break;
		}
		d[chnl] = st ? 50.0 : 1.0E6;
	}
	if ( 0 <= st ) {
		if ( (st = scope_h5_add_double_attr( h5d, H5K_FEC_TERM, d, chnl )) < 0 ) {
			return st;
		}
	}

	for ( chnl = 0; chnl < scope_get_num_channels( scp ); ++chnl ) {
		st = fecGetACMode( scp, chnl );
		if ( st < 0 ) {
			if ( -ENOTSUP != st ) {
				return st;
			}
			break;
		}
		u[chnl] = !!st;
	}
	if ( 0 <= st ) {
		if ( (st = scope_h5_add_uint_attr( h5d, H5K_FEC_CPLING, u, chnl )) < 0 ) {
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
		if ( (st = scope_h5_add_uint_attr( h5d, H5K_OVERRANGE, u, chnl )) < 0 ) {
			return st;
		}

		u[0] = !! (FW_BUF_HDR_FLG_AUTO_TRIGGERED & bufHdr);
		if ( (st = scope_h5_add_uint_attr( h5d, H5K_TRG_AUTO, u, 1 )) < 0 ) {
			return st;
		}
	}
	return 0;
#endif
}

