#include <hdf5.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <getopt.h>

#define NCOL 2
#define NELM (16*1024*1024)

static herr_t
addAttr(hid_t dset_id, const char *name, hid_t spc_id, hid_t typ_id, const void *val)
{
	herr_t hstat;
	hid_t  att_id = H5Acreate2( dset_id, name, typ_id, spc_id, H5P_DEFAULT, H5P_DEFAULT );
	if ( H5I_INVALID_HID == att_id ) {
		fprintf(stderr, "H5Acreate2 failed\n");
		return att_id;
	}

	if ( (hstat = H5Awrite( att_id, typ_id, val )) < 0 ) {
		fprintf(stderr, "H5Awrite(%s) failed\n", name);
	}

	if ( (hstat = H5Aclose( att_id )) < 0 ) {
		fprintf(stderr, "H5Aclose(%s) failed\n", name);
	}

	return hstat;
}

static herr_t
addUintAttr(hid_t dset_id, const char *name, hid_t spc_id, unsigned *val) {
	return addAttr(dset_id, name, spc_id, H5T_NATIVE_UINT, val);
}

static herr_t
addIntAttr(hid_t dset_id, const char *name, hid_t spc_id, int *val) {
	return addAttr(dset_id, name, spc_id, H5T_NATIVE_INT, val);
}

static herr_t
addDoubleAttr(hid_t dset_id, const char *name, hid_t spc_id, double *val) {
	return addAttr(dset_id, name, spc_id, H5T_NATIVE_DOUBLE, val);
}

static herr_t
addStringAttr(hid_t dset_id, const char *name, const char *val) {
	hid_t spc_id = H5I_INVALID_HID;
	hid_t typ_id = H5I_INVALID_HID;
	herr_t hstat = -1;
	hsize_t len  = val ? strlen(val) : 0;
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
	spc_id = H5Screate( H5S_SCALAR );
	if ( H5I_INVALID_HID == spc_id ) {
		fprintf(stderr, "addStringAttr: H5Screate failed\n");
		goto cleanup;
	}
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
	hstat = addAttr(dset_id, name, spc_id, typ_id, strp);
cleanup:
	if ( H5I_INVALID_HID != typ_id && H5Tclose( typ_id ) < 0 ) {
		fprintf(stderr, "addStringAttr: H5Tclose failed (ignored)\n");
	}
	if ( H5I_INVALID_HID != spc_id && H5Sclose( spc_id ) < 0 ) {
		fprintf(stderr, "addStringAttr: H5Sclose failed (ignored)\n");
	}
	return hstat;
}

/*
#define H_PREC 10
*/
#define H_DEFL

int
main(int argc, char **argv)
{
	int           rv           = 1;
	hid_t         type_id      = H5I_INVALID_HID;
	hid_t         file_id      = H5I_INVALID_HID;
	hid_t         dset_prop_id = H5I_INVALID_HID;
	hid_t         dspace_id    = H5I_INVALID_HID;
	hid_t         dset_id      = H5I_INVALID_HID;
	hid_t         att1_id      = H5I_INVALID_HID;
	hid_t         scal_id      = H5I_INVALID_HID;
	herr_t        hstat;
	hsize_t       dims[2]      = {NCOL, NELM};
	int16_t       *data        = NULL;
	int i,j;
	uint32_t      att1Val      = 0xdeadbeef;
	double        dblVal       = 1.23456789E78;
	char          strval[100];
	unsigned      prec         = 0;
	unsigned      cmpr         = 0;
	int           shfl         = 0;
	unsigned      mask         = 0xffc0;
	const char   *fnam         = "test.h5";
	int           opt;
	unsigned     *u_p;

	while ( (opt = getopt(argc, argv, "C:Sp:m:f:")) > 0 ) {
		u_p = 0;
		switch ( opt ) {
			case 'C' : u_p  = &cmpr;  break;
			case 'S' : shfl = 1;      break;
			case 'p':  u_p  = &prec;  break;
			case 'm':  u_p  = &mask;  break;
			case 'f':  fnam = optarg; break;
			default:
			break;
		}
		if ( u_p && 1 != sscanf(optarg, "%i", u_p)) {
			fprintf(stderr, "Unable to scan option arg to -%c\n", opt);
			return 1;
		}
	}
	if ( prec > 16 ) {
		fprintf(stderr, "Invalid precision (must be <= 16)\n");
		return 1;
	}
	if ( cmpr > 9 ) {
		fprintf(stderr, "Invalid compression parameter (1..9)\n");
		return 1;
	}

	if ( ! (data = malloc(sizeof(int16_t)*NCOL*NELM)) ) {
		fprintf(stderr, "No memory\n");
		goto cleanup;
	}
	for ( j = 0; j < NCOL; ++j ) {
		for ( i = 0; i < NELM; ++i ) {
			/*data[j*NELM+i] = (j*100+i);*/
			data[j*NELM+i] = random() & mask;
		}
	}

	type_id = H5Tcopy( H5T_NATIVE_SHORT );
	if ( H5I_INVALID_HID == type_id ) {
		fprintf(stderr, "H5Tcopy failed\n");
		goto cleanup;
	}
	if ( prec ) {
		if ( (hstat = H5Tset_precision( type_id, prec )) < 0 ) {
			fprintf(stderr, "H5Tset_precision failed\n");
			goto cleanup;
		}
		if ( (hstat = H5Tset_offset( type_id,  16 - prec )) < 0 ) {
			fprintf(stderr, "H5Tset_offset failed\n");
			goto cleanup;
		}
	}

	file_id = H5Fcreate( fnam, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT );

	if ( H5I_INVALID_HID == file_id ) {
		fprintf(stderr, "H5Fcreate failed\n");
		goto cleanup;
	}

#if 1
	/* NOTE: deflate or any other filter requires chunked layout */
	dset_prop_id = H5Pcreate( H5P_DATASET_CREATE );
	if ( H5I_INVALID_HID == dset_prop_id ) {
		fprintf(stderr, "H5Pcreate failed\n");
		goto cleanup;
	}

	if ( (hstat = H5Pset_chunk( dset_prop_id, sizeof(dims)/sizeof(dims[0]), dims )) < 0 ) {
		fprintf(stderr, "H5Pset_chunk failed\n");
		goto cleanup;
	}

	if ( prec ) {
		if ( (hstat = H5Pset_nbit( dset_prop_id )) < 0 ) {
			fprintf(stderr, "H5Pset_nbit failed\n");
			goto cleanup;
		}
	}

	if ( cmpr ) {
		/* order of filter installment matters! */
		if ( shfl ) {
			if ( (hstat = H5Pset_shuffle( dset_prop_id )) < 0 ) {
				fprintf(stderr, "H5Pset_shuffle failed\n");
				goto cleanup;
			}
		}
		if ( (hstat = H5Pset_deflate( dset_prop_id, cmpr )) < 0 ) {
			fprintf(stderr, "H5Pset_deflate failed\n");
			goto cleanup;
		}
	}
#else
	dset_prop_id = H5P_DEFAULT;
#endif

	dspace_id = H5Screate_simple( sizeof(dims)/sizeof(dims[0]), dims, dims );
	if ( H5I_INVALID_HID == dspace_id ) {
		fprintf(stderr, "H5Screate_simple failed\n");
		goto cleanup;
	}

	dset_id = H5Dcreate( file_id, "/mydata", type_id, dspace_id, H5P_DEFAULT, dset_prop_id, H5P_DEFAULT );
	if ( H5I_INVALID_HID == dset_id ) {
		fprintf(stderr, "H5Dcreate failed\n");
		goto cleanup;
	}

	scal_id = H5Screate( H5S_SCALAR );
	if ( H5I_INVALID_HID == scal_id ) {
		fprintf(stderr, "H5Screate failed\n");
		goto cleanup;
	}

	if ( (hstat = addUintAttr( dset_id, "my_uint_attr", scal_id, &att1Val )) < 0 ) {
		fprintf(stderr, "adding my_uint_attr failed\n");
		goto cleanup;
	}

	if ( (hstat = addDoubleAttr( dset_id, "my_double_attr", scal_id, &dblVal )) < 0 ) {
		fprintf(stderr, "adding my_double_attr failed\n");
		goto cleanup;
	}

	snprintf(strval, sizeof(strval), "%s", "bar");
	if ( (hstat = addStringAttr( dset_id, "foo", strval )) < 0 ) {
		fprintf(stderr, "adding foo failed\n");
		goto cleanup;
	}
	{ struct timespec now;
	clock_gettime( CLOCK_REALTIME, &now );
	tzset();
	struct tm tm;
	localtime_r( &now.tv_sec, &tm );
	strftime( strval, sizeof(strval), "%c %Z", &tm );
/*	ctime_r( &now.tv_sec, strval );
	strcat( strval, tzname[1] ); */
	printf(">>%s<<\n", strval);
	}
	if ( (hstat = addStringAttr( dset_id, "date", strval )) < 0 ) {
		fprintf(stderr, "adding foo failed\n");
		goto cleanup;
	}


	if ( (hstat = H5Dwrite( dset_id, type_id, H5S_ALL, H5S_ALL, H5P_DEFAULT, data ) ) < 0 ) {
		fprintf(stderr, "H5Dwrite failed\n");
		goto cleanup;
	}

	rv = 0;
cleanup:
	if ( H5I_INVALID_HID != scal_id ) {
		if ( (hstat = H5Sclose( scal_id )) ) {
			fprintf(stderr, "H5Sclose failed\n");
		}
	}

	if ( H5I_INVALID_HID != att1_id ) {
		if ( (hstat = H5Aclose( att1_id )) ) {
			fprintf(stderr, "H5Aclose failed\n");
		}
	}

	if ( H5I_INVALID_HID != dset_id ) {
		if ( (hstat = H5Dclose( dset_id )) ) {
			fprintf(stderr, "H5Dclose failed\n");
		}
	}
	if ( H5I_INVALID_HID != dspace_id ) {
		if ( (hstat = H5Sclose( dspace_id )) ) {
			fprintf(stderr, "H5Sclose failed\n");
		}
	}
	if ( H5I_INVALID_HID != dset_prop_id && H5P_DEFAULT != dset_prop_id ) {
		if ( (hstat = H5Pclose( dset_prop_id ) ) < 0 ) {
			fprintf(stderr, "H5Pclose failed\n");
		}
	}

	if ( H5I_INVALID_HID != file_id ) {
		hstat = H5Fclose( file_id );
		if ( hstat < 0 ) {
			fprintf(stderr, "H5Fclose failed\n");
		}
	}

	if ( H5I_INVALID_HID != type_id ) {
		hstat = H5Tclose( type_id );
		if ( hstat < 0 ) {
			fprintf(stderr, "H5Tclose failed\n");
		}
	}

	free(data);

	return rv;
}
