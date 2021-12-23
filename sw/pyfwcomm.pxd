#cython: embedsignature=True, language_level=3, c_string_type=str, c_string_encoding=ascii

from libc.stdint cimport *

cdef extern from "fwComm.h":
  ctypedef struct FWInfo:
    pass
  
  FWInfo        *fw_open(const char *devn, unsigned speed)
  void           fw_close(FWInfo *)
  int64_t        fw_get_version(FWInfo *)
  unsigned long  buf_get_size(FWInfo *)
  int            buf_flush(FWInfo *)
  int            buf_read(FWInfo *, uint16_t *hdr, uint8_t *buf, size_t len)

cdef extern from "lmh6882Sup.h":
  float          lmh6882GetAtt(FWInfo *fw, unsigned channel)
  int            lmh6882SetAtt(FWInfo *fw, unsigned channel, float att)

cdef extern from "dac47cxSup.h":
  int            dac47cxReset(FWInfo *)
  int            dac47cxInit(FWInfo *)
  void           dac47cxGetRange(int *tickMin, int *tickMax, float *voltMin, float *voltMax)
  int            dac47cxSetVolt(FWInfo *fw, unsigned channel, float val)

cdef extern from "max195xxSup.h":
  int            max195xxReset( FWInfo *fw );
  int            max195xxInit( FWInfo *fw );
  int            max195xxSetTestMode(FWInfo *fw, Max195xxTestMode m);
  int            max195xxSetCMVolt( FWInfo *fw, Max195xxCMVolt cmA, Max195xxCMVolt cmB );
  
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
  int            versaClkSetFBDiv(FWInfo *fw, unsigned idiv, unsigned fdiv)
  int            versaClkSetFBDivFlt(FWInfo *fw, double div)
  int            versaClkSetOutDiv(FWInfo *fw, unsigned outp, unsigned idiv, unsigned long fdiv)
  int            versaClkSetOutDivFlt(FWInfo *fw, unsigned outp, double div)
  int            versaClkSetOutEna(FWInfo *fw, unsigned outp, int ena)
  int            versaClkSetOutCfg(FWInfo *fw, unsigned outp, VersaClkOutMode mode, VersaClkOutSlew slew, VersaClkOutLevel level)

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
