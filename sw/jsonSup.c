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
scope_json_supported(ScopePvt *scp)
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
	unsigned      ch;
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

	if ( json_object_set_new( top, SCOPE_KEY_TRG_SRC   , json_integer( settings->acqParams.src ) ) ) {
		st = -ENOENT;
		goto bail;
	}

	if ( json_object_set_new( top, SCOPE_KEY_TRG_OUT_EN, json_integer( settings->acqParams.trigOutEn ) ) ) {
		st = -ENOENT;
		goto bail;
	}

	i = settings->acqParams.rising ? 1 : -1;
	if ( json_object_set_new( top, SCOPE_KEY_TRG_EDGE, json_integer( i ) ) ) {
		st = -ENOENT;
		goto bail;
	}

	d = scope_trig_level_volt( settings );
	if ( json_object_set_new( top, SCOPE_KEY_TRG_L_VOLT, json_real( d ) ) ) {
		st = -ENOENT;
		goto bail;
	}

	d = scope_trig_hysteresis_volt( settings );
	if ( json_object_set_new( top, SCOPE_KEY_TRG_H_VOLT, json_real( d ) ) ) {
		st = -ENOENT;
		goto bail;
	}

	if ( json_object_set_new( top, SCOPE_KEY_NPTS      , json_integer( settings->acqParams.npts ) ) ) {
		st = -ENOENT;
		goto bail;
	}

	if ( json_object_set_new( top, SCOPE_KEY_NSAMPLES  , json_integer( settings->acqParams.nsamples ) ) ) {
		st = -ENOENT;
		goto bail;
	}

	if ( json_object_set_new( top, SCOPE_KEY_AUTOTRG_MS, json_integer( settings->acqParams.autoTimeoutMS ) ) ) {
		st = -ENOENT;
		goto bail;
	}


	ul = settings->acqParams.cic0Decimation * settings->acqParams.cic1Decimation;
	if ( json_object_set_new( top, SCOPE_KEY_DECIMATION, json_integer( ul ) ) ) {
		st = -ENOENT;
		goto bail;
	}

	SAVE_PERCH_DBL( top, settings, SCOPE_KEY_FULSCL_VLT, fullScaleVolt  );

	SAVE_PERCH_DBL( top, settings, SCOPE_KEY_CURSCL_VLT, currentScaleVolt  );
	SAVE_PERCH_DBL( top, settings, SCOPE_KEY_FEC_ATT_DB, fecAttDb          );
	SAVE_PERCH_UNS( top, settings, SCOPE_KEY_FEC_CPLING, fecCouplingAC     );
	SAVE_PERCH_DBL( top, settings, SCOPE_KEY_PGA_ATT_DB, pgaAttDb          );
	SAVE_PERCH_UNS( top, settings, SCOPE_KEY_FEC_DAC_HI, dacRangeHi        );
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

/* Load current settings from JSON file.
 * RETURN 0 on success, negative error status on error.
 */
int
scope_json_load(ScopePvt *pvt, const char * filename, ScopeParams *settings)
{
#ifdef CONFIG_WITH_JANSSON
	return 0;
#else
	return -ENOTSUP;
#endif
}
