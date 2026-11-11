#!/bin/sh
# Builds gts3-bdaddr (static) with the Android NDK; it goes to the vendor
# image's /vendor/bin.
# usage: NDK=<android-ndk dir> sh build.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
"$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android33-clang" -O2 -Wall -Wextra \
	-static -s -o "$HERE/gts3-bdaddr" "$HERE/gts3_bdaddr.c"
echo "built gts3-bdaddr"
