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


cdef extern from "dac47cxSup.h":
  int            dac47cxReset(FWInfo *)
  int            dac47cxInit(FWInfo *)
  void           dac47cxGetRange(int *tickMin, int *tickMax, float *voltMin, float *voltMax)
  int            dac47cxSetVolt(FWInfo *fw, unsigned channel, float val)
  
cpdef enum Max195xxCMVolt:
    CM_0900mV=0
    CM_1050mV=1
    CM_1200mV=2
    CM_1350mV=3
    CM_0750mV=5
    CM_0600mV=6
    CM_0450mV=7

cpdef enum Max195xxTestMode:
    NO_TEST
    RAMP_TEST
    AA55_TEST

cdef extern from "max195xxSup.h":
  int            max195xxReset( FWInfo *fw );
  int            max195xxInit( FWInfo *fw );
  int            max195xxSetTestMode(FWInfo *fw, Max195xxTestMode m);
  int            max195xxSetCMVolt( FWInfo *fw, Max195xxCMVolt cmA, Max195xxCMVolt cmB );
