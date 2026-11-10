#!/bin/sh
# Builds the vendor HALs (health-mainline, lights-mainline, seh-stubs) with the
# Android NDK. They use libbinder_ndk's platform (LLNDK) API, which the NDK's
# stub library lacks, so link against a stub generated from binder-syms.txt
# (symbol + version node as Android 13's libbinder_ndk exports them); the real
# library comes from the system image at run time.
# usage: NDK=<android-ndk dir> sh build.sh
set -eu
NDK=${NDK:-$HOME/Tools/android-ndk-r30}
CC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang"
cd "$(dirname "$0")"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

# the stub: every symbol a function, each in its version node
for v in $(awk '{print $2}' binder-syms.txt | sort -u); do
	printf '%s {\n  global:\n' "$v"
	awk -v v="$v" '$2 == v {print "    " $1 ";"}' binder-syms.txt
	printf '};\n'
done > "$T/binder.map"
awk '{print "void " $1 "(void) {}"}' binder-syms.txt > "$T/binder.c"
"$CC" -shared -nostdlib -Wl,--version-script="$T/binder.map" -Wl,-soname,libbinder_ndk.so \
	-o "$T/libbinder_ndk.so" "$T/binder.c"

for p in health-mainline lights-mainline seh-stubs; do
	"$CC" -O2 -Wall -Wextra -std=gnu11 -o "$p" "$p.c" -L"$T" -lbinder_ndk -llog
	echo "built $p"
done
