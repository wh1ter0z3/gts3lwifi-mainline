#!/bin/sh
# Copyright (C) 2026 The gts3lwifi-mainline project
# SPDX-License-Identifier: Apache-2.0
#
# A slow live view of the recovery's screen on the PC, over adb: there is no
# scrcpy in a recovery (no Android framework to stream from), so this takes a
# screenshot with the recovery's fb2png about once a second and pulls it.
# Open the file in an image viewer that reloads changed files.
# usage: screen.sh [out.png]   (default: ./fox-screen.png)
set -eu
OUT=${1:-fox-screen.png}
adb wait-for-recovery
echo "writing $OUT once a second; Ctrl-C to stop"
while :; do
	if adb shell fb2png /tmp/fox-screen.png >/dev/null 2>&1 &&
	   adb pull /tmp/fox-screen.png "$OUT.tmp" >/dev/null 2>&1; then
		mv "$OUT.tmp" "$OUT"
	fi
	sleep 1
done
