#cython: embedsignature=True, language_level=3, c_string_type=str, c_string_encoding=ascii

from libc.stdint cimport *
from libc.math   cimport isnan

cdef extern from "fwComm.h":
  ctypedef struct FWInfo:
    pass

  uint8_t        FW_BUF_FLG_16B
  uint8_t        I2C_READ
  
  FWInfo        *fw_open(const char *devn, unsigned speed) nogil
  int            fw_xfer(FWInfo *, uint8_t cmd, const uint8_t *tbuf, uint8_t *rbuf, size_t len) nogil
  void           fw_close(FWInfo *) nogil
  void           fw_set_debug(FWInfo *fw, int level) nogil
  uint32_t       fw_get_version(FWInfo *) nogil
  uint64_t       fw_get_features(FWInfo *) nogil
  uint8_t        fw_get_api_version(FWInfo *) nogil
  uint8_t        fw_get_board_version(FWInfo *) nogil
  int            fw_reg_read(FWInfo *, uint32_t, uint8_t *, size_t, unsigned) nogil
  int            fw_reg_write(FWInfo *, uint32_t, uint8_t *, size_t, unsigned) nogil


  struct AcqParams:
    unsigned      mask
    TriggerSource src
    int           rising
    int16_t       level
    uint32_t      npts
    uint32_t      nsamples
    uint32_t      autoTimeoutMS
    uint8_t       cic0Decimation
    uint32_t      cic1Decimation
    uint8_t       cic0Shift
    uint8_t       cic1Shift
    int32_t       scale
  int            bb_spi_raw(FWInfo *, SPIDev, int clk, int mosi, int cs, int hiz) nogil
  int            bb_i2c_read_reg(FWInfo *, uint8_t sla, uint8_t reg) nogil
  int            bb_i2c_write_reg(FWInfo *, uint8_t sla, uint8_t reg, uint8_t val) nogil
  int            bb_i2c_rw_a8(FWInfo *fw, uint8_t sla, uint8_t addr, uint8_t *data, size_t len) nogil

  int            eepromGetSize(FWInfo *) nogil
  int            eepromRead(FWInfo *, unsigned off, uint8_t *buf, size_t len) nogil
  int            eepromWrite(FWInfo *, unsigned off, uint8_t *buf, size_t len) nogil


cpdef enum SPIDev:
  NONE
  FLASH
  ADC
  PGA
  FEG
  VGA
  VGB


cpdef enum TriggerSource:
  CHA
  CHB
  EXT

cdef extern from "lmh6882Sup.h":
  float          lmh6882GetAtt(FWInfo *fw, unsigned channel) nogil
  int            lmh6882SetAtt(FWInfo *fw, unsigned channel, float att) nogil

cdef extern from "ad8370Sup.h":
  int            ad8370Write(FWInfo *fw, unsigned channel, uint8_t val) nogil
  int            ad8370Read(FWInfo *fw, unsigned channel) nogil
  int            ad8370SetAtt(FWInfo *fw, unsigned channel, float att) nogil
  float          ad8370GetAtt(FWInfo *fw, unsigned channel) nogil

cpdef enum DAC47CXRefSelection:
  DAC47XX_VREF_INTERNAL_X1

cdef extern from "dac47cxSup.h":
  int            dac47cxReset(FWInfo *) nogil
  void           dac47cxGetRange(FWInfo *, int *tickMin, int *tickMax, float *voltMin, float *voltMax) nogil
  int            dac47cxSetVolt(FWInfo *fw, unsigned channel, float val) nogil
  int            dac47cxGetVolt(FWInfo *fw, unsigned channel, float *val) nogil
  int            dac47cxSet(FWInfo *fw, unsigned channel, int val) nogil
  int            dac47cxGet(FWInfo *fw, unsigned channel, uint16_t *val) nogil
  int            dac47cxSetRefSelection(FWInfo *fw, DAC47CXRefSelection sel) nogil

cdef extern from "max195xxSup.h":
  int            max195xxReadReg( FWInfo *fw, unsigned reg, uint8_t *val ) nogil
  int            max195xxWriteReg( FWInfo *fw, unsigned reg, uint8_t val ) nogil
  int            max195xxReset( FWInfo *fw ) nogil
  int            max195xxInit( FWInfo *fw ) nogil
  int            max195xxDLLLocked( FWInfo *fw ) nogil
  int            max195xxSetTestMode(FWInfo *fw, Max195xxTestMode m) nogil
  int            max195xxSetCMVolt( FWInfo *fw, Max195xxCMVolt cmA, Max195xxCMVolt cmB ) nogil
  int            max195xxSetTiming( FWInfo *fw, int dclkDelay, int dataDelay ) nogil
  int            max195xxGetClkTermination( FWInfo *fw ) nogil

  int            max195xxEnableClkTermination( FWInfo *fw, int on ) nogil

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

cpdef enum VersaClkFODRoute:
  NORMAL   = 0
  CASC_FOD = 1
  CASC_OUT = 2
  OFF      = 3

cdef extern from "versaClkSup.h":
  int            versaClkSetFBDiv(FWInfo *fw, unsigned idiv, unsigned fdiv) nogil
  int            versaClkSetFBDivFlt(FWInfo *fw, double  div) nogil
  int            versaClkGetFBDivFlt(FWInfo *fw, double  *div) nogil
  int            versaClkSetOutDiv(FWInfo *fw, unsigned outp, unsigned idiv, unsigned long fdiv) nogil
  int            versaClkSetOutDivFlt(FWInfo *fw, unsigned outp, double div) nogil
  int            versaClkGetOutDivFlt(FWInfo *fw, unsigned outp, double *div) nogil
  int            versaClkSetOutCfg(FWInfo *fw, unsigned outp, VersaClkOutMode mode, VersaClkOutSlew slew, VersaClkOutLevel level) nogil
  int            versaClkSetFODRoute(FWInfo *fw, unsigned outp, VersaClkFODRoute rte) nogil
  int            versaClkReadReg(FWInfo *fw, unsigned reg) nogil
  int            versaClkWriteReg(FWInfo *fw, unsigned reg, uint8_t val) nogil

cpdef enum VersaClkOutMode:
  OUT_CMOS = 1
  OUT_LVDS = 3

cpdef enum VersaClkOutSlew:
  SLEW_080 = 0
  SLEW_085 = 1
  SLEW_090 = 2
  SLEW_100 = 3

cpdef enum VersaClkOutLevel:
  LEVEL_18 = 0
  LEVEL_25 = 2
  LEVEL_33 = 3

cdef extern from "scopeSup.h":
  ctypedef struct ScopePvt:
    pass
  ScopePvt      *scope_open(FWInfo *fw) nogil
  void           scope_close(ScopePvt *) nogil

  int            scope_init(ScopePvt *, int force) nogil
  unsigned long  buf_get_size(ScopePvt *) nogil
  double         buf_get_sampling_freq(ScopePvt *) nogil
  uint8_t        buf_get_flags(ScopePvt *) nogil
  int            buf_flush(ScopePvt *) nogil
  int            buf_read(ScopePvt *, uint16_t *hdr, uint8_t *buf, size_t len) nogil
  int            buf_read_flt(ScopePvt *, uint16_t *hdr, float *buf, size_t len) nogil
  int            ACQ_PARAM_TIMEOUT_INF
  int            acq_set_params(ScopePvt *, AcqParams *set, AcqParams *get) nogil
  int            acq_set_level(ScopePvt *, int16_t level, uint16_t hysteresis) nogil
  int            acq_set_npts(ScopePvt *, int32_t npts) nogil
  int            acq_set_nsamples(ScopePvt *, int32_t nsamples) nogil
  int            acq_set_decimation(ScopePvt *, uint8_t cic0Decimation, uint32_t cic1Decimation) nogil
  int            acq_set_source(ScopePvt *, TriggerSource src, int rising) nogil
  int            acq_set_autoTimeoutMs(ScopePvt *, uint32_t timeout) nogil
  int            acq_set_scale(ScopePvt *, uint8_t cic0RShift, uint8_t cic1RShift, int32_t scale) nogil
  int            pgaReadReg(ScopePvt *, unsigned ch, unsigned reg) nogil
  int            pgaWriteReg(ScopePvt *, unsigned ch, unsigned reg, unsigned val) nogil
  int            pgaGetAttRange(ScopePvt*, double *min, double *max) nogil
  int            pgaGetAtt(ScopePvt *, unsigned channel, double *att) nogil
  int            pgaSetAtt(ScopePvt *, unsigned channel, double att) nogil
  int            fecGetACMode(ScopePvt *, unsigned channel) nogil
  int            fecSetACMode(ScopePvt *, unsigned channel, unsigned on) nogil
  int            fecGetTermination(ScopePvt *, unsigned channel) nogil
  int            fecSetTermination(ScopePvt *, unsigned channel, unsigned on) nogil
  int            fecGetDACRangeHi(ScopePvt *, unsigned channel) nogil
  int            fecSetDACRangeHi(ScopePvt *, unsigned channel, unsigned on) nogil
  int            fecGetAttRange(ScopePvt*, double *min, double *max) nogil
  int            fecGetAtt(ScopePvt *, unsigned channel, double *att) nogil
  int            fecSetAtt(ScopePvt *, unsigned channel, double att) nogil
  void           fecClose(ScopePvt *) nogil
