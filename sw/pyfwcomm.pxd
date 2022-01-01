#cython: embedsignature=True, language_level=3, c_string_type=str, c_string_encoding=ascii

from libc.stdint cimport *

cdef extern from "fwComm.h":
  ctypedef struct FWInfo:
    pass
  
  FWInfo        *fw_open(const char *devn, unsigned speed) nogil
  void           fw_close(FWInfo *) nogil
  int64_t        fw_get_version(FWInfo *) nogil
  unsigned long  buf_get_size(FWInfo *) nogil
  int            buf_flush(FWInfo *) nogil
  int            buf_read(FWInfo *, uint16_t *hdr, uint8_t *buf, size_t len) nogil
  int            acq_set_level(FWInfo *, int16_t level) nogil
  int            acq_set_npts(FWInfo *, int32_t npts) nogil
  int            acq_set_decimation(FWInfo *, uint8_t cic0Decimation, uint32_t cic1Decimation) nogil
  int            acq_set_source(FWInfo *, TriggerSource src, int rising) nogil
  int            acq_set_autoTimeoutMs(FWInfo *, uint32_t timeout) nogil
  int            acq_set_scale(FWInfo *, uint8_t cic0RShift, uint8_t cic1RShift, int32_t scale) nogil
  struct AcqParams:
    unsigned      mask
    TriggerSource src
    int           rising
    int16_t       level
    uint32_t      npts
    uint32_t      autoTimeoutMS
    uint8_t       cic0Decimation
    uint32_t      cic1Decimation
    uint8_t       cic0Shift
    uint8_t       cic1Shift
    int32_t       scale
  int            ACQ_PARAM_TIMEOUT_INF
  int            acq_set_params(FWInfo *, AcqParams *set, AcqParams *get) nogil


cpdef enum TriggerSource:
  CHA
  CHB
  EXT

cdef extern from "lmh6882Sup.h":
  float          lmh6882GetAtt(FWInfo *fw, unsigned channel) nogil
  int            lmh6882SetAtt(FWInfo *fw, unsigned channel, float att) nogil

cdef extern from "dac47cxSup.h":
  int            dac47cxReset(FWInfo *) nogil
  int            dac47cxInit(FWInfo *) nogil
  void           dac47cxGetRange(int *tickMin, int *tickMax, float *voltMin, float *voltMax) nogil
  int            dac47cxSetVolt(FWInfo *fw, unsigned channel, float val) nogil
  int            dac47cxGetVolt(FWInfo *fw, unsigned channel, float *val) nogil
  int            dac47cxSet(FWInfo *fw, unsigned channel, int val) nogil
  int            dac47cxGet(FWInfo *fw, unsigned channel, uint16_t *val) nogil

cdef extern from "max195xxSup.h":
  int            max195xxReset( FWInfo *fw ) nogil
  int            max195xxInit( FWInfo *fw ) nogil
  int            max195xxDLLLocked( FWInfo *fw ) nogil
  int            max195xxSetTestMode(FWInfo *fw, Max195xxTestMode m) nogil
  int            max195xxSetCMVolt( FWInfo *fw, Max195xxCMVolt cmA, Max195xxCMVolt cmB ) nogil
  
cpdef enum Max195xxCMVolt:
  CM_0900mV = 0
  CM_1050mV = 1
  CM_1200mV = 2
  CM_1350mV = 3
  CM_0750mV = 5
  CM_0600mV = 6
  CM_0450mV = 7

cpdef enum Max195xxTestMode:
  NO_TEST
  RAMP_TEST
  AA55_TEST

cdef extern from "versaClkSup.h":
  int            versaClkSetFBDiv(FWInfo *fw, unsigned idiv, unsigned fdiv) nogil
  int            versaClkSetFBDivFlt(FWInfo *fw, double div) nogil
  int            versaClkSetOutDiv(FWInfo *fw, unsigned outp, unsigned idiv, unsigned long fdiv) nogil
  int            versaClkSetOutDivFlt(FWInfo *fw, unsigned outp, double div) nogil
  int            versaClkSetOutEna(FWInfo *fw, unsigned outp, int ena) nogil
  int            versaClkSetOutCfg(FWInfo *fw, unsigned outp, VersaClkOutMode mode, VersaClkOutSlew slew, VersaClkOutLevel level) nogil

cpdef enum CLOCK_OUT:
  SEL_EXT  = 1
  SEL_ADC  = 2
  SEL_FPGA = 4

cpdef enum VersaClkOutMode:
  OUT_CMOS = 1

cpdef enum VersaClkOutSlew:
  SLEW_080 = 0
  SLEW_085 = 1
  SLEW_090 = 2
  SLEW_100 = 3

cpdef enum VersaClkOutLevel:
  LEVEL_18 = 0
  LEVEL_25 = 2
  LEVEL_33 = 3
