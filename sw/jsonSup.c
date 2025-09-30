#include <scopeSup.h>
#include <jsonSup.h>
#include <errno.h>
#include <math.h>

#ifdef CONFIG_WITH_JANSSON
#include <jansson.h>
#endif

/* Check if JSON support is available
 * RETURN: 0 if supported, -ENOTSUP if 
 *         unsupported.
 */
int
scope_json_supported()
{
#ifdef CONFIG_WITH_JANSSON
	return 0;
#else
	return -ENOTSUP;
#endif
}

#ifdef CONFIG_WITH_JANSSON
static int
save_dbl(json_t *obj, const ScopeParams *p, const char *key, const double *off)
{
	unsigned ch;
	json_t  *a = json_array();
	int      st;
	if ( ! a ) {
		return -ENOMEM;
	}
	for ( ch = 0; ch < p->numChannels; ++ch ) {
		double d;
		if ( isnan( (d = *(const double*)((uintptr_t)&p->afeParams[ch] + (uintptr_t)off)) ) ) {
			st = -ENOTSUP;
			goto bail;
		}
		if ( json_array_append_new( a, json_real( d ) ) ) {
			st = -ENOMEM;
			goto bail;
		}
	}

	if ( 0 == json_object_set_new( obj, key, a ) ) {
		return 0;
	}
	st = -ENOENT;

bail:
	json_decref( a );
	return st;
}

static int
save_uns(json_t *obj, const ScopeParams *p, const char *key, const int *off)
{
	unsigned ch;
	json_t  *a = json_array();
	int      st;
	if ( ! a ) {
		return -ENOMEM;
	}
	for ( ch = 0; ch < p->numChannels; ++ch ) {
		int i;
		if ( (i = *(const int*)((uintptr_t)&p->afeParams[ch] + (uintptr_t)off)) < 0 ) {
			st = -ENOTSUP;
			goto bail;
		}
		if ( json_array_append_new( a, json_integer( i ) ) ) {
			st = -ENOMEM;
			goto bail;
		}
	}

	if ( 0 == json_object_set_new( obj, key, a ) ) {
		return 0;
	}
	st = -ENOENT;

bail:
	json_decref( a );
	return st;
}

#define SAVE_PERCH_DBL(o,p,k,f) save_dbl((o), (p), (k), &((AFEParams*)0)->f)
#define SAVE_PERCH_UNS(o,p,k,f) save_uns((o), (p), (k), &((AFEParams*)0)->f)

#endif

/* Save current settings to JSON file.
 * RETURN 0 on success, negative error status on error.
 */
int
scope_json_save(ScopePvt *scp, const char * filename, const ScopeParams *settings)
{
#ifdef CONFIG_WITH_JANSSON
	json_t       *top         = NULL;
	int           st          = -ENOMEM;
	unsigned long ul;
	int           i;
	double        d;

	if ( ! ( top = json_object() ) ) {
		goto bail;
	}

	if ( json_object_set_new( top, SCOPE_KEY_NUM_CHNLS, json_integer( settings->numChannels ) ) ) {
		st = -ENOENT;
		goto bail;
	}

	if ( json_object_set_new( top, SCOPE_KEY_CLOCK_F_HZ, json_real( settings->samplingFreqHz ) ) ) {
		st = -ENOENT;
		goto bail;
	}

	if ( json_object_set_new( top, SCOPE_KEY_TRG_MODE, json_integer( settings->trigMode ) ) ) {
		st = -ENOENT;
		goto bail;
	}

	if ( !! (settings->acqParams.mask & ACQ_PARAM_MSK_SRC ) ) {
		if ( json_object_set_new( top, SCOPE_KEY_TRG_SRC   , json_integer( settings->acqParams.src ) ) ) {
			st = -ENOENT;
			goto bail;
		}
	}

	if ( !! (settings->acqParams.mask & ACQ_PARAM_MSK_TGO ) ) {
		if ( json_object_set_new( top, SCOPE_KEY_TRG_OUT_EN, json_integer( settings->acqParams.trigOutEn ) ) ) {
			st = -ENOENT;
			goto bail;
		}
	}

	if ( !! (settings->acqParams.mask & ACQ_PARAM_MSK_EDG ) ) {
		i = settings->acqParams.rising ? 1 : -1;
		if ( json_object_set_new( top, SCOPE_KEY_TRG_EDGE, json_integer( i ) ) ) {
			st = -ENOENT;
			goto bail;
		}
	}

	if ( !! (settings->acqParams.mask & ACQ_PARAM_MSK_LVL ) ) {
		d = acq_level_to_percent( settings->acqParams.level );
		if ( json_object_set_new( top, SCOPE_KEY_TRG_L_PERC, json_real( d ) ) ) {
			st = -ENOENT;
			goto bail;
		}

		d = acq_level_to_percent( settings->acqParams.hysteresis );
		if ( json_object_set_new( top, SCOPE_KEY_TRG_H_PERC, json_real( d ) ) ) {
			st = -ENOENT;
			goto bail;
		}
	}

	if ( !! (settings->acqParams.mask & ACQ_PARAM_MSK_NPT ) ) {
		if ( json_object_set_new( top, SCOPE_KEY_NPTS      , json_integer( settings->acqParams.npts ) ) ) {
			st = -ENOENT;
			goto bail;
		}
	}

	if ( !! (settings->acqParams.mask & ACQ_PARAM_MSK_NSM ) ) {
		if ( json_object_set_new( top, SCOPE_KEY_NSAMPLES  , json_integer( settings->acqParams.nsamples ) ) ) {
			st = -ENOENT;
			goto bail;
		}
	}

	if ( !! (settings->acqParams.mask & ACQ_PARAM_MSK_AUT ) ) {
		if ( json_object_set_new( top, SCOPE_KEY_AUTOTRG_MS, json_integer( settings->acqParams.autoTimeoutMS ) ) ) {
			st = -ENOENT;
			goto bail;
		}
	}

	if ( !! (settings->acqParams.mask & ACQ_PARAM_MSK_DCM ) ) {
		ul = settings->acqParams.cic0Decimation * settings->acqParams.cic1Decimation;
		if ( json_object_set_new( top, SCOPE_KEY_DECIMATION, json_integer( ul ) ) ) {
			st = -ENOENT;
			goto bail;
		}
	}

	SAVE_PERCH_DBL( top, settings, SCOPE_KEY_FULSCL_VLT, fullScaleVolt  );

	SAVE_PERCH_DBL( top, settings, SCOPE_KEY_CURSCL_VLT, currentScaleVolt  );
	SAVE_PERCH_DBL( top, settings, SCOPE_KEY_FEC_ATT_DB, fecAttDb          );
	SAVE_PERCH_UNS( top, settings, SCOPE_KEY_FEC_CPLING, fecCouplingAC     );
	SAVE_PERCH_DBL( top, settings, SCOPE_KEY_PGA_ATT_DB, pgaAttDb          );
	SAVE_PERCH_UNS( top, settings, SCOPE_KEY_DAC_RNG_HI, dacRangeHi        );
	SAVE_PERCH_DBL( top, settings, SCOPE_KEY_DAC_VOLT  , dacVolt           );
	SAVE_PERCH_DBL( top, settings, SCOPE_KEY_FEC_TERM  , fecTerminationOhm );

	if ( json_dump_file( top, filename, JSON_INDENT(2) ) ) {
		st = -EIO;
	} else {
		st = 0;
	}

bail:

	json_decref( top );

	return st;
#else
	return -ENOTSUP;
#endif
}

#ifdef CONFIG_WITH_JANSSON

#define QUIET 1
#define ALRT  0

#define SCLR  0

static
json_t *jget(json_t *dict, const char *key,  int quiet)
{
	json_t *val;
	if ( ! (val = json_object_get( dict, key )) && ! quiet ) {
		fprintf(stderr, "scope_json_load: key %s not found\n", key);
	}
	return val;
}

static int
check_num(json_t *j, double *d, int *i, unsigned ch, const char *key)
{
	if ( ! j || ! (d ?  json_is_real( j ) : json_is_integer( j )) ) {
		fprintf(stderr, "scope_json_load: key %s: %s value expected\n", key, d ? "real" : "integer" );
		return -EINVAL;
	}
	if ( d ) {
		d[ch] = json_real_value( j );
	} else {
		i[ch] = json_integer_value( j );
	}
	return 0;
}

static int
jget_num(json_t *dict, const char *key, double *d, int *i, unsigned nelms, int quiet)
{
	json_t  *val;
	unsigned ch;
	int      st;
	if ( ! (val = jget( dict, key, quiet )) ) {
		return -ENOENT;
	}
	if ( nelms > 0 ) {
		if ( ! json_is_array( val ) ) {
			fprintf(stderr, "scope_json_load: key %s -- array expected.\n", key);
			return -EINVAL;
		}
		if ( json_array_size( val ) != nelms ) {
			fprintf(stderr, "scope_json_load: key %s -- array of size %u expected.\n", key, nelms);
		}
		for ( ch = 0; ch < nelms; ++ch ) {
			if ( ( st = check_num( json_array_get( val, ch ), d, i, ch, key ) ) ) {
				return st;
			}
		}
		return 0;
	} else {
		return check_num( val, d, i, 0, key );
	}
}

static int
jget_real(json_t *dict, const char *key, double *d, unsigned nelms, int quiet)
{
	return jget_num( dict, key, d, NULL, nelms, quiet );
}

static int
jget_int(json_t *dict, const char *key, int *i, unsigned nelms, int quiet)
{
	return jget_num( dict, key, NULL, i, nelms, quiet );
}

static int
jget_real_or_nan(json_t *dict, const char *key, double *d, unsigned nelms)
{
	int      st = jget_real( dict, key, d, nelms, QUIET );
	unsigned ch;
	if ( -ENOENT == st ) {
		for ( ch = 0; ch < (SCLR != nelms ? nelms : 1); ++ch ) {
			d[ch] = 0.0/0.0;
		}
		st = 0;	
	}
	return st;
}

static int
jget_int_or_neg(json_t *dict, const char *key, int *i, unsigned nelms)
{
	int      st = jget_int( dict, key, i, nelms, QUIET );
	unsigned ch;
	if ( -ENOENT == st ) {
		for ( ch = 0; ch < (SCLR != nelms ? nelms : 1); ++ch ) {
			i[ch] = -1;
		}
		st = 0;	
	}
	return st;
}

static int
cpy_afe_dbl(json_t *dict, const char *key, ScopeParams *p, double *off)
{
	int      st;
	double   d[p->numChannels];
	unsigned ch;
	if ( (st = jget_real_or_nan( dict, key, d, p->numChannels )) ) {
		return st;
	}
	for ( ch = 0; ch < p->numChannels; ++ch ) {
		*(double*)((uintptr_t)&p->afeParams[ch] + (uintptr_t)off) = d[ch];
	}
	return 0;
}

static int
cpy_afe_int(json_t *dict, const char *key, ScopeParams *p, int *off)
{
	int      st;
	int      i[p->numChannels];
	unsigned ch;
	if ( (st = jget_int_or_neg( dict, key, i, p->numChannels )) ) {
		return st;
	}
	for ( ch = 0; ch < p->numChannels; ++ch ) {
		*(int*)((uintptr_t)&p->afeParams[ch] + (uintptr_t)off) = i[ch];
	}
	return 0;
}


#define CPY_AFE_DBL(d, k, p, fld) cpy_afe_dbl( (d), (k), (p), &((AFEParams*)0)->fld )
#define CPY_AFE_INT(d, k, p, fld) cpy_afe_int( (d), (k), (p), &((AFEParams*)0)->fld )

#endif

/* Load current settings from JSON file.
 * RETURN 0 on success, negative error status on error.
 */
int
scope_json_load(ScopePvt *scp, const char * filename, ScopeParams *settings)
{
#ifdef CONFIG_WITH_JANSSON
	json_t      *top;
	json_error_t jerr;
	int          st   = -ENOENT;
	int          ival;
	double       dval;

	settings->acqParams.mask = 0;

	top = json_load_file( filename, 0, &jerr );
	if ( ! top ) {
		fprintf(stderr, "%s: loading file failed (%s), line %d, col %d, pos %d\n", __func__, jerr.text, jerr.line, jerr.column, jerr.position);
		goto bail;
	}
	if ( ! json_is_object( top ) ) {
		fprintf(stderr, "%s: top-level JSON not an object.\n", __func__);
		st = -EINVAL;
		goto bail;
	}

	if ( (st = jget_int( top, SCOPE_KEY_NUM_CHNLS, (int*)&settings->numChannels, SCLR, ALRT )) ) {
		goto bail;
	}

	if ( settings->numChannels != scope_get_num_channels( scp ) ) {
		fprintf(stderr, "%s: unexpected number of channels.\n", __func__);
		st = -EINVAL;
		goto bail;
	}

	if ( (st = jget_real( top, SCOPE_KEY_CLOCK_F_HZ, &settings->samplingFreqHz, SCLR, ALRT )) ) {
		goto bail;
	}

	if ( (st = jget_int_or_neg( top, SCOPE_KEY_TRG_MODE, &settings->trigMode, SCLR ) ) ) {
		goto bail;
	}

	if ( settings->samplingFreqHz != buf_get_sampling_freq( scp ) ) {
		fprintf(stderr, "%s: WARNING -- sampling frequency mismatch; resetting to freq supported by device!\n", __func__);
		settings->samplingFreqHz = buf_get_sampling_freq( scp );
	}

	st = CPY_AFE_DBL( top, SCOPE_KEY_FULSCL_VLT, settings, fullScaleVolt );
	if ( st ) {
		goto bail;
	}

	st = CPY_AFE_DBL( top, SCOPE_KEY_CURSCL_VLT, settings, currentScaleVolt );
	if ( st ) {
		goto bail;
	}

	st = CPY_AFE_DBL( top, SCOPE_KEY_PGA_ATT_DB, settings, pgaAttDb );
	if ( st ) {
		goto bail;
	}

	st = CPY_AFE_DBL( top, SCOPE_KEY_FEC_ATT_DB, settings, fecAttDb );
	if ( st ) {
		goto bail;
	}

	st = CPY_AFE_DBL( top, SCOPE_KEY_FEC_TERM  , settings, fecTerminationOhm );
	if ( st ) {
		goto bail;
	}

	st = CPY_AFE_INT( top, SCOPE_KEY_FEC_CPLING, settings, fecCouplingAC );
	if ( st ) {
		goto bail;
	}

	st = CPY_AFE_DBL( top, SCOPE_KEY_DAC_VOLT  , settings, dacVolt );
	if ( st ) {
		goto bail;
	}


	st = CPY_AFE_INT( top, SCOPE_KEY_DAC_RNG_HI, settings, dacRangeHi );
	if ( st ) {
		goto bail;
	}

	st = jget_int( top, SCOPE_KEY_TRG_SRC, &ival, SCLR, QUIET );
	if ( -ENOENT != st ) {
		if ( st ) {
			goto bail;
		}
		switch ( (TriggerSource)ival ) {
			case CHA:
			case CHB:
			case EXT:
				settings->acqParams.src = (TriggerSource)ival;
				break;
			default:
				st = -EINVAL;
				goto bail;
		}
		settings->acqParams.mask |= ACQ_PARAM_MSK_SRC;
	}

	st = jget_int( top, SCOPE_KEY_TRG_OUT_EN, &ival, SCLR, QUIET );
	if ( -ENOENT != st ) {
		if ( st ) {
			goto bail;
		}
		if ( ival >= 0 ) {
			if ( EXT == settings->acqParams.src && ival ) {
				fprintf(stderr, "%s: WARNING; cannot enable trigger out in external-source mode\n", __func__);
				ival = 0;
			}
			settings->acqParams.trigOutEn = ival;
			settings->acqParams.mask |= ACQ_PARAM_MSK_TGO;
		}
	}

	st = jget_int( top, SCOPE_KEY_TRG_EDGE, &ival, SCLR, QUIET );
	if ( -ENOENT != st ) {
		if ( st ) {
			goto bail;
		}
		settings->acqParams.rising = (ival > 0);
		settings->acqParams.mask |= ACQ_PARAM_MSK_EDG;
	}

	st = jget_real( top, SCOPE_KEY_TRG_L_PERC, &dval, SCLR, QUIET );
	if ( -ENOENT != st ) {
		if ( st ) {
			goto bail;
		}
		/* match default hysteresis set by firmware */
		settings->acqParams.hysteresis = acq_percent_to_level( 3.125 );
		settings->acqParams.level      = acq_percent_to_level( dval );
		settings->acqParams.mask      |= ACQ_PARAM_MSK_LVL;
	}

	st = jget_real( top, SCOPE_KEY_TRG_H_PERC, &dval, SCLR, QUIET );
	if ( -ENOENT != st ) {
		if ( st ) {
			goto bail;
		}
		if ( ! (settings->acqParams.mask & ACQ_PARAM_MSK_LVL) ) {
			/* set default level; nothing from json */
			settings->acqParams.level = acq_percent_to_level( 0.0 );
		}
		settings->acqParams.hysteresis = acq_percent_to_level( dval );
		settings->acqParams.mask      |= ACQ_PARAM_MSK_LVL;
	}

	st = jget_int( top, SCOPE_KEY_NSAMPLES  , &ival, SCLR, QUIET );
	if ( -ENOENT != st ) {
		if ( st ) {
			goto bail;
		}
		if ( buf_get_size( scp ) < ival ) {
			ival = buf_get_size( scp );
			fprintf(stderr, "%s: WARNING - reducing number of samples to what the device supports (%d).\n", __func__, ival);
		}
		if ( ival < 1 ) {
			fprintf(stderr, "%s: WARNING - increasing number of samples to 1.\n", __func__);
			ival = 1;
		}
		settings->acqParams.nsamples   = ival;
		settings->acqParams.mask      |= ACQ_PARAM_MSK_NSM;
	}

	st = jget_int( top, SCOPE_KEY_NPTS      , &ival, SCLR, QUIET );
	if ( -ENOENT != st ) {
		if ( st ) {
			goto bail;
		}
		settings->acqParams.nsamples   = ival;
		if ( !! ( settings->acqParams.mask & ACQ_PARAM_MSK_NSM ) ) {
			if ( ival >= settings->acqParams.nsamples ) {
				fprintf(stderr, "%s: WARNING - reducing npts to number of samples.\n", __func__);
				ival = settings->acqParams.nsamples - 1;
			}
		} else {
			/* don't bother (current setting not readily available)
			 * it will be checked when they try to write to FW.
			 */
		}
		settings->acqParams.npts       = ival;
		settings->acqParams.mask      |= ACQ_PARAM_MSK_NPT;
	}

	st = jget_int( top, SCOPE_KEY_AUTOTRG_MS, &ival, SCLR, QUIET );
	if ( -ENOENT != st ) {
		if ( st ) {
			goto bail;
		}
		settings->acqParams.autoTimeoutMS = ival;
		settings->acqParams.mask         |= ACQ_PARAM_MSK_AUT;
	}

	st = jget_int( top, SCOPE_KEY_DECIMATION, &ival, SCLR, QUIET );
	if ( -ENOENT != st ) {
		if ( st ) {
			goto bail;
		}
		if ( ival < 0 ) {
			st = -EINVAL;
			goto bail;
		}
		if ( ( st = acq_auto_decimation( scp, (unsigned)ival, &settings->acqParams.cic0Decimation, &settings->acqParams.cic1Decimation ) ) ) {
			goto bail;
		}
		settings->acqParams.mask |= ACQ_PARAM_MSK_DCM;
	}

	st = 0;
bail:
	json_decref( top );
	return st;
#else
	return -ENOTSUP;
#endif
}
