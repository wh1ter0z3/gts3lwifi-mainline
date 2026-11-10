#!/bin/sh
# Builds gralloc.gts3.so (-> <dir>/odm/lib64/hw/) and yuvtest (a YUV EGLImage
# import check: adb push, run from adb shell) with the Android NDK.
# usage: NDK=<android-ndk dir> MESA=<mesa source dir> sh build.sh
set -eu
NDK=${NDK:-$HOME/Tools/android-ndk-r30}
MESA=${MESA:?MESA=<mesa source dir>, for its include/android_stub headers}
CC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android33-clang"
cd "$(dirname "$0")"
"$CC" -O2 -Wall -Wextra -fPIC -shared -fvisibility=hidden -I"$MESA/include/android_stub" \
	-o gralloc.gts3.so gralloc_cros_info.c -llog
"$CC" -O2 -Wall -o yuvtest yuvtest.c -lEGL -lGLESv2 -landroid -lnativewindow
echo "built gralloc.gts3.so yuvtest"
