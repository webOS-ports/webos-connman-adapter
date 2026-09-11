#!/bin/sh
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
# Host-side wrapper: push luna-stress.sh to a LuneOS device over adb and run
# it there.
#
# Usage: adb-stress.sh [-s SERIAL] [iterations]

set -e

SERIAL=""
if [ "$1" = "-s" ]; then
	SERIAL="-s $2"
	shift 2
fi
ITERATIONS=${1:-25}

HERE=$(dirname "$0")

adb $SERIAL push "$HERE/luna-stress.sh" /tmp/luna-stress.sh >/dev/null
adb $SERIAL shell "sh /tmp/luna-stress.sh $ITERATIONS"
