#cython: embedsignature=True, language_level=3, c_string_type=str, c_string_encoding=ascii
from pyfwcomm cimport *

from cpython.exc cimport *
from cpython     cimport *

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


cdef class Dac47CX:
  cdef FWInfo     *_fw

  def __cinit__(self, FwComm fw):
    self._fw = fw._fw

  def reset(self):
    if ( dac47cxReset( self._fw ) < 0 ):
      raise IOError("Dac47CX.reset()")

  def init(self):
    if ( dac47cxInit( self._fw ) < 0 ):
      raise IOError("Dac47CX.init()")
 
  def getRange(self):
    cdef float vmin, vmax
    dac47cxGetRange( NULL, NULL, &vmin, &vmax )
    return (vmin, vmax)

  def set(self, int channel, float volt):
    cdef int rv
    rv = dac47cxSetVolt( self._fw, channel, volt )
    if ( rv < 0 ):
      if ( -2 == rv ):
        raise ValueError("Dac47CX.set(): Invalid Channel")
      else:
        raise IOError("Dac47CX.set()")

cdef class FwComm:
  cdef FWInfo     *_fw
  cdef const char *_nm
  cdef VersaClk    _clk
  cdef Dac47CX     _dac
  cdef Max195xxADC _adc

  def __cinit__( self, str name, speed = 115200, *args, **kwargs ):
    self._fw = fw_open(name, speed)
    self._nm = name
    if self._fw is NULL:
      PyErr_SetFromErrnoWithFilenameObject( OSError, name )

  def __init__( self, str name, speed = 115200, *args, **kwargs ):
    self._clk = VersaClk( self )
    self._dac = Dac47CX( self )
    self._adc = Max195xxADC( self )

  def init( self ):
    for o in [SEL_EXT, SEL_ADC, SEL_FPGA]:
      self._clk.setOutCfg( o, OUT_CMOS, SLEW_100, LEVEL_18 )
    # 130MHz clock
    self.setClkFBDiv( 104.0 )
    self.setClkOutDiv( SEL_ADC, 10.0 )
    self.setClkOutEna( SEL_ADC, True )
    self._dac.init()
    self._adc.init()

  def setDAC( self, ch, v):
    self._dac.set( ch, v )

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

  def __dealloc__(self):
    fw_close( self._fw )
