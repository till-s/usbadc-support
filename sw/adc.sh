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

# set common-mode voltage (also important for PGA output)
#
# ADC: common mode input voltage range 0.4..1.4V
# ADC: controls common mode voltage of PGA
# PGA: output common mode voltage: 2*OCM
# Resistive divider 232/(232+178)
#
# PGA VOCM = 2*ADC_VCM
#
# Valid range for PGA: 2..3V (2.5V best)
#
# Common-mode register 8:
#   bit 6..4, 2..0:
#         000       -> 0.9 V
#         001       -> 1.05V
#         010       -> 1.2V
#
# With 1.2V -> VOCM of PGA becomes 2.4V   (near optimum)
#           -> VICM of ADC becomes 1.358V (close to max)
# With 1.05 -> VOCM of PGA becomes 2.1V   (close to min)
#           -> VICM of ADC becomes 1.188V (OK)
#
${BB} -A 8 0x11

# Signal levels: ADC 1.5Vpp
# Resistive attenuation -10dB (-5dB min w/o diff. 232 resistor)
# PGA min gain:         +6dB
# Front-end gain:       +15.6dB
# Input attenuator      -6dB
# -> input sensitivity 0.8Vpp
# -> max gain          0.08Vpp

#!!! OPA 859 -> output 2.4Vpp !!
#!!! PGA input 2Vpp max!!

# if we use test mode (0xaa/0x55) then
# we MUST engage offset-binary mode

#${BB} -A 6 0xd0
