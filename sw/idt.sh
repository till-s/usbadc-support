#!/usr/bin/env bash
# output divider := 14 (14<<4)
if [ -z "${BB:+set}" ] ; then
  BB=./bbcli
fi
# output divider
${BB} -I 0x2d 0x00
${BB} -I 0x2e 0xe0
${BB} -I 0x3d 0x00
${BB} -I 0x3e 0xe0
${BB} -I 0x5d 0x00
${BB} -I 0x5e 0xe0
# output divider control; disengage resetb, enable FOD
${BB} -I 0x21 0x81
${BB} -I 0x31 0x81
${BB} -I 0x51 0x81
# output control; CMOS 1.8V, fast slew rate
${BB} -I 0x60 0xa3
${BB} -I 0x62 0xa3
${BB} -I 0x66 0xa3
# output control; enable output
${BB} -I 0x61 0x01
${BB} -I 0x63 0x01
${BB} -I 0x67 0x01
