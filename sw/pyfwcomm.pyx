#cython: embedsignature=True, language_level=3, c_string_type=str, c_string_encoding=ascii
from pyfwcomm cimport *

from cpython.exc cimport *
from cpython     cimport *
from time        import  sleep
from libc.string cimport strdup
from libc.stdlib cimport free
from libc.string cimport strerror

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
  cdef char       *_nm
  cdef Mtx         _mtx

  def __cinit__( self, str name, speed = 115200, *args, **kwargs ):
    cdef int st
    self._mtx = Mtx()
    self._fw  = fw_open(name, speed)
    self._nm  = strdup( name )
    if self._fw is NULL:
      PyErr_SetFromErrnoWithFilenameObject( OSError, name )

  cdef FWInfo * __enter__(self):
    self._mtx.__enter__()
    return self._fw

  def __exit__(self, exc_typ, exc_val, trc):
    return self._mtx.__exit__(exc_typ, exc_val, trc)

  def __dealloc__(self):
    free( self._nm )
    fw_close( self._fw )

  def name(self):
    return self._nm

cdef class FwDev:
  cdef FwMgr _mgr

  def __cinit__(self, FwMgr mgr, *args, **kwargs):
    self._mgr = mgr

  # one-time initialization
  # after power-up.
  def init(self):
    pass

  def mgr(self):
    return self._mgr

cdef class LED(FwDev):

  def getVal(self, name):
    return False

  def setVal(self, name, val):
    pass

cdef class LEDv1(LED):

  cdef uint8_t _led[3]

  ledBase = 0

  def __init__(self, *args, **kwargs):
    cdef uint32_t addr
    cdef uint8_t  buf[3]
    super().__init__()
    addr = self.ledBase
    with self._mgr as fw, nogil:
      fw_reg_read( fw, addr, self._led, sizeof(self._led), 0 );

  ledMap = {
    "FrontRight_R":  8,
    "FrontRight_G":  9,
    "FrontRight_B": 10,

    "CHA_R"       :  4,
    "CHA_G"       :  5,
    "CHA_B"       :  6,

    "CHB_R"       :  0,
    "CHB_G"       :  1,
    "CHB_B"       :  2,

    "FrontMid_R"  : 12,
    "FrontMid_G"  : 13,
    "FrontMid_B"  : 14,

    "FrontLeft"   : 15,

    "Trig"        : 16,
    "TermA"       :  6,
    "TermB"       :  2,

    "OVRA"        :  4,
    "OVRB"        :  0
  }

  def getVal(self, name):
    idx = self.ledMap[name]
    adr = int(idx / 8)
    msk = (1<<(idx % 8))
    return (self._led[adr] & msk) != 0

  def setVal(self, name, val):
    cdef uint8_t  buf[1]
    cdef uint32_t adr
    idx = self.ledMap[name]
    adr = idx / 8
    msk = (1<<(idx % 8))
    if val:
      self._led[adr] |=  msk
    else:
      self._led[adr] &= ~msk
    buf[0] = self._led[adr]
    adr   += self.ledBase
    with self._mgr as fw, nogil:
      fw_reg_write( fw, adr, buf, 1, 0 )


cdef class VersaClk(FwDev):

  def setFBDiv(self, float div):
    cdef int  st
    with self._mgr as fw, nogil:
      st = versaClkSetFBDivFlt( fw, div )
    if ( st < 0 ):
      raise IOError("VersaClk.setFBDiv()")

  def getFBDiv(self):
    cdef int st
    cdef double div
    with self._mgr as fw, nogil:
      st = versaClkGetFBDivFlt( fw, &div )
    if ( st < 0 ):
      raise IOError("VersaClk.setFBDiv()")
    return div

  def setOutDiv(self, int out, float div):
    cdef int rv
    with self._mgr as fw, nogil:
      rv = versaClkSetOutDivFlt( fw, out, div )
    if ( rv < 0 ):
      if ( -3 == rv ):
        raise ValueError("VersaClk.setOutDiv() -- invalid output")
      else:
        raise IOError("VersaClk.setOutDiv()")

  def getOutDiv(self, int out):
    cdef int rv
    cdef double div
    with self._mgr as fw, nogil:
      rv = versaClkGetOutDivFlt( fw, out, &div )
    if ( rv < 0 ):
      if ( -3 == rv ):
        raise ValueError("VersaClk.getOutDiv() -- invalid output")
      else:
        raise IOError("VersaClk.getOutDiv()")
    return div

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

cdef class BBRaw(FwDev):
  cdef SPIDev _tgt

  def __cinit__(self, FwMgr mgr, SPIDev tgt, *args, **kwargs):
    self._tgt = tgt

  def __call__(self, int clk, int mosi, int cs=0, int hiz=0):
    cdef int rv
    with self._mgr as fw, nogil:
      rv = bb_spi_raw( fw, self._tgt, clk, mosi, cs, hiz )
    if rv < 0 :
      raise IOError("BBRaw.__call__: bb_spi_raw failed")
    return rv

cdef class Max195xxADC(FwDev):

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

  def setMuxedModeB(self):
    # muxed/DDR mode on port B
    self.writeReg( 1, 0x06 )

  def setMuxedModeA(self):
    # muxed/DDR mode on port B
    self.writeReg( 1, 0x02 )

  def getClkTermination(self):
    cdef int st
    with self._mgr as fw, nogil:
      st = max195xxGetClkTermination( fw )
    if ( st < 0 ):
      raise IOError("Max195xxADC.getClkTermination()")
    return st;

  def enableClkTermination(self, bool on):
    cdef int st
    cdef int val = on
    with self._mgr as fw, nogil:
      st = max195xxEnableClkTermination( fw, val )
    if ( st < 0 ):
      raise IOError("Max195xxADC.getClkTermination()")

cdef class DAC47CX(FwDev):

  def reset(self):
    cdef int st
    with self._mgr as fw, nogil:
      st = dac47cxReset( fw )
    if ( st < 0 ):
      raise IOError("DAC47CX.reset()")

  def getRange(self):
    cdef float vmin, vmax
    with self._mgr as fw, nogil:
      dac47cxGetRange( fw, NULL, NULL, &vmin, &vmax )
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

  def setRefInternalX1(self):
    cdef int rv
    with self._mgr as fw, nogil:
      rv = dac47cxSetRefSelection(fw, DAC47CXRefSelection.DAC47XX_VREF_INTERNAL_X1)
    if ( rv < 0 ):
      raise IOError("DAC47CX.getRefInternalX1(): failed")

cdef class Amp(FwDev):

  def getS2Range(self):
    cdef double attMin, attMax
    with self._mgr as fw, nogil:
      rv = pgaGetAttRange(fw, &attMin, &attMax)
    if ( rv < 0 ):
      raise IOError("Amp.getS2Range(): failed")
    return (attMin,attMax)

  def getS2Att(self, unsigned channel):
    cdef double att
    with self._mgr as fw, nogil:
      rv = pgaGetAtt( fw, channel, &att )
    if ( rv <= -1 ):
      if ( rv < -1 ):
        raise ValueError("Amp.getS2Att(): invalid channel")
      else:
        raise IOError("Amp.getS2Att()")
    return att

  def setS2Att(self, unsigned channel, double att):
    cdef int rv
    with self._mgr as fw, nogil:
      rv = pgaSetAtt( fw, channel, att )
    if ( rv < 0 ):
      if ( rv < -1 ):
        raise ValueError("Amp.setS2Att(): invalid channel or attenuation")
      else:
        raise IOError("Amp.setS2Att()")

  def readReg(self, unsigned channel, unsigned reg):
    cdef int rv
    with self._mgr as fw, nogil:
      rv = pgaReadReg( fw, channel, reg )
    if ( rv < 0 ):
      raise IOError("Amp.readReg failed")
    return rv

  def writeReg(self, unsigned channel, unsigned reg, uint8_t val):
    cdef int rv
    with self._mgr as fw, nogil:
      rv = pgaWriteReg( fw, channel, reg, val )
    if ( rv < 0 ):
      raise IOError("Amp.writeReg failed")

cdef class I2CDev(FwDev):

  def i2cReadReg(self, uint8_t sla, uint8_t off):
    cdef int rv
    sla <<= 1
    with self._mgr as fw, nogil:
      rv = bb_i2c_read_reg(fw, sla, off)
    if ( rv < 0 ):
      raise IOError("i2cReadReg failed")
    return rv

  def i2cWriteReg(self, uint8_t sla, uint8_t off, uint8_t val):
    cdef int rv
    sla <<= 1
    with self._mgr as fw, nogil:
      rv = bb_i2c_write_reg(fw, sla, off, val)
    if ( rv < 0 ):
      raise IOError("i2cReadReg failed")

  cdef i2cRW(self, uint8_t sla, uint8_t off, pyb):
    cdef Py_buffer b
    cdef int       rv
    if ( not PyObject_CheckBuffer( pyb ) or 0 != PyObject_GetBuffer( pyb, &b, PyBUF_C_CONTIGUOUS | PyBUF_WRITEABLE ) ):
      raise ValueError("I2CDev.i2cReadWrite arg must support buffer protocol")
    if   ( b.itemsize == 1 ) :
      with self._mgr as fw, nogil:
        rv = bb_i2c_rw_a8( fw, sla, off, <uint8_t*>b.buf, b.len )
    else:
      PyBuffer_Release( &b )
      raise ValueError("I2CDev.i2cReadWrite arg buffer itemsize must be 1")
    PyBuffer_Release( &b )
    if ( rv < 0 ):
      PyErr_SetFromErrnoWithFilenameObject(OSError, self.mgr().name())
    return rv

  def i2cRead(self, uint8_t sla, uint8_t off, pybuf):
    sla = (sla<<1) | I2C_READ
    return self.i2cRW( sla, off, pybuf )

  def i2cWrite(self, uint8_t sla, uint8_t off, pybuf):
    sla = (sla<<1) & ~I2C_READ
    return self.i2cRW( sla, off, pybuf )

cdef class FEC(FwDev):
  cdef double min_, max_

  def __init__(self, *args, **kwargs):
    cdef int st
    with self._mgr as fw, nogil:
      st = fecGetAttRange( fw, &self.min_, &self.max_ )
    if ( st < 0 ):
      raise RuntimeError("Front-End has no Attenuator controls")

  def setAttenuator(self, int channel, bool on):
    cdef int st
    cdef double val
    if on:
      val = self.max_
    else:
      val = self.min_
    with self._mgr as fw, nogil:
      st = fecSetAtt( fw, channel, val )
    if ( st < 0 ):
      raise RuntimeError("Front-End 'setAttenuator' failed")

  def getAttenuator(self, int channel):
    cdef int st
    cdef double val
    with self._mgr as fw, nogil:
      st = fecGetAtt( fw, channel, &val )
    if ( st < 0 ):
      raise RuntimeError("Front-End 'getAttenuator' failed")
    return abs(self.max_ - val) < abs(self.min_ - val)

  def setTermination(self, int channel, bool on):
    cdef int st, val
    # bool is a python object and cannot be used w/o gil
    val = on
    with self._mgr as fw, nogil:
      st = fecSetTermination( fw, channel, val )
    if ( st < 0 ):
      raise RuntimeError("Front-End 'setTermination' failed")

  def getTermination(self, int channel):
    cdef int st
    with self._mgr as fw, nogil:
      st = fecGetTermination( fw, channel )
    if ( st < 0 ):
      raise RuntimeError("Front-End 'getTermination' failed")
    return st > 0

  def setACMode(self, int channel, bool on):
    cdef int st, val
    # bool is a python object and cannot be used w/o gil
    val = on
    with self._mgr as fw, nogil:
      st = fecSetACMode( fw, channel, val )
    if ( st < 0 ):
      raise RuntimeError("Front-End 'setACMode' failed")

  def getACMode(self, int channel):
    cdef int st
    with self._mgr as fw, nogil:
      st = fecGetACMode( fw, channel )
    if ( st < 0 ):
      raise RuntimeError("Front-End 'getACMode' failed")
    return st > 0

  def setDacRangeHi(self, int channel, bool on):
    cdef int st, val
    # bool is a python object and cannot be used w/o gil
    val = on
    with self._mgr as fw, nogil:
      st = fecSetDACRangeHi( fw, channel, val )
    if ( st < 0 ):
      raise RuntimeError("Front-End 'setDACRangeHi' failed")

  def getDacRangeHi(self, int channel):
    cdef int st
    with self._mgr as fw, nogil:
      st = fecGetDACRangeHi( fw, channel )
    if ( st < 0 ):
      raise RuntimeError("Front-End 'getDACRangeHi' failed")
    return st > 0

cdef class FwComm:
  cdef FwMgr           _mgr
  cdef VersaClk        _clk
  cdef DAC47CX         _dac
  cdef Amp             _amp
  cdef FEC             _fec
  cdef LED             _led
  cdef Max195xxADC     _adc
  cdef int             _bufsz
  cdef uint8_t         _bufflags
  cdef AcqParams       _parmCache
  cdef Mtx             _syncMtx
  cdef Cond            _asyncEvt
  cdef pthread_t       _reader
  cdef object          _callable
  cdef object          _buf
  cdef float           _timo

  def exc(self):
    raise TimeoutError("foo")

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
  cdef void * threadFunc(void *arg) noexcept nogil:
    with gil:
      return (<FwComm>arg).pyThreadFunc()

  cdef void * pyThreadFunc(self):
    while True:
      timeout = -1.0 # silence compiler warning
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
      while ( True ):
        try:
          rv, hdr = self.read( pyb )
        except Exception as e:
          print("Exception in read (ignoring): ", e)
          rv = 0
        if ( rv != 0 ):
          break
        else:
          if ( numIter > 0 ):
            numIter -= 1
          if ( numIter == 0 ):
            break
          # avoid busy polling if there is no data
          sleep(0.01)
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
    cdef int64_t ver
    self._mgr   = FwMgr( name, speed, args, kwargs )
    with self._mgr as fw, nogil:
      st = acq_set_params( fw, NULL, &self._parmCache )
    if ( st < 0 ):
      raise IOError("FwComm.__init__(): acq_set_params failed")
    self._clk    = VersaClk( self._mgr )
    self._dac    = DAC47CX( self._mgr )
    self._adc    = Max195xxADC( self._mgr )
    self._amp    = Amp( self._mgr )

    brdVers      = self.boardVersion()
    if   ( 0 == brdVers ):
      self._fec = FEC( self._mgr )
      self._led = LED( self._mgr )
    elif ( 1 == brdVers or 2 == brdVers ):
      self._fec = FEC( self._mgr )
      self._led = LEDv1( self._mgr )
    else:
      self._fec = FEC( self._mgr )
      self._led = LED( self._mgr )

    with self._mgr as fw, nogil:
      self._bufsz    = buf_get_size( fw )
      self._bufflags = buf_get_flags( fw )
    st = pthread_create( &self._reader, NULL, self.threadFunc, <void*>self )
    if ( st != 0 ):
      raise OSError("pthread_create failed with status {:d}".format(st))

  def mgr(self):
    return self._mgr

  def setDebug(self, int level):
    with self._mgr as fw, nogil:
      fw_set_debug( fw, level )

  def init( self, force = False ):
    cdef int st,forceVal
    forceVal = force
    with self._mgr as fw, nogil:
      st = scopeInit( fw, forceVal ) 
    if ( st < 0 ):
      raise RuntimeError("scopeInit failed")

  def getAdcClkFreq(self):
    cdef double f
    with self._mgr as fw, nogil:
      f = buf_get_sampling_freq( fw )
    return f

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

  def ampGetS2Range(self):
    return self._amp.getS2Range()

  def ampGetS2Att(self, unsigned channel):
    return self._amp.getS2Att( channel )

  def ampSetS2Att(self, unsigned channel, double att):
    self._amp.setS2Att(channel, att)

  def fecGetACMode(self, int channel):
    return self._fec.getACMode( channel )

  def fecSetACMode(self, int channel, bool on ):
    self._fec.setACMode( channel, on )

  def fecGetAttenuator(self, int channel):
    return self._fec.getAttenuator( channel )

  def fecSetAttenuator(self, int channel, bool on ):
    self._fec.setAttenuator( channel, on )

  def fecGetTermination(self, int channel):
    return self._fec.getTermination( channel )

  def fecSetTermination(self, int channel, bool on ):
    self._fec.setTermination( channel, on )

  def fecGetDacRangeHi(self, int channel):
    return self._fec.getDacRangeHi( channel )

  def fecSetDacRangeHi(self, int channel, bool on ):
    self._fec.setDacRangeHi( channel, on )

  def ledGet(self, name):
    try:
      return self._led.getVal(name)
    except KeyError:
      print("No such LED")
      return False

  def ledSet(self, name, val):
    try:
      self._led.setVal(name, val)
    except KeyError:
      print("No such LED")

  def version(self):
    cdef uint32_t ver
    with self._mgr as fw, nogil:
      ver = fw_get_version( fw )
    return ver

  def apiVersion(self):
    cdef uint8_t ver
    with self._mgr as fw, nogil:
      ver = fw_get_api_version( fw )
    return ver

  def boardVersion(self):
    cdef uint8_t ver
    with self._mgr as fw, nogil:
      ver = fw_get_board_version( fw )
    return ver


  def getBufSize(self):
    return self._bufsz

  def getSampleSize(self):
    if ( 0 != (self._bufflags & FW_BUF_FLG_16B) ):
      return 2
    else:
      return 1

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
    if   ( ( b.itemsize == 1 ) or ( b.itemsize == 2 ) ):
      with self._mgr as fw, nogil:
        rv = buf_read( fw, &hdr, <uint8_t*>b.buf, b.len )
    elif ( b.itemsize == sizeof(float) ):
      with self._mgr as fw, nogil:
        rv = buf_read_flt( fw, &hdr, <float*>b.buf, b.len )
    else:
      PyBuffer_Release( &b )
      raise ValueError("FwComm.read arg buffer itemsize must be 1,2 or {:d}".format(sizeof(float)))
    PyBuffer_Release( &b )
    if ( rv < 0 ):
      PyErr_SetFromErrnoWithFilenameObject(OSError, self.mgr().name())
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
      st = acq_set_level( fw, l, 1000 )
    if ( st < 0 ):
      raise IOError("acqSetTriggerLevelPercent()")
    self._parmCache.level = l

  def acqGetNPreTriggerSamples(self):
    return self._parmCache.npts

  def acqSetNPreTriggerSamples(self, int n):
    cdef int st
    if ( n < 0 or n >= self._parmCache.nsamples ):
      raise ValueError("acqSetNPreTriggerSamples(): # pre-trigger samples out of range")
    if ( self._parmCache.npts == n ):
      return
    with self._mgr as fw, nogil:
      st = acq_set_npts( fw, n )
    if ( st < 0 ):
      raise IOError("acqSetNPreTriggerSamples()")
    self._parmCache.npts = n

  def acqSetNSamples(self, int n):
    cdef int st
    if ( n < 1 or n > self._bufsz ):
      raise ValueError("acqSetNSamples(): # samples out of range")
    if ( self._parmCache.nsamples == n ):
      return
    with self._mgr as fw, nogil:
      st = acq_set_nsamples( fw, n )
    if ( st < 0 ):
      raise IOError("acqSetNSamples()")
    # fw clips pts; update cache
    if ( self._parmCache.npts >= n ):
      self._parmCache.npts = n - 1;
    self._parmCache.nsamples = n

  def acqGetNSamples(self):
    return self._parmCache.nsamples

  def acqGetDecimation(self):
    return self._parmCache.cic0Decimation, self._parmCache.cic1Decimation

  def acqSetDecimation(self, n0, n1 = None):
    cdef uint8_t  cic0Dec
    cdef uint32_t cic1Dec
    cdef int      st
    cic1Dec = 1 # silence compiler warning
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
    if ( rising ):
      irising = 1
    else:
      irising = -1
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
    if ( 2 == self.getSampleSize() ):
      dtype = 'int16'
    else:
      dtype = 'int8'
    return numpy.zeros( (self._bufsz, 2), dtype = dtype )

  def mkFltBuf(self):
    return numpy.zeros( (self._bufsz, 2), dtype = "float32" )

# 'expert' class that gives access to low-level
# details that may not be portable across boards
cdef class FwCommExprt(FwComm):

  def __init__(self, *args, **kwargs):
    super().__init__(*args, **kwargs)

  def xfer(self, uint8_t cmd, txb = None, rxb = None):
    cdef Py_buffer tb
    cdef Py_buffer rb
    cdef int       rv
    cdef uint8_t  *tp;
    cdef uint8_t  *rp;
    cdef size_t    tl;
    cdef size_t    rl;
    cdef size_t    l;

    tp = <uint8_t*>0
    rp = <uint8_t*>0
    tl = 0
    rl = 0
    if ( not txb is None ):
      if ( not PyObject_CheckBuffer( txb ) or 0 != PyObject_GetBuffer( txb, &tb, PyBUF_C_CONTIGUOUS ) ):
        raise ValueError("FwComm.xfer txb arg must support buffer protocol")
      if ( tb.itemsize != 1 ):
        PyBuffer_Release( &tb )
        raise ValueError("FwComm.xfer txb itemsize must be 1")
      tp = <uint8_t*>tb.buf
      tl = tb.len
      if tl == 0:
        tp = <uint8_t*>0

    if ( not rxb is None ):
      if ( not PyObject_CheckBuffer( rxb ) or 0 != PyObject_GetBuffer( rxb, &rb, PyBUF_C_CONTIGUOUS | PyBUF_WRITEABLE ) ):
        raise ValueError("FwComm.xfer rxb arg must support buffer protocol")
      if ( rb.itemsize != 1 ):
        PyBuffer_Release( &rb )
        raise ValueError("FwComm.xfer rxb itemsize must be 1")
      rp = <uint8_t*>rb.buf
      rl = rb.len
      if rl == 0:
        rp = <uint8_t*>0

    if ( rl != 0 and tl !=0 and rl != tl ):
        if ( not rxb is None ):
          PyBuffer_Release( &rb )
        if ( not txb is None ):
          PyBuffer_Release( &tb )
        raise ValueError("FwComm.xfer txb and rxb must be of the same size (or None)")

    # tl and rl are either the same or one of them is 0
    if tl > 0:
      l = tl
    else:
      l = rl

    with self._mgr as fw, nogil:
      rv = fw_xfer( fw, cmd, tp, rp, l )

    if ( not txb is None ):
      PyBuffer_Release( &tb )
    if ( not rxb is None ):
      PyBuffer_Release( &rb )
    if ( rv < 0 ):
      PyErr_SetFromErrnoWithFilenameObject(OSError, self.mgr().name())
    return rv


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

  def clkGetFBDiv(self):
    return self._clk.getFBDiv()

  def clkSetOutDiv(self, int out, float div):
    self._clk.setOutDiv( out, div )

  def clkGetOutDiv(self, int out):
    return self._clk.getOutDiv( out )

  def clkSetFODRoute(self, int out, VersaClkFODRoute rte):
    self._clk.setFODRoute( out, rte )

  def clkReadReg(self, int reg):
    return self._clk.readReg(reg)

  def clkWriteReg(self, int reg, int val):
    return self._clk.writeReg(reg, val)

  def ampReadReg(self, unsigned channel, int reg):
    return self._amp.readReg( channel, reg );

  def ampWriteReg(self, unsigned channel, int reg, uint8_t val):
    self._amp.writeReg( channel, reg, val );

  def eepromGetSize(self):
    cdef int sz
    with self._mgr as fw, nogil:
      sz = eepromGetSize( fw )
    if ( sz < 0 ):
      raise RuntimeError("FwCommExprt.eepromGetSize failed: {}".format(strerror(-sz)))
    return sz

  cdef eepromRW(self, bool rnw, uint8_t off, pyb):
    cdef Py_buffer b
    cdef int       rv
    if ( not PyObject_CheckBuffer( pyb ) or 0 != PyObject_GetBuffer( pyb, &b, PyBUF_C_CONTIGUOUS | PyBUF_WRITEABLE ) ):
      raise ValueError("FwCommExprt.eepromRW arg must support buffer protocol")
    if   ( b.itemsize == 1 ) :
      if ( rnw ):
        with self._mgr as fw, nogil:
           rv = eepromRead( fw, off, <uint8_t*>b.buf, b.len )
      else:
        with self._mgr as fw, nogil:
           rv = eepromWrite( fw, off, <uint8_t*>b.buf, b.len )
    else:
      PyBuffer_Release( &b )
      raise ValueError("FwCommExprt.eepromRW arg buffer itemsize must be 1")
    PyBuffer_Release( &b )
    if ( rv < 0 ):
      if ( rnw ):
        nm = "Read"
      else:
        nm = "Write"
      raise RuntimeError("FwCommExprt.eeprom{} failed: {}".format(nm, strerror(-rv)))
    return rv

  def eepromRead(self, unsigned off, pb):
    return self.eepromRW( True, off, pb )

  def eepromWrite(self, unsigned off, pb):
    return self.eepromRW( False, off, pb )
