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
    self._hdr  = 0
    self.updateX( npts )
    self._mean = [0. for i in range(len(self._mem))]
    self._std  = [0. for i in range(len(self._mem))]

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

  def updateY(self, buf, hdr):
    stride = len(self._mem)
    for idx in range(stride):
      self._mem[idx][1::2] = buf[:,idx]
      self._mean[idx] = np.mean(buf[:,idx])
      self._std[idx]  = np.std(buf[:,idx])
    self._hdr = hdr

  def getCurv(self, idx):
    return self._curv[idx]

  def getHdr( self ):
    return self._hdr

class Scope(QtCore.QObject):

  haveData = QtCore.pyqtSignal()

  def __init__(self, devnam, *args, **kwargs):
    super().__init__(*args, **kwargs)
    if ( sys.flags.interactive ):
       self._fw       = fw.FwCommExprt( devnam )
    else:
       self._fw       = fw.FwComm( devnam )
    self._fw.init()
    self._main     = QtWidgets.QMainWindow()
    self._cent     = QtWidgets.QWidget()
    self._main.setCentralWidget( self._cent )
    hlay           = QtWidgets.QHBoxLayout()
    self._plot     = Qwt.QwtPlot()
    self._plot.setAutoReplot( True )
    self.updateYAxis()
    self.updateXAxis()
    self._zoom     = Qwt.QwtPlotZoomer( self._plot.canvas() )
    hlay.addWidget( self._plot, stretch = 2 )
    vlay           = QtWidgets.QVBoxLayout()
    hlay.addLayout( vlay )
    frm            = QtWidgets.QFormLayout()
    edt            = QtWidgets.QLineEdit()
    self._numCh    = 2
    self._channelColors = [ QtGui.QColor( QtCore.Qt.blue ), QtGui.QColor( QtCore.Qt.black ) ]
    self._channelNames  = [ "A", "B" ]
    self._trgArm        = "Continuous"
    def g():
      rv = self._fw.acqGetTriggerLevelPercent()
      return "{:.0f}".format(rv)
    def s(s):
      self._fw.acqSetTriggerLevelPercent( float(s) )
    createValidator( edt, g, s, QtGui.QDoubleValidator, -100.0, +100.0, 1 )
    frm.addRow( QtWidgets.QLabel("Trigger Level [%]"), edt )
    class TrgSrcMenu(MenuButton):
      def __init__(mb, parent = None):
        src,edg = self._fw.acqGetTriggerSource()
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
        src,edg = self._fw.acqGetTriggerSource()
        if   ( txt == "Channel A" ):
          src = fw.CHA
        elif ( txt == "Channel B" ):
          src = fw.CHB
        else:
          src = fw.EXT
        self._fw.acqSetTriggerSource( src, edg )
        
    frm.addRow( QtWidgets.QLabel("Trigger Source"), TrgSrcMenu() )

    class TrgEdgMenu(MenuButton):
      def __init__(mb, parent = None):
        src,edg = self._fw.acqGetTriggerSource()
        if   ( edg ):
          l0 = "Rising"
        else:
          l0 = "Falling"
        MenuButton.__init__(mb, [l0, "Rising", "Falling"], parent )

      def activated(mb, act):
        super().activated(act)
        txt     = act.text()
        src,edg = self._fw.acqGetTriggerSource()
        edg     = (txt == "Rising")
        self._fw.acqSetTriggerSource( src, edg )
 
    frm.addRow( QtWidgets.QLabel("Trigger Edge"), TrgEdgMenu() )

    class TrgAutMenu(MenuButton):
      def __init__(mb, parent = None):
        val = self._fw.acqGetAutoTimeoutMs()
        l0  = "On" if val >= 0 else "Off"
        MenuButton.__init__(mb, [l0, "On", "Off"], parent )

      def activated(mb, act):
        super().activated(act)
        txt     = act.text()
        val     = 100 if txt == "On" else -1
        self._fw.acqSetAutoTimeoutMs( val )
 
    frm.addRow( QtWidgets.QLabel("Trigger Auto"), TrgAutMenu() )

    class TrgArmMenu(MenuButton):
      def __init__(mb, parent = None):
        val = self._trgArm
        MenuButton.__init__(mb, [val, "Off", "Single", "Continuous"], parent )

      def activated(mb, act):
        super().activated(act)
        txt     = act.text()
        self._trgArm = txt

    self._trgArmMenu = TrgArmMenu()
    frm.addRow( QtWidgets.QLabel("Arm Trigger"), self._trgArmMenu )
 
    edt = QtWidgets.QLineEdit()
    def g():
      return str( self._fw.acqGetNPreTriggerSamples() )
    def s(s):
      npts = int(s)
      self._fw.acqSetNPreTriggerSamples( npts )
      self.updateXAxis()
      self._reader.setParms( npts=npts )
      self._zoom.setZoomBase()
    createValidator( edt, g, s, QtGui.QIntValidator, 0, self._fw.getBufSize() - 1  )
    frm.addRow( QtWidgets.QLabel("Trigger Sample #"), edt )

    edt = QtWidgets.QLineEdit()
    def g():
      d0, d1 = self._fw.acqGetDecimation()
      return str( d0*d1 )
    def s(s):
      self._fw.acqSetDecimation( int(s) )
    createValidator( edt, g, s, QtGui.QIntValidator, 1, 16*2**12 )
    frm.addRow( QtWidgets.QLabel("Decimation"), edt )

    frm.addRow( QtWidgets.QLabel("ADC Clock Freq."), QtWidgets.QLabel("{:g}".format( self._fw.getAdcClkFreq() )) )
 
    self._cent.setLayout( hlay )
    self._ov       = []
    self._ch       = []
    frm.addRow( QtWidgets.QLabel("Attenuator:") )
    for i in range(self._numCh):
      sl,  ov        = self.mksl( i, self._channelColors[i] )
      self._ov.append( ov )
      frm.addRow( sl )
      ch = Qwt.QwtPlotCurve()
      ch.attach( self._plot )
      ch.setPen( self._channelColors[i] )
      self._ch.append( ch )

    frm.addRow( QtWidgets.QLabel("Measurements:") )
    self._meanLbls = []
    self._stdLbls  = []
    for i in range(self._numCh):
       valLbl = QtWidgets.QLabel("")
       valLbl.setStyleSheet("color: {:s}; qproperty-alignment: AlignRight".format( self._channelColors[i].name() ) )
       self._meanLbls.append( valLbl )
       frm.addRow( QtWidgets.QLabel("Mean {:s}".format( self._channelNames[i] )), valLbl )
       valLbl = QtWidgets.QLabel("")
       valLbl.setStyleSheet("color: {:s}; qproperty-alignment: AlignRight".format( self._channelColors[i].name() ) )
       self._stdLbls.append( valLbl )
       frm.addRow( QtWidgets.QLabel("Sdev {:s}".format( self._channelNames[i] )), valLbl )

    vlay.addLayout( frm )

    self._reader = Reader( self )

    self.haveData.connect( self.updateData, QtCore.Qt.QueuedConnection )
    self._data   = None
    self._reader.start()

  def updateYAxis(self):
    self._plot.setAxisScale( Qwt.QwtPlot.yLeft, -128, 127 )

  def updateXAxis(self):
    n = self._fw.getBufSize() - 1
    t = self._fw.acqGetNPreTriggerSamples()
    xmax = n - t
    xmin = xmax - n
    self._plot.setAxisScale( Qwt.QwtPlot.xBottom, xmin, xmax )

  def updateData(self):
    d = self._reader.getData()
    if d is None:
      return
    if ( self._trgArm == "Off" ):
      self._reader.putBuf( d )
      return
    if not self._data is None:
      self._reader.putBuf( self._data )
    self._data = d
    hdr        = d.getHdr()
    for i in range( self._numCh ):
      self._ch[i].setSamples( d.getCurv( i ) )
      self._ov[i].setVisible( (hdr & (1<<i)) != 0 )
      self._meanLbls[i].setText("{:>7.2f}".format( d._mean[i] ))
      self._stdLbls[i].setText ("{:>7.2f}".format( d._std[i]  ))
    if ( self._trgArm == "Single" ):
      self._trgArm = "Off"
      self._trgArmMenu.setText("Off")

  def mksl(self, ch, color):
    hb             = QtWidgets.QHBoxLayout()
    sl             = QtWidgets.QSlider( QtCore.Qt.Horizontal )
    minmax         = self._fw.ampGetS2Range()
    sl.setMinimum(minmax[0])
    sl.setMaximum(minmax[1])
    sl.setTickPosition( QtWidgets.QSlider.TicksBelow )
    a  = int( round( self._fw.ampGetS2Att( ch ) ) )
    self._fw.ampSetS2Att( ch, a )
    sl.setValue( a )
    lb             = QtWidgets.QLabel( str(a) +"dB" )
    lb.setStyleSheet("color: {:s}".format( color.name() ))
    def cb(val):
      self._fw.ampSetS2Att(ch, val )
      lb.setText( str(val) + "dB" )
    sl.valueChanged.connect( cb )
    ov = QtWidgets.QLabel()
    ov.setText( "Ovr" )
    ov.setVisible( False )
    pol = ov.sizePolicy()
    pol.setRetainSizeWhenHidden( True )
    ov.setSizePolicy( pol )
    hb.addWidget(ov)
    hb.addWidget(sl)
    hb.addWidget(lb)
    return hb, ov

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
    self._bufhdr         = [0, 0]
    self._ridx           = 0
    # read time for 2x16k = 32k samples is ~50ms
    self._pollInterval   = 0.10
    self._npts           = self._fw.acqGetNPreTriggerSamples()
    self._scal           = 1.0
    self._processedBuf   = None

  def __call__(self, rv, hdr, buf):
    if ( rv <= 0):
      raise RuntimeError("indefinited async read returned ", rv)
    self._bufhdr[ self._ridx ] = hdr
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
      b.updateY( self._rbuf[ ridx ], self._bufhdr[ ridx ] )
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
            #print(".")
            lp = 0
      else:
        #if ( not sys.flags.interactive ):
        #  print("Nosleep ", delta)
        pass

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
  styl = None

  ( opts, args ) = getopt.getopt( sys.argv[1:], "hd:S:", [] )
  for opt in opts:
    if   ( opt[0] == '-d' ):
      devn = opt[1]
    elif ( opt[0] == '-h' ):
      usage( sys.argv[0] )
      sys.exit(0)
    elif ( opt[0] == '-S' ):
      styl = opt[1]

  app = QtWidgets.QApplication( args )
  if ( not styl is None ):
    with open(styl, "r") as f:
      app.setStyleSheet( f.read() )
  scp = Scope( devn )
  scp.show()
  if ( sys.flags.interactive ):
    scpThread = ScopeThread()
    scpThread.start()
    com       = scp.getFw()
    print("Interactive mode; Firmware handle is 'com', Scope handle is 'scp'")
  else:
    app.exec()
