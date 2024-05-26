#!/usr/bin/env python3
import sys
import io
import pyfwcomm  as fw
from   PyQt5     import QtCore, QtGui, QtWidgets, Qwt, Qt
from   Utils     import createValidator, MenuButton
import numpy     as np
from   threading import Thread, RLock, Lock, Condition, Semaphore
import time
import getopt
from   MsgDialog import MessageDialog

class Buf(object):

  def __init__(self, pooler, sz, npts = -1, scal = 1.0, *args, **kwargs):
    # Using polygons apparently lets us avoid yet another copy...
    super().__init__(*args, **kwargs)
    self._pooler   = pooler
    self._curv     = [ QtGui.QPolygonF(sz), QtGui.QPolygonF(sz) ]
    # no way to find out if Qt was configured for single-
    # or double-precision float without resorting e.g., to cython...
    # just assume double
    dt             = np.dtype('float64')
    self._sz       = sz
    self._npts     = npts
    self._scal     = scal
    self._mem      = [ np.frombuffer( p.data().asarray( 2 * dt.itemsize * sz ), dtype=dt ) for p in self._curv ]
    self._hdr      = 0
    self.updateX( npts )
    self._mean     = [0. for i in range(len(self._mem))]
    self._std      = [0. for i in range(len(self._mem))]
    self._refCnt   = 0

  def incRef(self):
    self._refCnt += 1

  def decRef(self):
    self._refCnt -= 1
    return self._refCnt

  def put(self):
    self._pooler.putBuf( self )

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

  # data layout: data[channel][x0,y0,x1,y1,...]
  def getMem( self ):
    return self._mem

# context manager for a buffer
class BufMgr:
  def __init__(self, buf):
    self._buf = buf

  def __enter__(self):
    if ( not self._buf is None ):
      self._buf.incRef()
    return self._buf

  def __exit__(self, excTyp, excVal, traceBack):
    if ( not self._buf is None ):
      self._buf.put()
    return None

class ScaleMod(Qwt.QwtScaleDraw):
  def __init__(self, *args, **kwargs):
    super().__init__( *args, **kwargs )
    print("ScaleMod constructor")

  def draw(self, painter, palette):
    print("draw")
    super().draw(painter, palette)

  def drawLabel(self, painter, val):
    print("drawLabel", val)
    super().drawLabel(painter, val)
    print("Length: ", self.length())

  def label(self, val):
    print("ScaleMod label")
    return "{:f}".format(val/32768)

class TrigLevel(Qwt.QwtPlotMarker):

  def __init__(self, zoom, fw, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self._fw         = fw
    self._zoom       = zoom
    self._txtWdgt    = None
    self._lvlPercent = self._fw.acqGetTriggerLevelPercent()

  def setLevelPercent(self, levelPercent):
    if levelPercent < -100 or levelPercent > 100:
      raise RuntimeError("TrigLevel.setLevelPercent: argument out of range")
    self._fw.acqSetTriggerLevelPercent( levelPercent )
    self._lvlPercent = levelPercent
    self.updateMark()

  def updateMark(self):
    scl = self.getScale()
    self.setValue( 0, scl * self._lvlPercent/100.0 )

  def getLevelPercent(self):
    return self._lvlPercent

  def getScale(self):
    return self._zoom.zoomBase().bottom()
# doesn't work if it is zoomed
#    return self.plot().axisScaleDiv( Qwt.QwtPlot.yLeft ).upperBound()

  def update(self, point):
    self.setValue( point.x(), point.y() )
    self._lvlPercent = 100.0*point.y()/self.getScale()
    if not self._txtWdgt is None:
      self._txtWdgt.setText("{:.0f}".format( self._lvlPercent ))

  def updateDone(self):
    self._fw.acqSetTriggerLevelPercent( self._lvlPercent )

  def attachTxt(self, txtWdgt):
    self._txtWdgt = txtWdgt

  def attach(self, plot):
    super().attach( plot )
    self.updateMark()

class MoveableMarkers(QtCore.QObject):

  def __init__(self, plot, picker, markers, *args, **kwargs):
    super().__init__( *args, **kwargs )
    self._plot     = plot
    self._picker   = picker
    self._markers  = markers
    self._selected = -1
    def act( on ):
      if not on:
        if ( self._selected >= 0 ):
          self._markers[self._selected].updateDone()
        self._selected = -1
    def mov( point ):
      if ( self._selected < 0 ):
        i    = 0
        dmin = 0.0
        for m in self._markers:
          ls = m.lineStyle()
          if   ( ls == Qwt.QwtPlotMarker.VLine ):
            xfrm = self._plot.canvasMap( Qwt.QwtPlot.xBottom )
            d = np.abs( xfrm.transform(point.x()) - xfrm.transform(m.xValue()) )
          elif ( ls == Qwt.QwtPlotMarker.HLine ):
            yfrm = self._plot.canvasMap( Qwt.QwtPlot.yLeft )
            d = np.abs( yfrm.transform(point.y()) - yfrm.transform(m.yValue()) )
          else:
            xfrm = self._plot.canvasMap( Qwt.QwtPlot.xBottom )
            yfrm = self._plot.canvasMap( Qwt.QwtPlot.yLeft )
            d = np.hypot(
                          xfrm.transform( point.x() ) - xfrm.transform( m.xValue() ),
                          yfrm.transform( point.y() ) - yfrm.transform( m.yValue() )
                        )
          if ( 0 == i or d < dmin ):
            self._selected = i
            dmin           = d
          i += 1
      m  = self._markers[ self._selected ]
      m.update( point )
    self._picker.activated.connect( act )
    self._picker.moved.connect( mov )

class Scope(QtCore.QObject):

  haveData = QtCore.pyqtSignal()

  def __init__(self, devnam, isSim = False, *args, **kwargs):
    super().__init__(*args, **kwargs)
    if ( sys.flags.interactive ):
       self._fw       = fw.FwCommExprt( devnam )
    else:
       self._fw       = fw.FwComm( devnam )
    if not isSim:
      self._fw.init()
    self._main          = QtWidgets.QMainWindow()
    self._cent          = QtWidgets.QWidget()
    menuBar             = QtWidgets.QMenuBar()
    fileMenu            = menuBar.addMenu( "File" )
    self._main.setCentralWidget( self._cent )
    self._main.setMenuBar( menuBar )
    hlay                = QtWidgets.QHBoxLayout()
    self._plot          = Qwt.QwtPlot()
    self._plot.setAutoReplot( True )
    d0, d1 = self._fw.acqGetDecimation()
    self._decimation    = d0*d1
    if not isSim:
      self._adcClkFreq    = self._fw.getAdcClkFreq()
    else:
      self._adcClkFreq    = 120.0e6
    self._plot.enableAxis( Qwt.QwtPlot.yRight )
    self._zoom          = Qwt.QwtPlotZoomer( self._plot.canvas() )
    self._zoom.setKeyPattern( Qwt.QwtEventPattern.KeyRedo, Qt.Qt.Key_I )
    self._zoom.setKeyPattern( Qwt.QwtEventPattern.KeyUndo, Qt.Qt.Key_O )
    self._zoom.setMousePattern( Qwt.QwtEventPattern.MouseSelect1, Qt.Qt.LeftButton,   Qt.Qt.ShiftModifier )
    self._zoom.setMousePattern( Qwt.QwtEventPattern.MouseSelect2, Qt.Qt.MiddleButton, Qt.Qt.ShiftModifier )
    self._zoom.setMousePattern( Qwt.QwtEventPattern.MouseSelect3, Qt.Qt.RightButton,  Qt.Qt.ShiftModifier )
    self._trigMarker    = Qwt.QwtPlotMarker()
    self._levlMarker    = TrigLevel( self._zoom, self._fw )
    self._trigMarker.setLineStyle( Qwt.QwtPlotMarker.VLine )
    self._levlMarker.setLineStyle( Qwt.QwtPlotMarker.HLine )
    self._trigMarker.attach( self._plot )
    self._levlMarker.attach( self._plot )
#    self._picker        = Qwt.QwtPlotPicker( self._plot.xBottom, self._plot.yLeft, Qwt.QwtPicker.NoRubberBand, Qwt.QwtPicker.AlwaysOn, self._plot.canvas() )
    self._picker        = Qwt.QwtPlotPicker( self._plot.xBottom, self._plot.yLeft,  self._plot.canvas() )
    self._picker.setStateMachine( Qwt.QwtPickerDragPointMachine() )
    self._plot.setAxisScaleDraw( Qwt.QwtPlot.yLeft, ScaleMod() )
    MoveableMarkers( self._plot, self._picker, [self._levlMarker] )
    if ( 2 == self._fw.getSampleSize() ):
      self._yScale = 32767
    else:
      self._yScale = 127
    self.updateYAxis()
    self.updateXAxis()
    hlay.addWidget( self._plot, stretch = 2 )
    vlay                = QtWidgets.QVBoxLayout()
    hlay.addLayout( vlay )
    frm                 = QtWidgets.QFormLayout()
    edt                 = QtWidgets.QLineEdit()
    self._numCh         = 2
    self._channelColors = [ QtGui.QColor( QtCore.Qt.blue ), QtGui.QColor( QtCore.Qt.black ) ]
    self._channelNames  = [ "A", "B" ]
    self._trgArm        = "Continuous"
    self.clrOvrLed()
    def g():
      rv = self._levlMarker.getLevelPercent()
      return "{:.0f}".format(rv)
    def s(s):
      self._levlMarker.setLevelPercent( float(s ) )
    createValidator( edt, g, s, QtGui.QDoubleValidator, -100.0, +100.0, 1 )
    self._levlMarker.attachTxt( edt )
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
        MenuButton.__init__(mb, [l0, "Channel A", "Channel B", "External"], parent )

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
        self.clrTrgLed()
        MenuButton.__init__(mb, [val, "Off", "Single", "Continuous"], parent )

      def activated(mb, act):
        super().activated(act)
        self.clrTrgLed()
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
      self._reader.setParms( npts=npts )
      self.updateXAxis()
    createValidator( edt, g, s, QtGui.QIntValidator, 0, self._fw.getBufSize() - 1  )
    frm.addRow( QtWidgets.QLabel("Trigger Sample #"), edt )

    edt = QtWidgets.QLineEdit()
    def g():
      return str( self._decimation )
    def s(s):
      val = int(s)
      self._fw.acqSetDecimation( val )
      self._decimation = val
      self._reader.setParms( scalX = (self._decimation / self._adcClkFreq) )
      self.updateXAxis()
    createValidator( edt, g, s, QtGui.QIntValidator, 1, 16*2**12 )
    frm.addRow( QtWidgets.QLabel("Decimation"), edt )

    frm.addRow( QtWidgets.QLabel("ADC Clock Freq."), QtWidgets.QLabel("{:g}".format( self._adcClkFreq )) )

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

    def tryAddTgl( lst, chn, lbls, getter, setter ):
      try:
        btn  = QtWidgets.QPushButton()
        btn.setCheckable( True )
        btn.setAutoDefault( False )
        def setLbl(checked):
          if ( checked ):
            btn.setText( lbls[0] )
            btn.setChecked(True)
          else:
            btn.setText( lbls[1] )
            btn.setChecked(False)
        setLbl( getter( chn ) )
        def cb( checked ):
          setLbl( checked )
          setter( chn, checked )
        btn.toggled.connect( cb )
        lst.append( btn )
        return True
      except RuntimeError:
        # not supported
        return False

    # try to make input controls
    wids = []
    for i in range(self._numCh):
      elms = []
      def setTerm(ch, val):
        self._fw.fecSetTermination(ch, val)
        self._fw.ledSet( 'Term{}'.format( self._channelNames[ch] ), val )
      if ( tryAddTgl( elms, i, ["50Ohm", "1MOhm" ], self._fw.fecGetTermination, setTerm ) ):
        self._fw.ledSet( 'Term{}'.format( self._channelNames[i] ), elms[-1].isChecked() )
      tryAddTgl( elms, i, ["DC",    "AC"    ], self._fw.fecGetACMode, self._fw.fecSetACMode )
      tryAddTgl( elms, i, ["-20dB", "0dB"   ], self._fw.fecGetAttenuator, self._fw.fecSetAttenuator )
      if len(elms) > 0:
        elms.insert(0, QtWidgets.QLabel("{}:".format(self._channelNames[i])))
        wids.append(elms)

    if ( len(wids) > 0 ):
      frm.addRow( QtWidgets.QLabel("Input Stage:") )
      for row in wids:
        hb = QtWidgets.QHBoxLayout()
        for w in row:
          hb.addWidget( w )
        frm.addRow( hb )

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

    self._data   = None

    def quitAction():
      sys.exit(0)

    def saveDataAction():
      self.saveData()

    self._msgDialog = MessageDialog( self._main, "UsbScope Message" )
    self._threadHandles = []

    fileMenu.addAction( "SaveData" ).triggered.connect( saveDataAction )
    fileMenu.addAction( "Quit" ).triggered.connect( quitAction )

    self._reader = Reader( self )
    self._reader.setParms( npts = self._fw.acqGetNPreTriggerSamples(), scalX = self._decimation / self._adcClkFreq )
    self.haveData.connect( self.updateData, QtCore.Qt.QueuedConnection )
    self._reader.start()

  def clrOvrLed(self):
    for chn in self._channelNames:
      self._fw.ledSet( 'OVR{}'.format( chn ), 0 )

  def clrTrgLed(self):
    self._fw.ledSet('Trig', 0)
    self.clrOvrLed()

  def updateYAxis(self):
    self._plot.setAxisScale( Qwt.QwtPlot.yLeft, -self._yScale - 1, self._yScale )
    self._plot.setAxisScale( Qwt.QwtPlot.yRight, - 1, 1 )
    self._zoom.setZoomBase()
    self._levlMarker.updateMark()

  def updateXAxis(self):
    n = self._fw.getBufSize() - 1
    t = self._fw.acqGetNPreTriggerSamples()
    xmax = n - t
    xmin = xmax - n
    xmin *= self._decimation / self._adcClkFreq
    xmax *= self._decimation / self._adcClkFreq
    self._plot.setAxisScale( Qwt.QwtPlot.xBottom, xmin, xmax )
    self._zoom.setZoomBase()

  def saveData(self):
    # hold a reference - while the filename dialog is spinning
    # events are dispatched and self._data may update
    with BufMgr( self._data ) as buf:
      if ( buf is None ):
        self._msgDialog.setText( "No Data Available To Save" )
        self._msgDialog.exec()
        return
      op  = QtWidgets.QFileDialog.Options()
      op |= QtWidgets.QFileDialog.DontUseNativeDialog;
      prop     = ""
      filetype = "Text Files (*.txt);;All Files (*)"
      fnam     = QtWidgets.QFileDialog.getSaveFileName( self._main, "File Name", prop, filetype, options=op )
      fnam     = fnam[0]
      if ( len( fnam ) == 0 ):
         # cancel
        return
      fw = FileWriter( buf, fnam )
      # keep a handle so the thread does not get garbage collected
      self._threadHandles.append( fw )
      def errorDialog(msg):
        self._threadHandles.remove( fw )
        if len( msg ) != 0:
          self._msgDialog.setText( msg )
          self._msgDialog.exec()
      fw.done.connect( errorDialog )
      fw.start()

  def updateData(self):
    d = self._reader.getData()
    if d is None:
      return
    if ( self._trgArm == "Off" ):
      d.put()
      return
    if not self._data is None:
      self._data.put()
    self._data = d
    hdr        = d.getHdr()
    for i in range( self._numCh ):
      self._ch[i].setSamples( d.getCurv( i ) )
      ovrRng = ( ( hdr & (1<<i) ) != 0 )
      self._ov[i].setVisible( ovrRng )
      self._fw.ledSet( 'OVR{}'.format( self._channelNames[i] ), ovrRng )
      self._meanLbls[i].setText("{:>7.2f}".format( d._mean[i] ))
      self._stdLbls[i].setText ("{:>7.2f}".format( d._std[i]  ))
    self._fw.ledSet('Trig', 1)
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

class FileWriter(QtCore.QThread):

  done = QtCore.pyqtSignal(str)

  def __init__(self, buf, fnam, *args, **kwargs):
    super().__init__(*args, **kwargs)
    # keep a reference until the thread is started
    buf.incRef()
    self._mgr  = BufMgr( buf )
    self._fnam = fnam

  def run(self):
    err = ""
    with self._mgr as buf:
      # release the reference we were handed by the thread creator
      buf.decRef()
      try:
        with io.open( self._fnam, "w" ) as f:
          m = buf.getMem()
          for i in range( 1, len( m[0] ), 2 ):
            print("{:10g}, {:10g}".format(m[0][i], m[1][i]), file=f)
      except Exception as e:
        err = "ERROR: File could not be written: " + e.args[0]
    self.done.emit( err )

class Reader(QtCore.QThread):

  def __init__(self, scope, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self._lck            = Lock()
    self._bufAvail       = Condition( Lock() )
    self._readDone       = Semaphore( 0 )
    self._scope          = scope
    self._fw             = self._scope.getFw()
    sz                   = self._fw.getBufSize()
    self._bufs           = [ Buf(self, sz) for i in range(3) ]
    self._rbuf           = [ self._fw.mkBuf(), self._fw.mkBuf() ]
    self._bufhdr         = [0, 0]
    self._ridx           = 0
    # read time for 2x16k = 32k samples is ~50ms
    self._pollInterval   = 0.10
    self._npts           = 0
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
          self._processedBuf.put()
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
      b = self._bufs.pop(0)
      b.incRef()
      return b

  def putBuf(self, b):
    with self._bufAvail:
      if ( b.decRef() > 0 ):
        return
      self._bufs.append(b)
      self._bufAvail.notify()

  def setParms(self, npts = -1, scalX = -1.0):
    with self._lck:
      if ( npts >= 0 ):
        self._npts = npts
      if ( scalX >  0.0 ):
        self._scal = scalX

class ScopeThread(QtCore.QThread):

  def run(self):
    self.exec()

def usage(nm):
  print("usage: {} [-d <usb_device_name] [-hs]".format(nm))

if __name__ == "__main__":

  devn  = '/dev/ttyUSB0'
  styl  = None
  isSim = False

  ( opts, args ) = getopt.getopt( sys.argv[1:], "hd:S:s", [] )
  for opt in opts:
    if   ( opt[0] == '-d' ):
      devn = opt[1]
    elif ( opt[0] == '-h' ):
      usage( sys.argv[0] )
      sys.exit(0)
    elif ( opt[0] == '-S' ):
      styl = opt[1]
    elif ( opt[0] == '-s' ):
      isSim = True

  app = QtWidgets.QApplication( args )
  if ( not styl is None ):
    with open(styl, "r") as f:
      app.setStyleSheet( f.read() )
  scp = Scope( devn, isSim )
  scp.show()
  if ( sys.flags.interactive ):
    scpThread = ScopeThread()
    scpThread.start()
    com       = scp.getFw()
    print("Interactive mode; Firmware handle is 'com', Scope handle is 'scp'")
  else:
    app.exec()
