#!/bin/sh
# Builds gsi-unzip (static, with the NDK's zlib) for the TWRP installer.
# usage: NDK=<android-ndk dir> sh build.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
"$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang" -O2 -Wall -Wextra \
	-static -s -o "$HERE/gsi-unzip" "$HERE/gsi_unzip.c" -lz
echo "built gsi-unzip"
