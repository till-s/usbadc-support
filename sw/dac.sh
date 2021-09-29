#!/usr/bin/env bash
if [ -z "${BB:+set}" ] ; then
  BB=./bbcli
fi
# reset
${BB} -D -- -1
# select internal bandgap (leave at gain 1)
${BB} -D 8 0x5
