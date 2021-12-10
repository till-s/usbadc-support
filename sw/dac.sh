#!/usr/bin/env bash
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
