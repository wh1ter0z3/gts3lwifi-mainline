#!/bin/sh
# postmarketOS initramfs hook (mkinitfs hooks/) for the Galaxy Tab S3 dual
# boot: attach the userdata partition with its GPT's partitions when the boot
# partition (pmos_boot_uuid=) isn't there yet.
#
# The initramfs' own mount_subpartitions() only takes a userdata partition
# whose table has exactly two partitions (postmarketOS's boot and root), and
# only while the root partition hasn't been found. The dual boot layout has
# more (gts3_boot, android_*, nura). With userdata attached here, the root
# and boot partitions are found by UUID as usual.
#
# Only for that layout (a partition named gts3_boot): a plain postmarketOS
# install on userdata is left to mount_subpartitions(), which also grows its
# root partition on the first boot.

boot_uuid=$(sed -n 's/.*pmos_boot_uuid=\([^ ]*\).*/\1/p' /proc/cmdline)
[ -n "$boot_uuid" ] || exit 0

# the same lookup the initramfs does (find_partition), without the cache
have_boot() { [ -n "$(blkid -c /dev/null --uuid "$boot_uuid" 2>/dev/null)" ]; }
have_boot && exit 0

dev=
for u in /sys/class/block/*/uevent; do
	grep -qx "PARTNAME=userdata" "$u" && dev=/dev/$(sed -n 's/^DEVNAME=//p' "$u") && break
done
[ -b "$dev" ] || exit 0
[ -z "$(losetup -j "$dev")" ] || exit 0

loop=$(losetup --show -Pf --direct-io=on "$dev") || exit 0
ours=
for i in 1 2 3 4 5 6 7 8 9 10; do
	grep -qsx "PARTNAME=gts3_boot" /sys/class/block/"${loop#/dev/}"p*/uevent && { ours=1; break; }
	sleep 0.2
done
[ -n "$ours" ] || { losetup -d "$loop"; exit 0; }
for i in 1 2 3 4 5 6 7 8 9 10; do
	have_boot && { echo "gts3-dualboot: $dev attached as $loop" > /dev/kmsg; exit 0; }
	sleep 0.2
done
echo "gts3-dualboot: no boot partition $boot_uuid on $dev" > /dev/kmsg
losetup -d "$loop"
