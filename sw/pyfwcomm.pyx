#cython: embedsignature=True, language_level=3, c_string_type=str, c_string_encoding=ascii
from pyfwcomm cimport *

from cpython.exc cimport *
from cpython     cimport *

import numpy

cdef class VersaClk:
  cdef FWInfo     *_fw

  def __cinit__(self, FwComm fw):
    self._fw = fw._fw

  def setFBDiv(self, float div):
    if ( versaClkSetFBDivFlt( self._fw, div ) < 0 ):
      raise IOError("VersaClk.setFBDiv()")

  def setOutDiv(self, int out, float div):
    cdef int rv
    rv = versaClkSetOutDivFlt( self._fw, out, div )
    if ( rv < 0 ):
      if ( -3 == rv ):
        raise ValueError("VersaClk.setOutDiv() -- invalid output")
      else:
        raise IOError("VersaClk.setOutDiv()")

  def setOutEna(self, int out, bool en):
    cdef int rv
    rv = versaClkSetOutEna( self._fw, out, en )
    if ( rv < 0 ):
      if ( -3 == rv ):
        raise ValueError("VersaClk.setOutEna() -- invalid output")
      else:
        raise IOError("VersaClk.setOutEna()")

  def setOutCfg(self, int out, VersaClkOutMode mode, VersaClkOutSlew slew, VersaClkOutLevel level):
    cdef int rv
    rv = versaClkSetOutCfg( self._fw, out, mode, slew, level )
    if ( rv < 0 ):
      if ( -3 == rv ):
        raise ValueError("VersaClk.setOutCfg() -- invalid output")
      else:
        raise IOError("VersaClk.setOutCfg()")

cdef class Max195xxADC:
  cdef FWInfo     *_fw

  def __cinit__(self, FwComm fw):
    self._fw = fw._fw

  def reset(self):
    if ( max195xxReset( self._fw ) < 0 ):
      raise IOError("Max195xxADC.reset()")

  def init(self):
    cdef int rv
    rv = max195xxInit( self._fw )
    if ( rv < 0 ):
      if ( -3 == rv ):
        raise RuntimeError("Max195ccADC.init(): ADC may have no clock")
      else:
        raise IOError("Max195xxADC.init()")

  def setTestMode(self, Max195xxTestMode m):
    if ( max195xxSetTestMode( self._fw, m ) < 0 ):
      raise IOError("Max195xxADC.setTestMode()")

  def setCMVolt(self, Max195xxCMVolt cmA, Max195xxCMVolt cmB):
    if ( max195xxSetCMVolt( self._fw, cmA, cmB ) < 0 ):
      raise IOError("Max195xxADC.setCMVolt()")


cdef class DAC47CX:
  cdef FWInfo     *_fw

  def __cinit__(self, FwComm fw):
    self._fw = fw._fw

  def reset(self):
    if ( dac47cxReset( self._fw ) < 0 ):
      raise IOError("DAC47CX.reset()")

  def init(self):
    if ( dac47cxInit( self._fw ) < 0 ):
      raise IOError("DAC47CX.init()")
 
  def getRange(self):
    cdef float vmin, vmax
    dac47cxGetRange( NULL, NULL, &vmin, &vmax )
    return (vmin, vmax)

  def setTicks(self, int channel, int ticks):
    cdef int rv
    rv = dac47cxSet( self._fw, channel, ticks )
    if ( rv < 0 ):
      if ( -2 == rv ):
        raise ValueError("DAC47CX.setTicks(): Invalid Channel")
      else:
        raise IOError("DAC47CX.setTicks()")

  def getTicks(self, int channel):
    cdef int      rv
    cdef uint16_t ticks
    rv = dac47cxGet( self._fw, channel, &ticks )
    if ( rv < 0 ):
      if ( -1 > rv ):
        raise ValueError("DAC47CX.getTicks(): Invalid Channel")
      else:
        raise IOError("DAC47CX.getTicks()")
    return ticks


  def setVolt(self, int channel, float volt):
    cdef int rv
    rv = dac47cxSetVolt( self._fw, channel, volt )
    if ( rv < 0 ):
      if ( -2 == rv ):
        raise ValueError("DAC47CX.setVolt(): Invalid Channel")
      else:
        raise IOError("DAC47CX.setVolt()")

  def getVolt(self, int channel):
    cdef int    rv
    cdef float  volt
    rv = dac47cxGetVolt( self._fw, channel, &volt )
    if ( rv < 0 ):
      if ( -1 > rv ):
        raise ValueError("DAC47CX.getVolt(): Invalid Channel")
      else:
        raise IOError("DAC47CX.getVolt()")
    return volt

cdef class FwComm:
  cdef FWInfo     *_fw
  cdef const char *_nm
  cdef VersaClk    _clk
  cdef DAC47CX     _dac
  cdef Max195xxADC _adc
  cdef int         _bufsz

  def __cinit__( self, str name, speed = 115200, *args, **kwargs ):
    self._fw = fw_open(name, speed)
    self._nm = name
    if self._fw is NULL:
      PyErr_SetFromErrnoWithFilenameObject( OSError, name )

  def __init__( self, str name, speed = 115200, *args, **kwargs ):
    self._clk = VersaClk( self )
    self._dac = DAC47CX( self )
    self._adc = Max195xxADC( self )

  def init( self ):
    self._bufsz = self.getBufSize()
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
    rv = lmh6882GetAtt( self._fw, channel )
    if ( rv < 0 ):
      if ( rv < -1.0 ):
        raise ValueError("getS2Att(): invalid channel")
      else:
        raise IOError("getS2Att()")
    return rv

  def setS2Att(self, int channel, float att):
    cdef float rv
    rv = lmh6882SetAtt( self._fw, channel, att )
    if ( rv < 0 ):
      if ( rv < -1 ):
        raise ValueError("setS2Att(): invalid channel or attenuation")
      else:
        raise IOError("setS2Att()")

  def version(self):
    return fw_get_version( self._fw )

  def getBufSize(self):
    return buf_get_size( self._fw )

  def flush(self):
      return buf_flush( self._fw )

  def read(self, pyb):
    cdef Py_buffer b
    cdef int       rv
    if ( not PyObject_CheckBuffer( pyb ) or 0 != PyObject_GetBuffer( pyb, &b, PyBUF_C_CONTIGUOUS | PyBUF_WRITEABLE ) ):
      raise ValueError("FwComm.read arg must support buffer protocol")
    if ( b.itemsize != 1 ):
      PyBuffer_Release( &b )
      raise ValueError("FwComm.read arg buffer itemsize must be 1")
    rv = buf_read( self._fw, NULL, <uint8_t*>b.buf, b.len )
    PyBuffer_Release( &b )
    if ( rv < 0 ):
      PyErr_SetFromErrnoWithFilenameObject(OSError, self._nm)
    return rv

  def setAcqTriggerLevelPercent(self, float lvl):
    cdef int16_t l
    if ( lvl > 100.0 or lvl < -100.0 ):
      raise ValueError("setAcqTriggerLevelPercent(): trigger (percentage) level must be -100 <= level <= +100")
    l = round( 32767.0 * lvl/100.0 )
    if ( acq_set_level( self._fw, l ) < 0 ):
      raise IOError("setAcqTriggerLevelPercent()")

  def setAcqNPreTriggerSamples(self, int n):
    if ( n < 0 or n > self._bufsz - 1 ):
      raise ValueError("setAcqNPreTriggerSamples(): # pre-trigger samples out of range")
    if ( acq_set_npts( self._fw, n ) < 0 ):
      raise IOError("setAcqNPreTriggerSamples()")

  def setAcqDecimation(self, n0, n1 = None):
    cdef uint8_t  cic0Dec
    cdef uint32_t cic1Dec
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
    if ( acq_set_decimation( self._fw, cic0Dec, cic1Dec ) < 0 ):
      raise IOError("setAcqDecimation()")

  def setAcqTriggerSource(self, TriggerSource src, bool rising = True):
    if ( acq_set_source( self._fw, src, rising ) < 0 ):
      raise IOError("setAcqTriggerSource()")

  def setAcqAutoTimeoutMs(self, int timeout):
    if ( timeout < 0 ):
      timeout = 0
    if ( acq_set_autoTimeoutMs( self._fw, timeout ) < 0 ):
      raise IOError("setAcqAutoTimeoutMs()")

  def setAcqScale(self, float scale):
    cdef int32_t iscale
    iscale = round( scale * 2.0**30 )
    if ( acq_set_scale( self._fw, 0, 0, iscale ) < 0 ):
      raise IOError("setAcqScale()")

  def mkBuf(self):
    return numpy.zeros( (self._bufsz, 2), dtype = "int8" )

  def __dealloc__(self):
    fw_close( self._fw )
