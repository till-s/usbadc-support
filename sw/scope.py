#!/usr/bin/env python3
import sys
import pyfwcomm  as fw
from   PyQt5     import QtCore, QtGui, QtWidgets, Qwt
from   Utils     import createValidator, MenuButton

class scope(QtCore.QObject):
  def __init__(self, devnam, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self._fw       = fw.FwComm( devnam )
    if ( not self._fw.isADCDLLLocked() ):
      self._fw.init()
    self._main     = QtWidgets.QMainWindow()
    self._cent     = QtWidgets.QWidget()
    self._main.setCentralWidget( self._cent )
    hlay           = QtWidgets.QHBoxLayout()
    self._plot     = Qwt.QwtPlot()
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
      self._fw.setAcqNPreTriggerSamples( int(s) )
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

  def show(self):
    self._main.show()

if __name__ == "__main__":
  app = QtWidgets.QApplication( sys.argv )
  scp = scope("/dev/ttyUSB0")
  scp.show()
  app.exec()
