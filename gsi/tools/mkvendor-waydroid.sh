#!/bin/sh
# Waydroid's vendor (ext4, generic arm64) made bootable natively on the Tab S3:
# our init/fstab, Waydroid's props for a pmOS-side compositor, firmware,
# busybox debug shell. Runs as root on the tablet; rebuilds the ext4 image
# (Waydroid's has too few inodes for the firmware tree).
# usage: mkvendor-waydroid.sh <waydroid vendor.img> <overlay> <common overlay> <busybox> <out.img>
set -eu
SRC=$1 OV=$2 COV=$3 BB=$4 OUT=$5
FIRMWARE=${FIRMWARE:-/lib/firmware}
W=$(mktemp -d /var/tmp/mkvw.XXXXXX)
trap 'umount "$W/src" 2>/dev/null || :; rm -rf "$W"' EXIT
mkdir "$W/src" "$W/v"
V=$W/v
mount -o ro,loop "$SRC" "$W/src"
tar -C "$W/src" --xattrs --xattrs-include='*' -cf - . | tar -C "$V" --xattrs --xattrs-include='*' -xpf -
umount "$W/src"
install -m 0644 -o 0 -g 0 "$OV/etc/init/init.gts3.rc" "$V/etc/init/"
install -m 0644 -o 0 -g 0 "$COV/etc/fstab.gts3" "$V/etc/"
install -m 0644 -o 0 -g 0 "$OV/waydroid.prop" "$V/waydroid.prop"
rm -rf "$V/firmware"; mkdir "$V/firmware"
# /lib/firmware, then its updates/ over it (as the kernel searches them)
(cd "$FIRMWARE" && tar --exclude=./updates -chf - .) | tar -C "$V/firmware" -xf -
[ -d "$FIRMWARE/updates" ] && (cd "$FIRMWARE/updates" && tar -chf - .) | tar -C "$V/firmware" -xf -
chown -R 0:0 "$V/firmware"
install -m 0755 -o 0 -g 2000 "$BB" "$V/bin/busybox"
install -m 0755 -o 0 -g 2000 "$COV/bin/gsi-login" "$V/bin/gsi-login"
mkdir -p "$V/bin/bb"
for a in $("$V/bin/busybox" --list); do ln -sf ../busybox "$V/bin/bb/$a"; done
size=$(( $(du -sm "$V" | cut -f1) * 12 / 10 + 32 ))
rm -f "$OUT"
mke2fs -q -t ext4 -b 4096 -N 16384 -L vendor -d "$V" "$OUT" ${size}M
echo "built $OUT (${size} MiB)"
