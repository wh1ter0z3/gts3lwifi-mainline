#!/bin/sh
# Builds libc2plugin_store.so (the app-side shim, see c2plugin_shim.cpp) with
# the Android NDK.
# usage: NDK=<android-ndk dir> sh build.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
CXX=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android33-clang++
"$CXX" -std=c++17 -O2 -Wall -Wextra -fPIC -shared -fvisibility=hidden \
	-static-libstdc++ -Wl,-soname,libc2plugin_store.so -Wl,-z,max-page-size=16384 \
	-o "$HERE/libc2plugin_store.so" "$HERE/c2plugin_shim.cpp"
echo "built libc2plugin_store.so"
