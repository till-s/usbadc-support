#LB-MIT
#
# MIT License
#
# Copyright (c) 2026 Till Straumann
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
#LE-MIT

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
