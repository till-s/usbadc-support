from PyQt5 import QtCore,QtGui,QtWidgets

class MessageDialog(QtWidgets.QDialog):

  def __init__(self, parent, title = None):
    super().__init__( parent )
    if ( not title is None ):
      self.setWindowTitle( title )
    self._buttonBox = QtWidgets.QDialogButtonBox( QtWidgets.QDialogButtonBox.Ok )
    self._buttonBox.accepted.connect( self.accept )
    self._layout    = QtWidgets.QVBoxLayout()
    self._lbl       = QtWidgets.QLabel( "" )
    self._layout.addWidget( self._lbl )
    self._layout.addWidget( self._buttonBox )
    self.setLayout( self._layout )

  def setText(self, msg):
    self._lbl.setText( msg )
