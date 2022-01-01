#cython: embedsignature=True, language_level=3, c_string_type=str, c_string_encoding=ascii
from pyfwcomm cimport *

from cpython.exc cimport *
from cpython     cimport *

import numpy

cdef class FwMgr:
  cdef FWInfo     *_fw
  cdef const char *_nm
  

  def __cinit__( self, str name, speed = 115200, *args, **kwargs ):
    self._fw = fw_open(name, speed)
    self._nm = name
    if self._fw is NULL:
      PyErr_SetFromErrnoWithFilenameObject( OSError, name )

  cdef FWInfo * __enter__(self):
    print("ENTER")
    return self._fw

  def __exit__(self, exc_typ, exc_val, trc):
    print("EXIT")
    return False # re-raise exception

  def __dealloc__(self):
    fw_close( self._fw )

cdef class VersaClk:
  cdef FwMgr _mgr

  def __cinit__(self, FwMgr mgr):
    self._mgr = mgr

  def setFBDiv(self, float div):
    cdef int st
    with self._mgr as fw, nogil:
      st = versaClkSetFBDivFlt( fw, div )
    if ( st < 0 ):
      raise IOError("VersaClk.setFBDiv()")

  def setOutDiv(self, int out, float div):
    cdef int rv
    with self._mgr as fw, nogil:
      rv = versaClkSetOutDivFlt( fw, out, div )
    if ( rv < 0 ):
      if ( -3 == rv ):
        raise ValueError("VersaClk.setOutDiv() -- invalid output")
      else:
        raise IOError("VersaClk.setOutDiv()")

  def setOutEna(self, int out, bool en):
    cdef int rv, ien
    ien = en
    with self._mgr as fw, nogil:
      rv = versaClkSetOutEna( fw, out, ien )
    if ( rv < 0 ):
      if ( -3 == rv ):
        raise ValueError("VersaClk.setOutEna() -- invalid output")
      else:
        raise IOError("VersaClk.setOutEna()")

  def setOutCfg(self, int out, VersaClkOutMode mode, VersaClkOutSlew slew, VersaClkOutLevel level):
    cdef int rv
    cdef VersaClkOutMode cmode
    cmode = mode
    with self._mgr as fw, nogil:
      rv = versaClkSetOutCfg( fw, out, cmode, slew, level )
    if ( rv < 0 ):
      if ( -3 == rv ):
        raise ValueError("VersaClk.setOutCfg() -- invalid output")
      else:
        raise IOError("VersaClk.setOutCfg()")

cdef class Max195xxADC:
  cdef FwMgr _mgr

  def __cinit__(self, FwMgr mgr):
    self._mgr = mgr

  def reset(self):
    cdef int st
    with self._mgr as fw, nogil:
      st = max195xxReset( fw )
    if ( st < 0 ):
      raise IOError("Max195xxADC.reset()")

  def init(self):
    cdef int rv
    with self._mgr as fw, nogil:
      rv = max195xxInit( fw )
    if ( rv < 0 ):
      if ( -3 == rv ):
        raise RuntimeError("Max195ccADC.init(): ADC may have no clock")
      else:
        raise IOError("Max195xxADC.init()")

  def setTestMode(self, Max195xxTestMode m):
    cdef int st
    with self._mgr as fw, nogil:
      st = max195xxSetTestMode( fw, m )
    if ( st < 0 ):
      raise IOError("Max195xxADC.setTestMode()")

  def setCMVolt(self, Max195xxCMVolt cmA, Max195xxCMVolt cmB):
    cdef int st
    with self._mgr as fw, nogil:
      st = max195xxSetCMVolt( fw, cmA, cmB )
    if ( st < 0 ):
      raise IOError("Max195xxADC.setCMVolt()")

  def dllLocked(self):
    cdef int st
    with self._mgr as fw, nogil:
      st = max195xxDLLLocked( fw )
    return st == 0

cdef class DAC47CX:
  cdef FwMgr _mgr

  def __cinit__(self, FwMgr mgr):
    self._mgr = mgr

  def reset(self):
    cdef int st
    with self._mgr as fw, nogil:
      st = dac47cxReset( fw )
    if ( st < 0 ):
      raise IOError("DAC47CX.reset()")

  def init(self):
    cdef int st
    with self._mgr as fw, nogil:
      st = dac47cxInit( fw )
    if ( st < 0 ):
      raise IOError("DAC47CX.init()")
 
  def getRange(self):
    cdef float vmin, vmax
    with self._mgr as fw, nogil:
      dac47cxGetRange( NULL, NULL, &vmin, &vmax )
    return (vmin, vmax)

  def setTicks(self, int channel, int ticks):
    cdef int rv
    with self._mgr as fw, nogil:
      rv = dac47cxSet( fw, channel, ticks )
    if ( rv < 0 ):
      if ( -2 == rv ):
        raise ValueError("DAC47CX.setTicks(): Invalid Channel")
      else:
        raise IOError("DAC47CX.setTicks()")

  def getTicks(self, int channel):
    cdef int      rv
    cdef uint16_t ticks
    with self._mgr as fw, nogil:
      rv = dac47cxGet( fw, channel, &ticks )
    if ( rv < 0 ):
      if ( -1 > rv ):
        raise ValueError("DAC47CX.getTicks(): Invalid Channel")
      else:
        raise IOError("DAC47CX.getTicks()")
    return ticks


  def setVolt(self, int channel, float volt):
    cdef int rv
    with self._mgr as fw, nogil:
      rv = dac47cxSetVolt( fw, channel, volt )
    if ( rv < 0 ):
      if ( -2 == rv ):
        raise ValueError("DAC47CX.setVolt(): Invalid Channel")
      else:
        raise IOError("DAC47CX.setVolt()")

  def getVolt(self, int channel):
    cdef int    rv
    cdef float  volt
    with self._mgr as fw, nogil:
      rv = dac47cxGetVolt( fw, channel, &volt )
    if ( rv < 0 ):
      if ( -1 > rv ):
        raise ValueError("DAC47CX.getVolt(): Invalid Channel")
      else:
        raise IOError("DAC47CX.getVolt()")
    return volt

cdef class FwComm:
  cdef FwMgr       _mgr
  cdef const char *_nm
  cdef VersaClk    _clk
  cdef DAC47CX     _dac
  cdef Max195xxADC _adc
  cdef int         _bufsz
  cdef AcqParams   _parmCache

  def __cinit__( self, str name, speed = 115200, *args, **kwargs ):
    pass

  def __init__( self, str name, speed = 115200, *args, **kwargs ):
    cdef int st
    self._mgr   = FwMgr( name, speed, args, kwargs )
    with self._mgr as fw, nogil:
      st = acq_set_params( fw, NULL, &self._parmCache )
    if ( st < 0 ):
      raise IOError("FwComm.__init__(): acq_set_params failed")
    self._clk   = VersaClk( self._mgr )
    self._dac   = DAC47CX( self._mgr )
    self._adc   = Max195xxADC( self._mgr )
    self._bufsz = self.getBufSize()

  def init( self ):
    for o in [SEL_EXT, SEL_ADC, SEL_FPGA]:
      self._clk.setOutCfg( o, OUT_CMOS, SLEW_100, LEVEL_18 )
    if False:
      # 130MHz clock
      self.setClkFBDiv( 104.0 )
      self.setClkOutDiv( SEL_ADC, 10.0 )
    else:
      # 100MHz clock
      self.setClkOutDiv( SEL_ADC, 14.0 )
    self.setClkOutEna( SEL_ADC, True )
    self._dac.init()
    self._adc.init()

  def setDACVolt( self, ch, v):
    self._dac.setVolt( ch, v )

  def getDACVolt( self, ch ):
    return self._dac.getVolt( ch )

  def setDACTicks( self, ch, v):
    self._dac.setTicks( ch, v )

  def getDACTicks( self, ch ):
    return self._dac.getTicks( ch )

  def getDACRange( self ):
    return self._dac.getRange()

  def setTestMode(self, Max195xxTestMode m):
    self._adc.setTestMode( m )

  def setCMVolt(self, Max195xxCMVolt cmA, Max195xxCMVolt cmB):
    self._adc.setCMVolt( cmA, cmB )

  def setClkFBDiv(self, float div):
    self._clk.setFBDiv( div )

  def setClkOutDiv(self, int out, float div):
    self._clk.setOutDiv( out, div )

  def setClkOutEna(self, int out, bool en):
    self._clk.setOutEna( out, en )

  def getS2Att(self, int channel):
    cdef float rv
    with self._mgr as fw, nogil:
      rv = lmh6882GetAtt( fw, channel )
    if ( rv < 0 ):
      if ( rv < -1.0 ):
        raise ValueError("getS2Att(): invalid channel")
      else:
        raise IOError("getS2Att()")
    return rv

  def setS2Att(self, int channel, float att):
    cdef float rv
    with self._mgr as fw, nogil:
      rv = lmh6882SetAtt( fw, channel, att )
    if ( rv < 0 ):
      if ( rv < -1 ):
        raise ValueError("setS2Att(): invalid channel or attenuation")
      else:
        raise IOError("setS2Att()")

  def version(self):
    cdef int64_t ver
    with self._mgr as fw, nogil:
      ver = fw_get_version( fw )
    return ver

  def getBufSize(self):
    cdef unsigned long sz
    with self._mgr as fw, nogil:
      sz = buf_get_size( fw )
    return sz

  def flush(self):
    cdef int st
    with self._mgr as fw, nogil:
      st = buf_flush( fw )
    return st

  def read(self, pyb):
    cdef Py_buffer b
    cdef int       rv
    cdef uint16_t  hdr
    if ( not PyObject_CheckBuffer( pyb ) or 0 != PyObject_GetBuffer( pyb, &b, PyBUF_C_CONTIGUOUS | PyBUF_WRITEABLE ) ):
      raise ValueError("FwComm.read arg must support buffer protocol")
    if ( b.itemsize != 1 ):
      PyBuffer_Release( &b )
      raise ValueError("FwComm.read arg buffer itemsize must be 1")
    with self._mgr as fw, nogil:
      rv = buf_read( fw, &hdr, <uint8_t*>b.buf, b.len )
    PyBuffer_Release( &b )
    if ( rv < 0 ):
      PyErr_SetFromErrnoWithFilenameObject(OSError, self._nm)
    return rv, hdr

  def isADCDLLLocked(self):
    return self._adc.dllLocked()

  def getAcqTriggerLevelPercent(self):
    return 100.0 * float(self._parmCache.level) / 32767.0

  def setAcqTriggerLevelPercent(self, float lvl):
    cdef int16_t l
    cdef int     st
    if ( lvl > 100.0 or lvl < -100.0 ):
      raise ValueError("setAcqTriggerLevelPercent(): trigger (percentage) level must be -100 <= level <= +100")
    l = round( 32767.0 * lvl/100.0 )
    if ( self._parmCache.level == l ):
      return
    with self._mgr as fw, nogil:
      st = acq_set_level( fw, l )
    if ( st < 0 ):
      raise IOError("setAcqTriggerLevelPercent()")
    self._parmCache.level = l

  def getAcqNPreTriggerSamples(self):
    return self._parmCache.npts

  def setAcqNPreTriggerSamples(self, int n):
    cdef int st
    if ( n < 0 or n > self._bufsz - 1 ):
      raise ValueError("setAcqNPreTriggerSamples(): # pre-trigger samples out of range")
    if ( self._parmCache.npts == n ):
      return
    with self._mgr as fw, nogil:
      st = acq_set_npts( fw, n )
    if ( st < 0 ):
      raise IOError("setAcqNPreTriggerSamples()")
    self._parmCache.npts = n

  def getAcqDecimation(self):
    return self._parmCache.cic0Decimation, self._parmCache.cic1Decimation

  def setAcqDecimation(self, n0, n1 = None):
    cdef uint8_t  cic0Dec
    cdef uint32_t cic1Dec
    cdef int      st
    if ( not n1 is None ):
      if ( n0 < 1 or n0 > 16 or  n1 < 1 or n1 > 2**12 ):
        raise ValueError("setAcqDecimation(): decimation out of range")
      if ( n1 != 1 and 1 == n0 ):
        raise ValueError("setAcqDecimation(): if CIC1 decimation > 1 then CIC0 must be > 1, too")
      cic0Dec = n0
      cic1Dec = n1
    else:
      if ( n0 < 1 or n0 > 16 * 2**12):
        raise ValueError("setAcqDecimation(): decimation out of range")
      if ( 1 == n0 ):
        cic0Dec = 1
        cic1Dec = 1
      else:
        for cic0Dec in range(16,0,-1):
          if ( n0 % cic0Dec == 0 ):
            cic1Dec = int(n0 / cic0Dec)
            break
        if 1 == cic0Dec:
          raise ValueError("setAcqDecimation(): decimation must have a factor in 2..16")
    if ( self._parmCache.cic0Decimation == cic0Dec and self._parmCache.cic1Decimation == cic1Dec ):
      return
    with self._mgr as fw, nogil:
      st = acq_set_decimation( fw, cic0Dec, cic1Dec )
    if ( st < 0 ):
      raise IOError("setAcqDecimation()")
    self._parmCache.cic0Decimation = cic0Dec
    self._parmCache.cic1Decimation = cic1Dec

  def getAcqTriggerSource(self):
    return self._parmCache.src, bool(self._parmCache.rising)

  def setAcqTriggerSource(self, TriggerSource src, bool rising = True):
    cdef int st
    cdef int irising
    if ( TriggerSource(self._parmCache.src) == src and bool(self._parmCache.rising) == rising ):
      return
    irising = rising
    with self._mgr as fw, nogil:
      st = acq_set_source( fw, src, irising )
    if ( st < 0 ):
      raise IOError("setAcqTriggerSource()")
    self._parmCache.src    = src
    self._parmCache.rising = rising

  def getAcqAutoTimeoutMs(self):
    timeout = self._parmCache.autoTimeoutMS
    if ( timeout == ACQ_PARAM_TIMEOUT_INF ):
      timeout = -1
    return timeout

  def setAcqAutoTimeoutMs(self, int timeout):
    cdef int st
    if ( timeout < 0 ):
      timeout = ACQ_PARAM_TIMEOUT_INF
    if ( self._parmCache.autoTimeoutMS == timeout ):
      return
    with self._mgr as fw, nogil:
      st = acq_set_autoTimeoutMs( fw, timeout )
    if ( st < 0 ):
      raise IOError("setAcqAutoTimeoutMs()")
    self._parmCache.autoTimeoutMS = timeout

  def getAcqScale(self):
    return float(self._parmCache.scale) / 2.0**30

  def setAcqScale(self, float scale):
    cdef int     st
    cdef int32_t iscale
    iscale = round( scale * 2.0**30 )
    if ( self._parmCache.scale == iscale ):
      return
    with self._mgr as fw, nogil:
      st = acq_set_scale( fw, 0, 0, iscale )
    if ( st < 0 ):
      raise IOError("setAcqScale()")
    self._parmCache.scale = iscale

  def mkBuf(self):
    return numpy.zeros( (self._bufsz, 2), dtype = "int8" )


