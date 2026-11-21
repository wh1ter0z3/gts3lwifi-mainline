#!/bin/sh
# postmarketOS initramfs hook (mkinitfs hooks/) for the Galaxy Tab S3 dual
# boot: map the partitions of the userdata partition's GPT when the boot
# partition (pmos_boot_uuid=) isn't there yet.
#
# The initramfs' own mount_subpartitions() only takes a userdata partition
# whose table has exactly two partitions (postmarketOS's boot and root), and
# only while the root partition hasn't been found. The dual boot layout has
# more (gts3_boot, android_*, nura). With them mapped here, the root and boot
# partitions are found by UUID as usual.
#
# Each partition becomes a device-mapper linear device (/dev/mapper/gts3-<GPT
# name>) straight on userdata: unlike a loop device, it passes the eMMC's
# inline encryption engine through, so fscrypt with inlinecrypt encrypts in
# hardware (docs/encryption.md). A read-only loop device is attached only for
# the kernel to read the GPT, then detached. If dm-linear isn't possible, the
# partitions stay on a loop device as before.
#
# Only for that layout (a partition named gts3_boot): a plain postmarketOS
# install on userdata is left to mount_subpartitions(), which also grows its
# root partition on the first boot.

boot_uuid=$(sed -n 's/.*pmos_boot_uuid=\([^ ]*\).*/\1/p' /proc/cmdline)
[ -n "$boot_uuid" ] || exit 0

# the same lookup the initramfs does (find_partition), without the cache
have_boot() { [ -n "$(blkid -c /dev/null --uuid "$boot_uuid" 2>/dev/null)" ]; }
wait_boot() {
	for i in 1 2 3 4 5 6 7 8 9 10; do
		have_boot && return 0
		sleep 0.2
	done
	return 1
}
have_boot && exit 0

dev=
for u in /sys/class/block/*/uevent; do
	grep -qx "PARTNAME=userdata" "$u" && dev=/dev/$(sed -n 's/^DEVNAME=//p' "$u") && break
done
[ -b "$dev" ] || exit 0
[ -z "$(losetup -j "$dev")" ] || exit 0
[ ! -e /dev/mapper/gts3-gts3_boot ] || exit 0

# attach with the GPT's partitions; $1: extra losetup options
attach() {
	loop=$(losetup --show -Pf $1 "$dev") || return 1
	for i in 1 2 3 4 5 6 7 8 9 10; do
		grep -qsx "PARTNAME=gts3_boot" /sys/class/block/"${loop#/dev/}"p*/uevent && return 0
		sleep 0.2
	done
	losetup -d "$loop"
	return 1
}

# dm-linear: one device per partition, at the offsets the kernel read
if command -v dmsetup > /dev/null && attach -r; then
	made= ok=1
	for p in /sys/class/block/"${loop#/dev/}"p*; do
		name=$(sed -n 's/^PARTNAME=//p' "$p/uevent")
		[ -n "$name" ] || name=p${p##*p}
		if dmsetup create "gts3-$name" --table "0 $(cat "$p/size") linear $dev $(cat "$p/start")"; then
			made="$made gts3-$name"
		else
			ok=
		fi
	done
	losetup -d "$loop"
	command -v mdev > /dev/null && mdev -s
	if [ -n "$ok" ] && wait_boot; then
		echo "gts3-dualboot: $dev mapped with dm-linear:$made" > /dev/kmsg
		exit 0
	fi
	for m in $made; do
		dmsetup remove "$m"
	done
	echo "gts3-dualboot: dm-linear failed, using a loop device" > /dev/kmsg
fi

# fallback: the partitions on a loop device
attach --direct-io=on || exit 0
wait_boot && { echo "gts3-dualboot: $dev attached as $loop" > /dev/kmsg; exit 0; }
echo "gts3-dualboot: no boot partition $boot_uuid on $dev" > /dev/kmsg
losetup -d "$loop"
