#include <fwComm.h>
#include <scopeSup.h>
#include <jsonSup.h>
#include <getopt.h>
#include <stdio.h>
#include <math.h>

#define CHECK(p,p1,parm)  (((p)->parm == (p1)->parm) ? 0 : (fprintf(stderr, "%s mismatch\n", #parm), 1) )

#define CHECKD(p,p1,parm) ((isnan((p)->parm) && isnan((p1)->parm)) ? 0 : (CHECK((p),(p1),parm)))
int
main(int argc, char **argv)
{
const char  *dev = NULL;
ScopePvt    *scp = NULL;
FWInfo      *fw  = NULL;
int          rv  = 1;
ScopeParams *p   = NULL;
ScopeParams *p1  = NULL;
int          opt;
unsigned     ch;
int          fail;

	while ( (opt = getopt(argc, argv, "d:")) > 0 ) {
		switch ( opt ) {
			case 'd': dev = optarg; break;
			default:
				  fprintf(stderr, "Invalid option -%c\n", opt);
				  return 1;
		}
	}
	if ( ! dev ) {
		fprintf(stderr, "Missing scope device name (use -d)\n");
		return 1;
	}

	if ( ! (fw = fw_open( dev, 115200 )) ) {
		fprintf(stderr, "fw_open failed\n");
		goto bail;
	}

	if ( ! (scp = scope_open( fw )) ) {
		fprintf(stderr, "scope_open failed\n");
		goto bail;
	}

	if ( ! (p = scope_alloc_params( scp )) ) {
		fprintf(stderr, "scope_alloc_params failed\n");
		goto bail;
	}
	if ( ! (p1 = scope_alloc_params( scp )) ) {
		fprintf(stderr, "scope_alloc_params failed\n");
		goto bail;
	}


	p->trigMode = 3;
	p->acqParams.src = CHB;
	p->acqParams.trigOutEn = 0;
	p->acqParams.rising = 0;
	p->acqParams.level = 1234;
	p->acqParams.hysteresis = 4321;
	p->acqParams.npts = 8765;
	p->acqParams.nsamples = 10000;
	p->acqParams.autoTimeoutMS = 430;
	if ( acq_auto_decimation( scp, 130, &p->acqParams.cic0Decimation, &p->acqParams.cic1Decimation ) ) {
		fprintf(stderr, "acq_auto_decimation failed\n");
		goto bail;
	}
	p->acqParams.mask = 
		ACQ_PARAM_MSK_SRC |
		ACQ_PARAM_MSK_TGO |
		ACQ_PARAM_MSK_EDG |
		ACQ_PARAM_MSK_LVL |
		ACQ_PARAM_MSK_NPT |
		ACQ_PARAM_MSK_NSM |
		ACQ_PARAM_MSK_AUT |
		ACQ_PARAM_MSK_DCM;

	for ( ch = 0; ch < p->numChannels; ++ch ) {
		AFEParams *ap = &p->afeParams[ch];
		ap->fullScaleVolt      = 1.0 + ch*10.0;
		ap->currentScaleVolt   = 2.0 + ch*10.0;
		ap->pgaAttDb           = 3.0 + ch*10.0;
		ap->fecAttDb           = 4.0 + ch*10.0;
		ap->fecTerminationOhm  = 0.0/0.0;
		ap->dacVolt            = 5.0 + ch*10.0;
		ap->fecCouplingAC      = !! ch;
		ap->dacRangeHi         = !! (1 - ch);
	}

	if ( scope_json_save( scp, "tst.json", p ) ) {
		fprintf(stderr, "scope_json_save failed\n");
		goto bail;
	}

	if ( scope_json_load( scp, "tst.json", p1 ) ) {
		fprintf(stderr, "scope_json_load failed\n");
		goto bail;
	}

	fail = 0;
	fail += CHECK(p, p1, numChannels);
	fail += CHECK(p, p1, samplingFreqHz);
	fail += CHECK(p, p1, trigMode);
	fail += CHECK(p, p1, acqParams.mask);
	fail += CHECK(p, p1, acqParams.src);
	fail += CHECK(p, p1, acqParams.trigOutEn);
	fail += CHECK(p, p1, acqParams.rising);
	fail += CHECK(p, p1, acqParams.level);
	fail += CHECK(p, p1, acqParams.hysteresis);
	fail += CHECK(p, p1, acqParams.npts);
	fail += CHECK(p, p1, acqParams.nsamples);
	fail += CHECK(p, p1, acqParams.autoTimeoutMS);
	fail += CHECK(p, p1, acqParams.cic0Decimation);
	fail += CHECK(p, p1, acqParams.cic1Decimation);

	for ( ch = 0; ch < p->numChannels; ++ch ) {
		fail += CHECKD(p, p1, afeParams[ch].fullScaleVolt);
		fail += CHECKD(p, p1, afeParams[ch].currentScaleVolt);
		fail += CHECKD(p, p1, afeParams[ch].pgaAttDb);
		fail += CHECKD(p, p1, afeParams[ch].fecAttDb);
		fail += CHECKD(p, p1, afeParams[ch].fecTerminationOhm);
		fail += CHECKD(p, p1, afeParams[ch].dacVolt);
		fail += CHECK(p, p1, afeParams[ch].fecCouplingAC);
		fail += CHECK(p, p1, afeParams[ch].dacRangeHi);
	}

	if ( fail ) {
		fprintf(stderr, "Test FAILED: (%d mismatches)\n", fail);
		goto bail;
	}
	rv = 0;
bail:
	if ( p ) {
		scope_free_params( p );
	}
	if ( p1 ) {
		scope_free_params( p1 );
	}
	if ( scp ) {
		scope_close( scp );
	}
	if ( fw ) {
		fw_close( fw );
	}
	return rv;
}

