#!/bin/sh
# Assembles the odm files for Samsung's fingerprint HAL (see
# odm/etc/init/fingerprint-samsung.rc) into $OUT (default ./out/odm): our
# libQSEEComAPI (NDK), links to the stock Android 9 libraries on the tablet's
# system partition (nothing of Samsung's in here).
# usage: NDK=<ndk> [OUT=dir] sh build.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/out/odm}
ST=/vendor/firmware/.part/system
rm -rf "${OUT:?}" && mkdir -p "$OUT/lib64/fp" "$OUT/lib64/hw"
cp -R "$HERE/odm/." "$OUT/"
"$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang" -O2 -Wall -fPIC \
	-shared -s -Wl,-soname,libQSEEComAPI.so -o "$OUT/lib64/fp/libQSEEComAPI.so" \
	"$HERE/../qseecom/QSEEComAPI.c" -llog
# what the service and fingerprint.default.so need (their NEEDED closure)
for l in lib64/android.hardware.biometrics.fingerprint@2.1.so lib64/libbinder.so \
	lib64/liblog.so lib64/libvndksupport.so \
	lib64/vendor.samsung.hardware.biometrics.fingerprint@2.1.so \
	lib64/vndk-sp-28/libbase.so lib64/vndk-sp-28/libc++.so lib64/vndk-sp-28/libcutils.so \
	lib64/vndk-sp-28/libhardware.so lib64/vndk-sp-28/libhidlbase.so \
	lib64/vndk-sp-28/libhidltransport.so lib64/vndk-sp-28/libhwbinder.so \
	lib64/vndk-sp-28/libutils.so \
	vendor/lib64/libbauthserver.so vendor/lib64/libbauthtzcommon.so vendor/lib64/libcrypto.so \
	vendor/lib64/libegis_fp_normal_sensor_test.so vendor/lib64/libgf_in_system_lib.so \
	vendor/lib64/libqfp_sensortest.so vendor/lib64/libsynaFpSensorTestNwd.so; do
	ln -s "$ST/$l" "$OUT/lib64/fp/${l##*/}"
done
ln -s "$ST/vendor/lib64/hw/fingerprint.default.so" "$OUT/lib64/hw/fingerprint.default.so"
chmod -R go-w "$OUT"
echo "built $OUT"
