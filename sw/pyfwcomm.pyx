#cython: embedsignature=True, language_level=3, c_string_type=str, c_string_encoding=ascii
from pyfwcomm cimport *

from cpython.exc cimport *
from cpython     cimport *

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
  cdef Dac47CX     _dac
  cdef Max195xxADC _adc

  def __cinit__( self, str name, speed = 115200, *args, **kwargs ):
    self._fw = fw_open(name, speed)
    self._nm = name
    if self._fw is NULL:
      PyErr_SetFromErrnoWithFilenameObject( OSError, name )

  def __init__( self, str name, speed = 115200, *args, **kwargs ):
    self._dac = Dac47CX( self )
    self._adc = Max195xxADC( self )

  def init( self ):
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
