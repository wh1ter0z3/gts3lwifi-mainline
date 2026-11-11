#!/bin/sh
# Builds libc2_gpu_usage.so (see c2_gpu_usage.c) with the Android NDK; it goes
# to <dir>/odm/lib64/c2v4l2/, and the c2-v4l2 rc preloads it.
# usage: NDK=<android-ndk dir> sh build.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
"$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android33-clang" -O2 -Wall -Wextra \
	-fPIC -shared -fvisibility=hidden -Wl,-soname,libc2_gpu_usage.so -Wl,-z,max-page-size=16384 \
	-o "$HERE/libc2_gpu_usage.so" "$HERE/c2_gpu_usage.c" -ldl
echo "built libc2_gpu_usage.so"
