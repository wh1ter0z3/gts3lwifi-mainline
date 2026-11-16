#!/bin/sh
# Copyright (C) 2026 The gts3lwifi-mainline project
# SPDX-License-Identifier: Apache-2.0
#
# Applies the vendor-side patches to an AOSP tree (run from its top, or give
# the top as $1). --check: only test that they apply. Each patch goes to one
# project; a patch already applied is skipped.
set -eu
CHECK=
[ "${1:-}" = --check ] && { CHECK=1; shift; }
TOP=$(cd "${1:-${ANDROID_BUILD_TOP:-.}}" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
while read -r p dir; do
	[ -n "$p" ] || continue
	d="$TOP/$dir"
	[ -d "$d" ] || { echo "$dir: not in $TOP" >&2; exit 1; }
	if git -C "$d" apply --reverse --check "$HERE/$p" 2>/dev/null; then
		echo "$p: already applied"
	elif [ -n "$CHECK" ]; then
		git -C "$d" apply --check "$HERE/$p" && echo "$p: applies"
	else
		git -C "$d" apply "$HERE/$p" && echo "$p: applied to $dir"
	fi
done <<'LIST'
03-hardware-interfaces-audio.patch hardware/interfaces
04-hardware-interfaces-bluetooth.patch hardware/interfaces
05-external-minigbm.patch external/minigbm
09-system-media.patch system/media
14-external-libdrm.patch external/libdrm
16-external-tinyalsa.patch external/tinyalsa
LIST
