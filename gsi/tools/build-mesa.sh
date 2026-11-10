#!/bin/sh
# Builds Mesa's EGL/GLES for the vendor with the Android NDK: freedreno only,
# Android 13 API (the GloDroid vendor's), libdrm static, libc++ static.
# usage: build-mesa.sh <mesa source dir> <build dir> <out dir>
# then: <gsi dir>/vendor-bind/lib64/egl/ <- <out dir>/*.so, and a line
# "lib64/egl" in <gsi dir>/vendor-bind/BIND; plus gralloc-shim/ in
# <gsi dir>/odm/lib64/hw/ (else Mesa can't import YUV buffers).
# Tested with Mesa 26.2.6 (archive.mesa3d.org, signed by the release key).
# needs: meson, ninja, python3-mako, bison, flex; libdrm comes as a subproject.
set -eu
SRC=$(cd "${1:?mesa source}" && pwd); B=${2:?build dir}; OUT=${3:?out dir}
NDK=${NDK:-$HOME/Tools/android-ndk-r30}
T=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
mkdir -p "$B" "$OUT"; B=$(cd "$B" && pwd)
cat > "$B.cross.ini" <<EOC
[binaries]
ar = '$T/llvm-ar'
c = ['$T/aarch64-linux-android33-clang']
cpp = ['$T/aarch64-linux-android33-clang++', '-fno-exceptions', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '--start-no-unused-arguments', '-static-libstdc++', '--end-no-unused-arguments']
c_ld = 'lld'
cpp_ld = 'lld'
strip = '$T/llvm-strip'

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
EOC
cd "$SRC"
[ -f "$B/build.ninja" ] || meson setup "$B" --cross-file "$B.cross.ini" --buildtype=release -Db_ndebug=true \
	-Dplatforms=android -Dplatform-sdk-version=33 -Dandroid-stub=true \
	-Dandroid-libbacktrace=disabled -Dandroid-libperfetto=disabled \
	-Dallow-fallback-for=libdrm -Dlibdrm:default_library=static \
	-Dgallium-drivers=freedreno -Dfreedreno-kmds=msm -Dvulkan-drivers= \
	-Degl=enabled -Dgles1=enabled -Dgles2=enabled -Dopengl=false \
	-Degl-lib-suffix=_mesa -Dgles-lib-suffix=_mesa \
	-Dglx=disabled -Dgbm=disabled -Dllvm=disabled -Dxmlconfig=disabled -Dexpat=disabled \
	-Dzstd=disabled -Dshader-cache=disabled -Dvalgrind=disabled -Dlibunwind=disabled \
	-Dlmsensors=disabled -Dbuild-tests=false -Dtools= -Dperfetto=false
# two jobs, low priority: the build machine has 7 GB
nice -n 19 ninja -C "$B" -j2
for f in src/egl/libEGL_mesa.so src/mesa/glapi/es1api/libGLESv1_CM_mesa.so \
	 src/mesa/glapi/es2api/libGLESv2_mesa.so src/gallium/targets/dri/libgallium_dri.so; do
	"$T/llvm-strip" --strip-unneeded -o "$OUT/$(basename $f)" "$B/$f"
done
ls -l "$OUT"
