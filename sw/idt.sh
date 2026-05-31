#!/usr/bin/env bash
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

# output divider := 14 (14<<4)
if [ -z "${BB:+set}" ] ; then
  BB=./bbcli
fi
# output divider
${BB} -I 0x2d 0xff
${BB} -I 0x2e 0xf0
${BB} -I 0x3d 0x00
${BB} -I 0x3e 0xe0
${BB} -I 0x5d 0x00
${BB} -I 0x5e 0xe0
# output divider control; disengage resetb, enable FOD
${BB} -I 0x21 0x81
${BB} -I 0x31 0x81
${BB} -I 0x51 0x81
# output control; CMOS 1.8V, fast slew rate
${BB} -I 0x60 0xa0
${BB} -I 0x62 0xa3
${BB} -I 0x66 0xa3
# output control; enable output
${BB} -I 0x61 0x01
${BB} -I 0x63 0x01
${BB} -I 0x67 0x01
