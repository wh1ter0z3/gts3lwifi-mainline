#!/bin/sh
# Installs OneUI (a GSI's system.img) on a Galaxy Tab S3, SM-T820 or SM-T825,
# that runs lk2nd: builds the userdata partition's image on this PC (the
# system image, our vendor image and board files, an empty /data) and
# flashes it, with the boot image for the model lk2nd reports. The tablet's
# own firmware stays on its partitions; nothing from Samsung is in this
# package, the system image comes from the GSI you downloaded.
#
# usage: install-oneui.sh <GSI zip or system.img>
#   with the tablet in lk2nd's fastboot mode, connected over USB
# Needs: fastboot, mke2fs (e2fsprogs 1.43 or newer), unzip, about 15 GB free
# where it works (WORK=<dir>, default: next to this script). img2simg
# (Debian/Ubuntu: android-sdk-libsparse-utils; Arch: android-tools) makes
# flashing faster; without it fastboot converts the image itself.
# ERASES the userdata partition: Android data, a postmarketOS install on it.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${1:-}
[ -n "$SRC" ] && [ -f "$SRC" ] || { echo "usage: $0 <GSI zip or system.img>" >&2; exit 1; }
WORK=${WORK:-$HERE/work}
PATH=$PATH:/usr/sbin:/sbin
MiB=1048576

die() { echo "error: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "$1 not found ($2)"; }
need fastboot "Android platform tools"
need mke2fs "e2fsprogs"
need unzip "unzip"
v=$(mke2fs -V 2>&1 | sed -n 's/^mke2fs \([0-9]*\)\.\([0-9]*\).*/\1 \2/p')
set -- $v
[ "${1:-0}" -gt 1 ] || [ "${2:-0}" -ge 43 ] || die "mke2fs 1.43 or newer needed (has ${1:-?}.${2:-?})"

cd "$HERE"
if [ -f SHA256SUMS ]; then
	echo "Checking the package..."
	sha256sum -c --quiet SHA256SUMS || die "package files damaged, download it again"
fi

echo "Waiting for the tablet in lk2nd's fastboot mode..."
until fastboot devices 2>/dev/null | grep -q .; do sleep 1; done
gv() { fastboot getvar "$1" 2>&1 | sed -n "s/^$1:[[:space:]]*//p" | head -n 1; }
COMPAT=$(gv lk2nd:compatible)
case "$COMPAT" in
samsung,gts3lwifi) MODEL=SM-T820; BOOT=boot-gts3lwifi.img ;;
samsung,gts3llte) MODEL=SM-T825; BOOT=boot-gts3llte.img ;;
*) die "not a Galaxy Tab S3 in lk2nd (lk2nd:compatible: '${COMPAT:-none}')" ;;
esac
[ -f "$BOOT" ] || die "$BOOT missing from the package"
SIZE=$(gv partition-size:userdata)
SIZE=$(printf '%d' "$SIZE" 2>/dev/null) || die "no userdata partition size from lk2nd"
[ "$SIZE" -gt $((16 * 1024 * MiB)) ] || die "userdata partition too small ($SIZE bytes)"
echo "Tablet: $MODEL, userdata partition $((SIZE / MiB)) MiB"

free=$(mkdir -p "$WORK" && df -Pk "$WORK" | awk 'NR == 2 {print $4}')
[ "$free" -gt $((15 * 1024 * 1024)) ] || die "less than 15 GB free in $WORK"
rm -rf "${WORK:?}/root" "$WORK/store.img" "$WORK/store.simg"
R=$WORK/root/android
mkdir -p "$R"

echo "Taking the system image from $SRC..."
case "$SRC" in
*.zip)
	n=$(unzip -Z1 "$SRC" | grep -E '(^|/)system\.img$' | head -n 1)
	[ -n "$n" ] || die "no system.img in $SRC"
	unzip -p "$SRC" "$n" > "$R/system.img" ;;
*.xz) need xz "xz-utils"; xz -dc "$SRC" > "$R/system.img" ;;
*) cp "$SRC" "$R/system.img" ;;
esac
# An Android sparse image (magic ed26ff3a) is unpacked first
if [ "$(od -An -tx4 -N4 "$R/system.img" | tr -d ' ')" = ed26ff3a ]; then
	need simg2img "the same package as img2simg"
	simg2img "$R/system.img" "$R/system.raw" && mv "$R/system.raw" "$R/system.img"
fi
# ext4 (magic ef53 at 1080) or EROFS (e0f5e1e2 at 1024)
m1=$(od -An -tx2 -j1080 -N2 "$R/system.img" | tr -d ' ')
m2=$(od -An -tx4 -j1024 -N4 "$R/system.img" | tr -d ' ')
[ "$m1" = ef53 ] || [ "$m2" = e0f5e1e2 ] || die "system.img is neither ext4 nor EROFS"

echo "Adding the vendor image and board files..."
cp vendor.img "$R/"
for d in odm system-add vendor-bind; do [ -d "$d" ] && cp -R "$d" "$R/"; done
[ -f system-hide ] && cp system-hide "$R/"
chmod -R u+rwX,go+rX,go-w "$WORK/root"

# /data: an ext4 image that may grow to what the partition has left, less a
# margin for the partition's own file system
used=$(du -sb "$WORK/root" | cut -f1)
DATA=$(( (SIZE - used - SIZE / 100 - 512 * MiB) / MiB * MiB ))
[ "$DATA" -gt $((4 * 1024 * MiB)) ] || die "no room for /data ($((DATA / MiB)) MiB)"
truncate -s "$DATA" "$R/userdata.img"
mke2fs -q -F -t ext4 -L data "$R/userdata.img"
truncate -s 16M "$R/log.img"
mke2fs -q -F -t ext4 -L gsilog "$R/log.img"
echo "/data: $((DATA / MiB)) MiB"

echo "Building the userdata partition image..."
truncate -s "$SIZE" "$WORK/store.img"
mke2fs -q -F -t ext4 -L gsistore -m 0 -i $MiB -d "$WORK/root" "$WORK/store.img"
rm -rf "${WORK:?}/root"
# A sparse image skips the unused space; else fastboot sends it as zeros
IMG=$WORK/store.img
if command -v img2simg >/dev/null 2>&1; then
	img2simg "$WORK/store.img" "$WORK/store.simg"
	rm -f "$WORK/store.img"
	IMG=$WORK/store.simg
fi

echo
echo "Ready to flash the $MODEL:"
echo "  boot (after lk2nd): $BOOT"
echo "  userdata: OneUI, $(du -m "$IMG" | cut -f1) MiB of data"
echo "This ERASES the userdata partition (Android data, postmarketOS)."
printf 'Type yes to flash: '
read -r ok
[ "$ok" = yes ] || { echo "Not flashed. The image stays in $IMG"; exit 1; }
fastboot flash boot "$BOOT"
fastboot flash userdata "$IMG"
fastboot reboot
echo "Done. The first start takes about 8 minutes (the Samsung logo, then a"
echo "black screen for a while, then OneUI's lock screen: swipe up)."
