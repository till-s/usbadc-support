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

if [ -z "${BB:+set}" ] ; then
  BB=./bbcli
fi
# reset
${BB} -D -- -1
# select internal bandgap (leave at gain 1)
# according to datasheet, if we use the internal
# bandgap then
#  - all channels must use it
#  - select on ch0
#  - other channels must be in external, buffered mode
${BB} -D 8   0x000D
#
# Buffer op-amp:
#   Vo 10/15 + Vref 5/15 = Vdac 10/15
# -> Vo = Vdac - Vref/2
# I.e., gain 1 is to be used
#${BB} -D 0xa 0x0000
