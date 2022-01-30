#cython: embedsignature=True, language_level=3, c_string_type=str, c_string_encoding=ascii
from pyfwcomm cimport *

from cpython.exc cimport *
from cpython     cimport *

cdef extern from "pthread.h":
  ctypedef struct pthread_mutexattr_t
  ctypedef struct pthread_mutex_t:
    pass
  int pthread_mutex_init(pthread_mutex_t *, pthread_mutexattr_t *)
  int pthread_mutex_destroy(pthread_mutex_t *)
  int pthread_mutex_lock(pthread_mutex_t *) nogil
  int pthread_mutex_unlock(pthread_mutex_t *) nogil

  ctypedef struct pthread_attr_t
  ctypedef struct pthread_t:
    pass
  int pthread_create(pthread_t*, const pthread_attr_t *, void *(*)(void *), void *)

  ctypedef struct pthread_condattr_t
  ctypedef struct pthread_cond_t:
    pass
  int pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *)
  int pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *) nogil
  int pthread_cond_signal(pthread_cond_t *) nogil
  int pthread_cond_destroy(pthread_cond_t *)

import numpy

cdef class Mtx:
  cdef pthread_mutex_t _mtx

  def __cinit__( self, *args, **kwargs ):
    st = pthread_mutex_init( &self._mtx, NULL )
    if ( st != 0 ):
      raise OSError("pthread_mutex_init failed with status {:d}".format(st))

  cdef __enter__(self):
    cdef int st
    with nogil:
      st = pthread_mutex_lock( &self._mtx )
    if ( st != 0 ):
      raise OSError("pthread_mutex_lock failed with status {:d}".format(st))

  def __exit__(self, exc_typ, exc_val, trc):
    cdef int st
    st = pthread_mutex_unlock( &self._mtx )
    if ( st != 0 ):
      raise OSError("pthread_mutex_unlock failed with status {:d}".format(st))
    return False # re-raise exception

  def __dealloc__(self):
    pthread_mutex_destroy( &self._mtx )

  cdef pthread_mutex_t *m( self ):
    return &self._mtx

cdef class Cond:
  cdef Mtx              _mtx
  cdef pthread_cond_t   _cnd

  def __cinit__( self, Mtx mtx, *args, **kwargs ):
    self._mtx = mtx
    st = pthread_cond_init( &self._cnd, NULL )
    if ( st != 0 ):
      raise OSError("pthread_cond_init failed with status {:d}".format(st))

  cdef pthread_cond_t *c(self):
    return &self._cnd

  def __enter__(self):
    class LockedCond:
      def __init__(cself):
        cself.c = self
      def wait(cself):
        cdef int st
        cdef pthread_mutex_t *m
        m = self._mtx.m()
        with nogil:
          st = pthread_cond_wait( &self._cnd, m )
        if ( st != 0 ):
          raise OSError("pthread_cond_wait failed with status {:d}".format(st))
      def signal(cself):
        self.signal()
    self._mtx.__enter__()
    return LockedCond()

  def signal(self):
    st = pthread_cond_signal( & self._cnd )
    if ( st != 0 ):
      raise OSError("pthread_cond_wait failed with status {:d}".format(st))

  def __exit__(self, exc_typ, exc_val, trc):
    return self._mtx.__exit__( exc_typ, exc_val, trc )

  def __dealloc__(self):
    pthread_cond_destroy( &self._cnd )


cdef class FwMgr:
  cdef FWInfo     *_fw
  cdef const char *_nm
  cdef Mtx         _mtx

  def __cinit__( self, str name, speed = 115200, *args, **kwargs ):
    cdef int st
    self._mtx = Mtx()
    self._fw  = fw_open(name, speed)
    self._nm  = name
    if self._fw is NULL:
      PyErr_SetFromErrnoWithFilenameObject( OSError, name )

  cdef FWInfo * __enter__(self):
    self._mtx.__enter__()
    return self._fw

  def __exit__(self, exc_typ, exc_val, trc):
    return self._mtx.__exit__(exc_typ, exc_val, trc)

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

  def setFODRoute(self, int out, VersaClkFODRoute rte):
    cdef int rv
    with self._mgr as fw, nogil:
      rv = versaClkSetFODRoute(fw, out, rte)
    if ( rv < 0 ):
      if ( -3 == rv ):
        raise ValueError("VersaClk.setFODRoute() -- invalid output")
      else:
        raise IOError("VersaClk.setFODRoute()")

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

  def readReg(self, int reg):
    cdef int      rv
    with self._mgr as fw, nogil:
      rv = versaClkReadReg(fw, reg)
    return rv

  def writeReg(self, int reg, int val):
    cdef int      rv
    with self._mgr as fw, nogil:
      rv = versaClkWriteReg(fw, reg, val)
    return rv

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

  def readReg(self, unsigned reg):
    cdef uint8_t val
    cdef int     rv
    with self._mgr as fw, nogil:
      rv = max195xxReadReg( fw, reg, &val )
    if ( rv < 0 ):
      raise IOError("Max195xxADC.readReg()")
    return val

  def writeReg(self, unsigned reg, uint8_t val):
    cdef int     rv
    with self._mgr as fw, nogil:
      rv = max195xxWriteReg( fw, reg, val )
    if ( rv < 0 ):
      raise IOError("Max195xxADC.readReg()")

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

  def setTiming(self, int dclkDelay, int dataDelay):
    cdef int st
    with self._mgr as fw, nogil:
      st = max195xxSetTiming( fw, dclkDelay, dataDelay )
    if   ( -2 == st ):
      raise ValueError("Max195xxADC.setTiming(): delay values must be in -3..+3")
    elif ( st < 0 ):
      raise IOError("Max195xxADC.setTiming()")

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
  cdef FwMgr           _mgr
  cdef const char     *_nm
  cdef VersaClk        _clk
  cdef DAC47CX         _dac
  cdef Max195xxADC     _adc
  cdef int             _bufsz
  cdef AcqParams       _parmCache
  cdef Mtx             _syncMtx
  cdef Cond            _asyncEvt
  cdef pthread_t       _reader
  cdef object          _callable
  cdef object          _buf
  cdef float           _timo

  def readAsync(self, pyb, callback, float timeout = -1.0):
    if (not callable( callback ) ):
      raise ValueError( "FwComm.readAsync: callback not callable" )
    with self._asyncEvt as c:
      if ( not self._buf is None ):
        return False
      self._callable = callback
      self._buf      = pyb
      self._timo     = timeout
      c.signal()
      return True

  @staticmethod
  cdef void * threadFunc(void *arg) nogil:
    with gil:
      return (<FwComm>arg).pyThreadFunc()

  cdef void * pyThreadFunc(self):
    while True:
      with self._asyncEvt as c:
        while self._buf is None:
          c.wait()
        callback  = self._callable
        pyb       = self._buf
        self._buf = None
        timeout   = self._timo
      if ( timeout < 0 ):
        numIter = -1
      else:
        numIter = int( timeout / 0.05 )
        if ( 0 == numIter ):
          numIter = 1
      rv = 0
      while ( 0 != numIter and 0 == rv ):
        rv, hdr = self.read( pyb )
        if ( numIter > 0 ):
          numIter -= 1
      callback( rv, hdr, pyb )
    return NULL

  def __cinit__( self, str name, speed = 115200, *args, **kwargs ):
    self._syncMtx   = Mtx()
    self._asyncEvt  = Cond( self._syncMtx )
    self._callable  = None
    self._buf       = None
    self._timo      = -1.0

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
    with self._mgr as fw, nogil:
      self._bufsz = buf_get_size( fw )
    st = pthread_create( &self._reader, NULL, self.threadFunc, <void*>self )
    if ( st != 0 ):
      raise OSError("pthread_create failed with status {:d}".format(st))

  def init( self ):
    for o in [SEL_EXT, SEL_ADC, SEL_FPGA]:
      self._clk.setOutCfg( o, OUT_CMOS, SLEW_100, LEVEL_18 )
    if True:
      # 130MHz clock
      self.clkSetFBDiv( 104.0 )
      self.clkSetOutDiv( SEL_ADC, 10.0 )
    else:
      # 100MHz clock
      self.clkSetOutDiv( SEL_ADC, 14.0 )
    self.clkSetOutDiv( SEL_EXT, 4095.0 )
    self.clkSetFODRoute( SEL_EXT, CASC_FOD )
    self.clkSetFODRoute( SEL_ADC, NORMAL   )
    self._dac.init()
    self._adc.init()

  def dacSetVolt( self, ch, v):
    self._dac.setVolt( ch, v )

  def dacGetVolt( self, ch ):
    return self._dac.getVolt( ch )

  def dacSetTicks( self, ch, v):
    self._dac.setTicks( ch, v )

  def dacGetTicks( self, ch ):
    return self._dac.getTicks( ch )

  def dacGetRange( self ):
    return self._dac.getRange()

  def datSetTestMode(self, Max195xxTestMode m):
    self._adc.setTestMode( m )

  def adcReadReg(self, reg):
    return self._adc.readReg( reg )

  def adcWriteReg(self, reg, val):
    self._adc.writeReg( reg, val )

  def adcSetCMVolt(self, Max195xxCMVolt cmA, Max195xxCMVolt cmB):
    self._adc.setCMVolt( cmA, cmB )

  def adcIsDLLLocked(self):
    return self._adc.dllLocked()

  def adcSetTiming(self, dclkDelay, dataDelay):
    self._adc.setTiming( dclkDelay, dataDelay )

  def clkSetFBDiv(self, float div):
    self._clk.setFBDiv( div )

  def clkSetOutDiv(self, int out, float div):
    self._clk.setOutDiv( out, div )

  def clkSetFODRoute(self, int out, VersaClkFODRoute rte):
    self._clk.setFODRoute( out, rte )

  def clkReadReg(self, int reg):
    return self._clk.readReg(reg)

  def clkWriteReg(self, int reg, int val):
    return self._clk.writeReg(reg, val)

  def ampGetS2Att(self, int channel):
    cdef float rv
    with self._mgr as fw, nogil:
      rv = lmh6882GetAtt( fw, channel )
    if ( rv < 0 ):
      if ( rv < -1.0 ):
        raise ValueError("getS2Att(): invalid channel")
      else:
        raise IOError("getS2Att()")
    return rv

  def ampSetS2Att(self, int channel, float att):
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
    return self._bufsz

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
    if   ( b.itemsize == 1 ):
      with self._mgr as fw, nogil:
        rv = buf_read( fw, &hdr, <uint8_t*>b.buf, b.len )
    elif ( b.itemsize == sizeof(float) ):
      with self._mgr as fw, nogil:
        rv = buf_read_flt( fw, &hdr, <float*>b.buf, b.len )
    else:
      PyBuffer_Release( &b )
      raise ValueError("FwComm.read arg buffer itemsize must be 1 or {:d}".format(sizeof(float)))
    PyBuffer_Release( &b )
    if ( rv < 0 ):
      PyErr_SetFromErrnoWithFilenameObject(OSError, self._nm)
    return rv, hdr

  def acqGetTriggerLevelPercent(self):
    return 100.0 * float(self._parmCache.level) / 32767.0

  def acqSetTriggerLevelPercent(self, float lvl):
    cdef int16_t l
    cdef int     st
    if ( lvl > 100.0 or lvl < -100.0 ):
      raise ValueError("acqSetTriggerLevelPercent(): trigger (percentage) level must be -100 <= level <= +100")
    l = round( 32767.0 * lvl/100.0 )
    if ( self._parmCache.level == l ):
      return
    with self._mgr as fw, nogil:
      st = acq_set_level( fw, l )
    if ( st < 0 ):
      raise IOError("acqSetTriggerLevelPercent()")
    self._parmCache.level = l

  def acqGetNPreTriggerSamples(self):
    return self._parmCache.npts

  def acqSetNPreTriggerSamples(self, int n):
    cdef int st
    if ( n < 0 or n > self._bufsz - 1 ):
      raise ValueError("acqSetNPreTriggerSamples(): # pre-trigger samples out of range")
    if ( self._parmCache.npts == n ):
      return
    with self._mgr as fw, nogil:
      st = acq_set_npts( fw, n )
    if ( st < 0 ):
      raise IOError("acqSetNPreTriggerSamples()")
    self._parmCache.npts = n

  def acqGetDecimation(self):
    return self._parmCache.cic0Decimation, self._parmCache.cic1Decimation

  def acqSetDecimation(self, n0, n1 = None):
    cdef uint8_t  cic0Dec
    cdef uint32_t cic1Dec
    cdef int      st
    if ( not n1 is None ):
      if ( n0 < 1 or n0 > 16 or  n1 < 1 or n1 > 2**12 ):
        raise ValueError("acqSetDecimation(): decimation out of range")
      if ( n1 != 1 and 1 == n0 ):
        raise ValueError("acqSetDecimation(): if CIC1 decimation > 1 then CIC0 must be > 1, too")
      cic0Dec = n0
      cic1Dec = n1
    else:
      if ( n0 < 1 or n0 > 16 * 2**12):
        raise ValueError("acqSetDecimation(): decimation out of range")
      if ( 1 == n0 ):
        cic0Dec = 1
        cic1Dec = 1
      else:
        for cic0Dec in range(16,0,-1):
          if ( n0 % cic0Dec == 0 ):
            cic1Dec = int(n0 / cic0Dec)
            break
        if 1 == cic0Dec:
          raise ValueError("acqSetDecimation(): decimation must have a factor in 2..16")
    if ( self._parmCache.cic0Decimation == cic0Dec and self._parmCache.cic1Decimation == cic1Dec ):
      return
    with self._mgr as fw, nogil:
      st = acq_set_decimation( fw, cic0Dec, cic1Dec )
    if ( st < 0 ):
      raise IOError("acqSetDecimation()")
    self._parmCache.cic0Decimation = cic0Dec
    self._parmCache.cic1Decimation = cic1Dec

  def acqGetTriggerSource(self):
    return self._parmCache.src, bool(self._parmCache.rising)

  def acqSetTriggerSource(self, TriggerSource src, bool rising = True):
    cdef int st
    cdef int irising
    if ( TriggerSource(self._parmCache.src) == src and bool(self._parmCache.rising) == rising ):
      return
    irising = rising
    with self._mgr as fw, nogil:
      st = acq_set_source( fw, src, irising )
    if ( st < 0 ):
      raise IOError("acqSetTriggerSource()")
    self._parmCache.src    = src
    self._parmCache.rising = rising

  def acqGetAutoTimeoutMs(self):
    timeout = self._parmCache.autoTimeoutMS
    if ( timeout == ACQ_PARAM_TIMEOUT_INF ):
      timeout = -1
    return timeout

  def acqSetAutoTimeoutMs(self, int timeout):
    cdef int st
    if ( timeout < 0 ):
      timeout = ACQ_PARAM_TIMEOUT_INF
    if ( self._parmCache.autoTimeoutMS == timeout ):
      return
    with self._mgr as fw, nogil:
      st = acq_set_autoTimeoutMs( fw, timeout )
    if ( st < 0 ):
      raise IOError("acqSetAutoTimeoutMs()")
    self._parmCache.autoTimeoutMS = timeout

  def acqGetScale(self):
    return float(self._parmCache.scale) / 2.0**30

  def acqSetScale(self, float scale):
    cdef int     st
    cdef int32_t iscale
    iscale = round( scale * 2.0**30 )
    if ( self._parmCache.scale == iscale ):
      return
    with self._mgr as fw, nogil:
      st = acq_set_scale( fw, 0, 0, iscale )
    if ( st < 0 ):
      raise IOError("acqSetScale()")
    self._parmCache.scale = iscale

  def mkBuf(self):
    return numpy.zeros( (self._bufsz, 2), dtype = "int8" )

  def mkFltBuf(self):
    return numpy.zeros( (self._bufsz, 2), dtype = "float32" )
