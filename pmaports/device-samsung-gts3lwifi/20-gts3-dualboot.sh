#!/bin/sh
# postmarketOS initramfs hook (mkinitfs hooks/) for the Galaxy Tab S3 layouts
# (docs/partitions.md): map the partitions of our layout's GPT when the boot
# partition (pmos_boot_uuid=) isn't there yet.
#
# The layout's GPT is at the start of Samsung's system partition and spans
# system, cache, hidden and userdata (contiguous, to the end of the eMMC):
# Samsung's own table can't be changed outside Download mode. With
# LineageOS (or an older install) it is at the start of userdata instead.
# The initramfs' own mount_subpartitions() knows neither. With the
# partitions mapped here, the root and boot partitions are found by UUID as
# usual.
#
# Each partition becomes a device-mapper linear device (/dev/mapper/gts3-<GPT
# name>) on Samsung's partitions under it, one segment per partition it
# spans (not on the whole eMMC: that would claim the whole eMMC, and no
# other partition, efs for one, could be mounted any more). Unlike a loop
# device, it passes the eMMC's inline encryption engine through, so fscrypt
# with inlinecrypt encrypts in hardware (docs/encryption.md). A read-only loop device on the eMMC, from
# where the GPT starts, is attached only for the kernel to read the GPT,
# then detached. If dm-linear isn't possible, the partitions stay on that
# loop device.
#
# Only for our layout (a partition named gts3_boot): a plain postmarketOS
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

# an eMMC partition by its name
pdev() {
	for u in /sys/class/block/*/uevent; do
		grep -qx "PARTNAME=$1" "$u" && { echo "/dev/$(sed -n 's/^DEVNAME=//p' "$u")"; return 0; }
	done
	return 1
}
# a GPT header at the start of a partition
gpt_at() { [ "$(dd if="$1" bs=8 skip=64 count=1 2>/dev/null)" = "EFI PART" ]; }
part=$(pdev system) && gpt_at "$part" || part=$(pdev userdata) || exit 0
[ -b "$part" ] || exit 0
sp=/sys/class/block/${part#/dev/}
dev=/dev/$(basename "$(dirname "$(readlink -f "$sp")")")
base=$(cat "$sp/start")
[ -b "$dev" ] && [ -n "$base" ] || exit 0
[ ! -e /dev/mapper/gts3-gts3_boot ] || exit 0

# Samsung's partitions the layout spans, in order: <node>:<start>:<size>
if [ "$part" = "$(pdev userdata)" ]; then spans=userdata; else spans="system cache hidden userdata"; fi
segs=
for n in $spans; do
	d=$(pdev $n) || exit 0
	segs="$segs $d:$(cat /sys/class/block/${d#/dev/}/start):$(cat /sys/class/block/${d#/dev/}/size)"
done
# the dm-linear table for <eMMC sector> <sectors> over those partitions
table() {
	a=$1 left=$2 cur=0
	for g in $segs; do
		d=${g%%:*} r=${g#*:}
		ps=${r%%:*} pe=$((${r%%:*} + ${r#*:}))
		[ "$left" -gt 0 ] || break
		[ "$a" -lt "$pe" ] || continue
		[ "$a" -ge "$ps" ] || return 1
		n=$((pe - a))
		[ "$n" -le "$left" ] || n=$left
		echo "$cur $n linear $d $((a - ps))"
		cur=$((cur + n)) a=$((a + n)) left=$((left - n))
	done
	[ "$left" = 0 ]
}

# a loop device on the eMMC from the GPT on, with its partitions; $1: extra
# losetup options
attach() {
	loop=$(losetup --show -Pf -o $((base * 512)) $1 "$dev") || return 1
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
		if t=$(table $((base + $(cat "$p/start"))) "$(cat "$p/size")") &&
			printf '%s\n' "$t" | dmsetup create "gts3-$name"; then
			made="$made gts3-$name"
		else
			ok=
		fi
	done
	losetup -d "$loop"
	command -v mdev > /dev/null && mdev -s
	if [ -n "$ok" ] && wait_boot; then
		echo "gts3-dualboot: ${part#/dev/} layout mapped with dm-linear:$made" > /dev/kmsg
		exit 0
	fi
	for m in $made; do
		dmsetup remove "$m"
	done
	echo "gts3-dualboot: dm-linear failed, using a loop device" > /dev/kmsg
fi

# fallback: the partitions on a loop device
attach --direct-io=on || exit 0
wait_boot && { echo "gts3-dualboot: ${part#/dev/} layout attached as $loop" > /dev/kmsg; exit 0; }
echo "gts3-dualboot: no boot partition $boot_uuid in the ${part#/dev/} layout" > /dev/kmsg
losetup -d "$loop"
