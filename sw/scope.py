#!/usr/bin/env python3
import sys
import pyfwcomm  as fw
from   PyQt5     import QtCore, QtGui, QtWidgets, Qwt
from   Utils     import createValidator, MenuButton
import numpy     as np
from   threading import Thread, RLock, Lock, Condition, Semaphore
import time
import getopt

class Buf(object):
  def __init__(self, sz, npts = -1, scal = 1.0, *args, **kwargs):
    # Using polygons apparently lets us avoid yet another copy...
    super().__init__(*args, **kwargs)
    self._curv = [ QtGui.QPolygonF(sz), QtGui.QPolygonF(sz) ]
    # no way to find out if Qt was configured for single-
    # or double-precision float without resorting e.g., to cython...
    # just assume double
    dt         = np.dtype('float64')
    self._sz   = sz
    self._npts = npts
    self._scal = scal
    self._mem  = [ np.frombuffer( p.data().asarray( 2 * dt.itemsize * sz ), dtype=dt ) for p in self._curv ]
    self.updateX( npts )

  def updateX(self, npts, scal = 1.0):
    if ( (self._scal != scal or self._npts != npts) and (self._npts >= 0 or npts >= 0) ):
      if ( npts >= 0 ):
        self._npts = npts
      if ( scal > 0. ):
        self._scal = scal
      strt       = -npts * self._scal
      stop       = (self._sz - 1 - npts) * self._scal
      self._mem[0][0::2] = np.linspace(strt, stop, self._sz, endpoint=True)
      for m in self._mem[1:]:
        m[0::2] = self._mem[0][0::2]

  def updateY(self, buf):
    stride = len(self._mem)
    for idx in range(stride):
      self._mem[idx][1::2] = buf[:,idx]

  def getCurv(self, idx):
    return self._curv[idx]

class Scope(QtCore.QObject):

  haveData = QtCore.pyqtSignal()

  def __init__(self, devnam, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self._fw       = fw.FwComm( devnam )
    if ( not self._fw.isADCDLLLocked() ):
      self._fw.init()
    self._fw.setClkFODRoute( fw.SEL_EXT, fw.CASC_OUT )
    self._main     = QtWidgets.QMainWindow()
    self._cent     = QtWidgets.QWidget()
    self._main.setCentralWidget( self._cent )
    hlay           = QtWidgets.QHBoxLayout()
    self._plot     = Qwt.QwtPlot()
    self._plot.setAutoReplot( True )
    self.updateYAxis()
    self.updateXAxis()
    self._zoom     = Qwt.QwtPlotZoomer( self._plot.canvas() )
    hlay.addWidget( self._plot )
    vlay           = QtWidgets.QVBoxLayout()
    hlay.addLayout( vlay )
    frm            = QtWidgets.QFormLayout()
    edt            = QtWidgets.QLineEdit()
    def g():
      rv = self._fw.getAcqTriggerLevelPercent()
      return "{:.0f}".format(rv)
    def s(s):
      self._fw.setAcqTriggerLevelPercent( float(s) )
    createValidator( edt, g, s, QtGui.QDoubleValidator, -100.0, +100.0, 1 )
    frm.addRow( QtWidgets.QLabel("Trigger Level [%]"), edt )
    class TrgSrcMenu(MenuButton):
      def __init__(mb, parent = None):
        src,edg = self._fw.getAcqTriggerSource()
        if   ( src == fw.CHA ):
          l0 = "Channel A"
        elif ( src == fw.CHB ):
          l0 = "Channel B"
        else:
          l0 = "External"
        MenuButton.__init__(mb, [l0, "Channel A", "Channel B", "External "], parent )

      def activated(mb, act):
        super().activated(act)
        txt     = act.text()
        src,edg = self._fw.getAcqTriggerSource()
        if   ( txt == "Channel A" ):
          src = fw.CHA
        elif ( txt == "Channel B" ):
          src = fw.CHB
        else:
          src = fw.EXT
        self._fw.setAcqTriggerSource( src, edg )
        
    frm.addRow( QtWidgets.QLabel("Trigger Source"), TrgSrcMenu() )

    class TrgEdgMenu(MenuButton):
      def __init__(mb, parent = None):
        src,edg = self._fw.getAcqTriggerSource()
        if   ( edg ):
          l0 = "Rising"
        else:
          l0 = "Falling"
        MenuButton.__init__(mb, [l0, "Rising", "Falling"], parent )

      def activated(mb, act):
        super().activated(act)
        txt     = act.text()
        src,edg = self._fw.getAcqTriggerSource()
        edg     = (txt == "Rising")
        self._fw.setAcqTriggerSource( src, edg )
 
    frm.addRow( QtWidgets.QLabel("Trigger Edge"), TrgEdgMenu() )

    class TrgAutMenu(MenuButton):
      def __init__(mb, parent = None):
        val = self._fw.getAcqAutoTimeoutMs()
        l0  = "On" if val >= 0 else "Off"
        MenuButton.__init__(mb, [l0, "On", "Off"], parent )

      def activated(mb, act):
        super().activated(act)
        txt     = act.text()
        val     = 100 if txt == "On" else -1
        self._fw.setAcqAutoTimeoutMs( val )
 
    frm.addRow( QtWidgets.QLabel("Trigger Auto"), TrgAutMenu() )


    edt = QtWidgets.QLineEdit()
    def g():
      return str( self._fw.getAcqNPreTriggerSamples() )
    def s(s):
      npts = int(s)
      self._fw.setAcqNPreTriggerSamples( npts )
      self.updateXAxis()
      self._reader.setParms( npts=npts )
      self._zoom.setZoomBase()
    createValidator( edt, g, s, QtGui.QIntValidator, 0, self._fw.getBufSize() - 1  )
    frm.addRow( QtWidgets.QLabel("Trigger Sample #"), edt )

    edt = QtWidgets.QLineEdit()
    def g():
      d0, d1 = self._fw.getAcqDecimation()
      return str( d0*d1 )
    def s(s):
      self._fw.setAcqDecimation( int(s) )
    createValidator( edt, g, s, QtGui.QIntValidator, 1, 16*2**12 )
    frm.addRow( QtWidgets.QLabel("Decimation"), edt )
 
    vlay.addLayout( frm )

    self._cent.setLayout( hlay )
    sl             = self.mksl(0)
    vlay.addLayout( sl )
    sl             = self.mksl(1)
    vlay.addLayout( sl )

    self._c1 = Qwt.QwtPlotCurve()
    self._c1.attach( self._plot )
    self._c2 = Qwt.QwtPlotCurve()
    self._c2.attach( self._plot )
    self._reader = Reader( self )

    self.haveData.connect( self.updateData, QtCore.Qt.QueuedConnection )
    self._data   = None
    self._reader.start()

  def updateYAxis(self):
    self._plot.setAxisScale( Qwt.QwtPlot.yLeft, -128, 127 )

  def updateXAxis(self):
    n = self._fw.getBufSize() - 1
    t = self._fw.getAcqNPreTriggerSamples()
    xmax = n - t
    xmin = xmax - n
    self._plot.setAxisScale( Qwt.QwtPlot.xBottom, xmin, xmax )

  def updateData(self):
    d = self._reader.getData()
    if d is None:
      return
    if not self._data is None:
      self._reader.putBuf( self._data )
    self._data = d
    self._c1.setSamples( d.getCurv( 0 ) )
    self._c2.setSamples( d.getCurv( 1 ) )

  def mksl(self, ch):
    hb             = QtWidgets.QHBoxLayout()
    sl             = QtWidgets.QSlider( QtCore.Qt.Horizontal )
    sl.setMinimum(0)
    sl.setMaximum(20)
    sl.setTickPosition( QtWidgets.QSlider.TicksBelow )
    a  = int( round( self._fw.getS2Att( ch ) ) )
    self._fw.setS2Att( ch, a )
    sl.setValue( a )
    lb             = QtWidgets.QLabel( str(a) +"dB" )
    def cb(val):
      self._fw.setS2Att(ch, val )
      lb.setText( str(val) + "dB" )
    sl.valueChanged.connect( cb )
    hb.addWidget(sl)
    hb.addWidget(lb)
    return hb

  def getFw(self):
    return self._fw

  def show(self):
    self._main.show()

  def notify(self):
    self.haveData.emit()

class Reader(QtCore.QThread):

  def __init__(self, scope, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self._lck            = Lock()
    self._bufAvail       = Condition( self._lck )
    self._readDone       = Semaphore( 0 )
    self._scope          = scope
    self._fw             = self._scope.getFw() 
    sz                   = self._fw.getBufSize()
    self._bufs           = [ Buf(sz) for i in range(3) ] 
    self._rbuf           = [ self._fw.mkBuf(), self._fw.mkBuf() ]
    self._ridx           = 0
    # read time for 2x16k = 32k samples is ~50ms
    self._pollInterval   = 0.10
    self._npts           = self._fw.getAcqNPreTriggerSamples()
    self._scal           = 1.0
    self._processedBuf   = None

  def read(self):
   if True:
    self._fw.readAsync( self._rbuf, self )
    self._readDone.acquire()
   else:
    while True:
      rv, hdr = self._fw.read( self._rbuf )
      if ( rv > 0 ):
        return
      time.sleep( self._pollInterval )

  def __call__(self, rv, hdr, buf):
    if ( rv <= 0):
      raise RuntimeError("indefinited async read returned ", rv)
    self._readDone.release()

  def readAsync(self):
    rv          = self._ridx
    self._ridx ^= 1
    if not self._fw.readAsync( self._rbuf[ self._ridx ], self ):
      raise RuntimeError("Unable to schedule async read")
    return rv

  def run(self):
    then = time.monotonic()
    lp   = 0
    self.readAsync()
    while True:
      b    = self.getBuf()
      self._readDone.acquire()
      # flip buffer
      ridx        = self.readAsync()
      b.updateY( self._rbuf[ ridx ] )
      with self._lck:
        npts = self._npts
        scal = self._scal
      b.updateX( npts, scal )
      with self._lck:
        if (not self._processedBuf is None):
          self._bufs.append( self._processedBuf )
        self._processedBuf = b
      self._scope.notify()
      now   = time.monotonic()
      delta = self._pollInterval - (now - then)
      then  = now
      if ( delta > 0.0 ):
        time.sleep( delta )
        if ( not sys.flags.interactive ):
          lp += 1
          if lp == 10:
            print(".")
            lp = 0
      else:
        if ( not sys.flags.interactive ):
          print("Nosleep ", delta)

  def getData(self):
    with self._lck:
      rv = self._processedBuf
      self._processedBuf = None
    return rv

  def getBuf(self):
    with self._bufAvail:
      while ( len(self._bufs) == 0 ):
        self._bufAvail.wait()
      return self._bufs.pop(0)

  def putBuf(self, b):
    with self._bufAvail:
      self._bufs.append(b)
      self._bufAvail.notify()

  def setParms(self, npts = -1, scal = -1.0):
    with self._lck:
      if ( npts >= 0 ):
        self._npts = npts
      if ( scal >  0.0 ):
        self._scal = scal

class ScopeThread(QtCore.QThread):

  def run(self):
    self.exec()

def usage(nm):
  print("usage: {} [-d <usb_device_name] [-h]".format(nm))

if __name__ == "__main__":

  devn = '/dev/ttyUSB0'

  ( opts, args ) = getopt.getopt( sys.argv[1:], "hd:", [] )
  for opt in opts:
    if   ( opt[0] == '-d' ):
      devn = opt[1]
    elif ( opt[0] == '-h' ):
      usage( sys.argv[0] )
      sys.exit(0)

  app = QtWidgets.QApplication( args )
  scp = Scope( devn )
  scp.show()
  if ( sys.flags.interactive ):
    scpThread = ScopeThread()
    scpThread.start()
    com       = scp.getFw()
    print("Interactive mode; Firmware handle is 'com', Scope handle is 'scp'")
  else:
    app.exec()
