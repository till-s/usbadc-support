#!/usr/bin/env bash

if [ -z "${BB:+set}" ] ; then
  BB=./bbcli
fi

if ! [ "0x8003" = `./bbcli -A 0 | awk '{print $3}'` ] ; then
  echo "ADC readback test failed -- is the PLL initialized (does the ADC have a clock) ?"
  exit 1
fi

# set muxed-mode on channel B
${BB} -A 1 0x6

# if we use test mode (0xaa/0x55) then
# we MUST engage offset-binary mode

#${BB} -A 6 0xd0
