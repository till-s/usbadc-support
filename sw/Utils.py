from PyQt5 import QtCore, QtGui, QtWidgets
# Magic factory; creates a subclass of 'clazz'
# (which is expected to be a 'QValidator' subclass)
# and furnishes a 'fixup' and connects to signals
# so that 'setter' may update an associated object
# from the new QLineEdit text. 'getter' is used
# to restore the text from an associated object if
# editing fails or is abandoned.

def createValidator(lineEdit, getter, setter, clazz, *args, **kwargs):

  class TheValidator(clazz):
    def __init__(self, lineEdit, getter, setter, *args, **kwargs):
      super().__init__( *args, **kwargs )
      def mkRestoreVal(w, g):
        def act():
          w.setText( g() )
        return act
      def mkSetVal(w, g, s): 
        def act():
          try:
            s( w.text() )
          except Exception as e:
            w.setText( g() )
        return act
      self._edt = lineEdit
      self._get = getter
      self._set = setter
      if not lineEdit is None:
        self.connect( lineEdit )

    def connect(self, lineEdit):
      def mkRestoreVal(w, g):
        def act():
          w.setText( g() )
        return act
      def mkSetVal(w, g, s): 
        def act():
          try:
            s( w.text() )
          except Exception as e:
            w.setText( g() )
        return act
      self._edt = lineEdit
      self._edt.editingFinished.connect( mkRestoreVal( lineEdit, getter ) )
      self._edt.returnPressed.connect(   mkSetVal(     lineEdit, getter, setter ) )
      self._edt.setValidator( self )
      mkRestoreVal( lineEdit, getter ) ()

    def fixup(self, s):
      return self._get()

  return TheValidator( lineEdit, getter, setter, *args, **kwargs )

# Action which emits itself
class ActAction(QtWidgets.QAction):

  _signal = QtCore.pyqtSignal(QtWidgets.QAction)

  def __init__(self, name, parent=None):
    QtWidgets.QAction.__init__(self, name, parent)
    self.triggered.connect( self )

  def __call__(self):
    self._signal.emit(self)

  def connect(self, slot):
    self._signal.connect( slot )


class MenuButton(QtWidgets.QPushButton):

  def __init__(self, lbls, parent = None):
    super().__init__(parent)
    menu = QtWidgets.QMenu()
    self.setText( lbls[0] )
    # if the first label is also among the
    # following elements then it is the default/initial
    # value
    if lbls[0] in lbls[1:]:
      lbls = lbls[1:]
    for i in lbls:
      a = ActAction(i, self)
      a.connect( self.activated )
      menu.addAction( a )
    self.setMenu( menu )

  def activated(self, act):
    self.setText(act.text())
