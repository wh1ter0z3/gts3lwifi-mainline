#!/bin/sh
# Builds the installer zip's tools/xz: XZ Utils, static for aarch64, so it
# runs in any recovery. Unlike busybox's xzcat and the recoveries' xz 5.2, it
# unpacks a multi-block .xz (nura-root.img.xz) on all cores (-T0): about 75
# MB/s on the Tab S3 against 14 MB/s, and the eMMC becomes the limit.
# Cross-compiled in a Debian container (podman or docker).
# usage: mkxz.sh <out file>
set -eu
[ $# -eq 1 ] || { sed -n '2,7p' "$0" >&2; exit 1; }
V=5.8.1
SUM=507825b599356c10dca1cd720c9d0d0c9d5400b9de300af00e4d1ea150795543
OUT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
curl -sSL -o "$W/xz-$V.tar.gz" "https://github.com/tukaani-project/xz/releases/download/v$V/xz-$V.tar.gz"
echo "$SUM  $W/xz-$V.tar.gz" | sha256sum -c --quiet
P=$(command -v podman || command -v docker)
"$P" run --rm -v "$W:/w:Z" docker.io/library/debian:trixie sh -c "
	set -e
	apt-get update -qq > /dev/null 2>&1
	apt-get install -y -qq gcc-aarch64-linux-gnu libc6-dev-arm64-cross make > /dev/null 2>&1
	cd /tmp && tar --no-same-owner --no-same-permissions -xzf /w/xz-$V.tar.gz && cd xz-$V
	./configure -q --host=aarch64-linux-gnu --disable-shared --enable-static --disable-nls --disable-doc \
		--disable-scripts --disable-lzmadec --disable-lzmainfo --disable-xzdec CFLAGS=-O2 > /dev/null
	make -s -j\$(nproc) -C src/liblzma > /dev/null
	# libtool's way to link everything statically
	make -s -j\$(nproc) -C src/xz LDFLAGS=-all-static > /dev/null
	aarch64-linux-gnu-strip -o /w/xz src/xz/xz"
cp "$W/xz" "$OUT"
file "$OUT" 2> /dev/null || ls -l "$OUT"
