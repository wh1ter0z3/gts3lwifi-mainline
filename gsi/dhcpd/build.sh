#!/bin/sh
# Builds gsi-dhcpd (static) with the Android NDK; mkgsiboot.sh puts it in the
# boot ramdisk.
# usage: NDK=<android-ndk dir> sh build.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
"$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android33-clang" -O2 -Wall -Wextra \
	-static -s -o "$HERE/gsi-dhcpd" "$HERE/gsi_dhcpd.c"
echo "built gsi-dhcpd"
