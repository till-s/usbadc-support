#!/usr/bin/env bash
#
# Connect ACM device to a listening socket with socat
if [ $# -lt 1 ]; then
	echo "Usage $0 port [ACM_device]"
	echo
	echo "Connect an ACM device to a listening socket"
	exit 1
fi
port=$1
dev="/dev/ttyACM0"
if [ $# -gt 1 ]; then
	dev=$2
fi
socat TCP-LISTEN:${port},nodelay,reuseaddr,fork,backlog=0 GOPEN:"${dev}",raw,echo=0
