#!/bin/sh
# Installs Nura (postmarketOS) and, optionally, our Android (any GSI on our
# vendor) side by side on a Galaxy Tab S3 that runs lk2nd, from a Linux PC:
# builds the userdata partition's image here with the dual boot layout
# (docs/dual-boot.md), flashes it and lk2nd v2 over lk2nd's fastboot. The
# tablet's own firmware stays on its partitions.
#
# usage: install-dualboot.sh --nura <Nura userdata image> [options]
#   --nura FILE      Nura's userdata image (.simg/.img, may be .zst/.xz):
#                    the release's pmos-...-userdata.simg.zst
#   --gsi FILE       Android: the GSI (system.img, .img.xz/.gz/.zst, or a zip
#                    with system.img inside); with --android, else no Android
#   --android DIR    Android: our files: vendor.img, Image.gz, gts3.dtb,
#                    initramfs (gsi-init with gsi.parts), cmdline
#   --lk2nd FILE     lk2nd v2 image for this model (flashed after userdata)
#   --system SIZE --vendor SIZE --data SIZE   Android's partitions
#                    (default 4G, 1G, 4G); Nura gets the rest
#   --shared SIZE    also a shared exFAT partition (default: none, the
#                    microSD card is the shared storage)
#   --image FILE     only build the image (raw, sparse file) here; for tests
#   --size BYTES     with --image: the userdata size (else asked from lk2nd)
#   --model T820|T825  with --image: the model (else asked from lk2nd)
#   --payload DIR    don't install: write the files twrp/update-binary (the
#                    OrangeFox installer) needs into DIR: nura-boot.tar,
#                    nura-root.simg, nura.env, and android/ from --android
# ERASES the userdata partition: Android data, a postmarketOS install on it.
# Needs: fastboot, e2fsprogs (mke2fs, e2fsck, resize2fs, debugfs), sfdisk,
# simg2img and img2simg (android-sdk-libsparse-utils / android-tools),
# python3, cpio, zstd/xz for compressed images. WORK=<dir> (default: ./work)
# needs room for the Nura image unpacked and the userdata image's data.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
PATH=$PATH:/usr/sbin:/sbin
MiB=1048576

die() { echo "error: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "$1 not found ($2)"; }
say() { echo "== $*"; }

NURA= GSI= ANDROID= LK2ND= IMAGE= SIZE= MODEL= PAYLOAD=
SYSTEM=4G VENDOR=1G DATA=4G SHARED=
while [ $# -gt 0 ]; do
	case "$1" in
	--nura) NURA=$2 ;; --gsi) GSI=$2 ;; --android) ANDROID=$2 ;; --lk2nd) LK2ND=$2 ;;
	--system) SYSTEM=$2 ;; --vendor) VENDOR=$2 ;; --data) DATA=$2 ;; --shared) SHARED=$2 ;;
	--image) IMAGE=$2 ;; --size) SIZE=$2 ;; --model) MODEL=$2 ;; --payload) PAYLOAD=$2 ;;
	*) sed -n '2,29p' "$0" >&2; exit 1 ;;
	esac
	shift 2
done
[ -f "$NURA" ] || die "--nura: no such file '$NURA'"
[ -z "$GSI" ] || [ -n "$ANDROID" ] || die "--gsi needs --android (our vendor and boot files)"
[ -z "$ANDROID" ] || [ -n "$GSI" ] || [ -n "$PAYLOAD" ] || die "--android needs --gsi"
if [ -n "$ANDROID" ]; then
	for f in vendor.img Image.gz gts3.dtb initramfs cmdline; do
		[ -s "$ANDROID/$f" ] || die "--android: no $f in $ANDROID"
	done
fi
for t in mke2fs e2fsck resize2fs debugfs sfdisk blkid simg2img python3 cpio; do need $t "see the usage"; done
LAYOUT=$HERE/gts3-layout
[ -x "$LAYOUT" ] || LAYOUT=$HERE/../dualboot/gts3-layout
[ -f "$LAYOUT" ] || die "gts3-layout not next to this script"
HOOK=$HERE/20-gts3-dualboot.sh
[ -f "$HOOK" ] || HOOK=$HERE/../../pmaports/device-samsung-gts3lwifi/20-gts3-dualboot.sh
[ -f "$HOOK" ] || die "20-gts3-dualboot.sh not found"

# The tablet: model and userdata size, from lk2nd (a payload is for both)
[ -z "$PAYLOAD" ] || { MODEL=${MODEL:-T820}; SIZE=${SIZE:-26226966016}; IMAGE=${IMAGE:-none}; }
if [ -z "$IMAGE" ] || [ -z "$SIZE" ] || [ -z "$MODEL" ]; then
	need fastboot "Android platform tools"
	echo "Waiting for the tablet in lk2nd's fastboot mode..."
	until fastboot devices 2>/dev/null | grep -q .; do sleep 1; done
	gv() { fastboot getvar "$1" 2>&1 | sed -n "s/^$1:[[:space:]]*//p" | head -n 1; }
	case "$(gv lk2nd:compatible)" in
	samsung,gts3lwifi) MODEL=T820 ;;
	samsung,gts3llte) MODEL=T825 ;;
	*) die "not a Galaxy Tab S3 in lk2nd" ;;
	esac
	SIZE=$(printf '%d' "$(gv partition-size:userdata)" 2>/dev/null) || die "no userdata size from lk2nd"
fi
case "$MODEL" in
T820) DTB=apq8096-samsung-gts3lwifi.dtb ;;
T825) DTB=msm8996-samsung-gts3llte.dtb ;;
*) die "--model: T820 or T825" ;;
esac
[ "$SIZE" -gt $((16 * 1024 * MiB)) ] || die "userdata partition too small ($SIZE bytes)"
echo "Tablet: SM-$MODEL, userdata $((SIZE / MiB)) MiB"

WORK=${WORK:-$PWD/work}
mkdir -p "$WORK"
W=$(cd "$WORK" && pwd)
rm -rf "${W:?}/nura" "$W/bootfs"
mkdir -p "$W/nura" "$W/bootfs"

# A file, decompressed and unsparsed into $2
unpack() {
	case "$1" in
	*.zst) need zstd zstd; zstd -dc "$1" ;;
	*.xz) need xz xz-utils; xz -dc "$1" ;;
	*.gz) gzip -dc "$1" ;;
	*) cat "$1" ;;
	esac > "$2.in"
	if [ "$(od -An -tx1 -N4 "$2.in" | tr -d ' ')" = 3aff26ed ]; then
		simg2img "$2.in" "$2" && rm -f "$2.in"
	else
		mv "$2.in" "$2"
	fi
}
# start and size (sectors) of partition $2 of disk image $1
part() { sfdisk -d "$1" | sed -n "s/^[^ ]*[^0-9]$2 : *start= *\([0-9]*\), *size= *\([0-9]*\),.*/\1 \2/p"; }
# a partition of disk image $1 into file $3 / from file $3 (sparse)
part_out() { set -- "$1" "$3" $(part "$1" "$2"); dd if="$1" of="$2" bs=512 skip="$3" count="$4" conv=sparse status=none; }
part_in() { set -- "$1" "$3" $(part "$1" "$2"); dd if="$2" of="$1" bs=512 seek="$3" conv=sparse,notrunc status=none; }

say "Nura: unpacking $(basename "$NURA")"
unpack "$NURA" "$W/nura/disk.img"
N=$W/nura
sfdisk -d "$N/disk.img" > "$N/table" 2> /dev/null || die "no partition table in the Nura image"
part_out "$N/disk.img" 1 "$N/boot.img"
part_out "$N/disk.img" 2 "$N/root.img"
rm -f "$N/disk.img"
BOOT_UUID=$(blkid -o value -s UUID "$N/boot.img")
ROOT_UUID=$(blkid -o value -s UUID "$N/root.img")
[ "$(blkid -o value -s LABEL "$N/boot.img")" = pmOS_boot ] || die "partition 1 of the Nura image isn't pmOS_boot"
[ "$(blkid -o value -s LABEL "$N/root.img")" = pmOS_root ] || die "partition 2 of the Nura image isn't pmOS_root"
echo "   boot $BOOT_UUID, root $ROOT_UUID"

# Nura's boot partition files, and its kernel command line (from the boot
# image pmOS made for lk2nd: deviceinfo's options and the UUIDs)
debugfs -R "rdump / $W/bootfs" "$N/boot.img" 2> /dev/null
B=$W/bootfs
rm -rf "$B/lost+found"
[ -f "$B/vmlinuz" ] && [ -f "$B/initramfs" ] && [ -f "$B/$DTB" ] || die "Nura's boot partition has no vmlinuz, initramfs or $DTB"
NURA_CMDLINE=$(python3 - "$B/boot.img" <<'PY'
import sys
b = open(sys.argv[1], "rb").read(2048)
assert b[:8] == b"ANDROID!", "no Android boot image"
print((b[64:576].split(b"\0")[0] + b[608:1632].split(b"\0")[0]).decode().strip())
PY
) || die "can't read the command line from Nura's boot.img"
echo "$NURA_CMDLINE" | grep -q "pmos_root_uuid=$ROOT_UUID" || die "Nura's boot.img is for another root"
# lk2nd boots extlinux.conf; the boot.img stays for a manual fastboot boot

# The hook that finds Nura's root in the layout (device package r15 has
# it): in its initramfs now, as a second cpio archive, and for mkinitfs
mkdir -p "$W/hook/hooks"
cp "$HOOK" "$W/hook/hooks/20-gts3-dualboot.sh"
(cd "$W/hook" && printf 'hooks\nhooks/20-gts3-dualboot.sh\n' | cpio -o -H newc -R 0:0 --quiet | gzip -9n) >> "$B/initramfs"

# The OrangeFox installer's files: Nura's boot files (hook in the
# initramfs), its root (checked, hook for mkinitfs; grown on the tablet),
# the UUIDs and command line, our Android files
if [ -n "$PAYLOAD" ]; then
	say "payload in $PAYLOAD"
	mkdir -p "$PAYLOAD"
	P=$(cd "$PAYLOAD" && pwd)
	tar -C "$B" --numeric-owner --owner=0 --group=0 -cf "$P/nura-boot.tar" .
	e2fsck -fy "$N/root.img" > /dev/null 2>&1 || [ $? -le 1 ] || die "e2fsck of Nura's root failed"
	# the recoveries' e2fsprogs (1.45) don't know orphan_file (1.47's mke2fs
	# default) and refuse to check or resize the file system with it
	if dumpe2fs -h "$N/root.img" 2> /dev/null | grep -q "features:.*orphan_file"; then
		tune2fs -O ^orphan_file "$N/root.img" > /dev/null || die "can't drop orphan_file from Nura's root"
		e2fsck -fy "$N/root.img" > /dev/null 2>&1 || [ $? -le 1 ] || die "e2fsck of Nura's root failed"
	fi
	debugfs -w -R "mkdir /etc/mkinitfs/hooks" "$N/root.img" > /dev/null 2>&1 || true
	debugfs -w -R "write $HOOK /etc/mkinitfs/hooks/20-gts3-dualboot.sh" "$N/root.img" > /dev/null 2>&1
	for f in "uid 0" "gid 0" "mode 0100644"; do
		debugfs -w -R "sif /etc/mkinitfs/hooks/20-gts3-dualboot.sh $f" "$N/root.img" > /dev/null 2>&1
	done
	need img2simg "android-sdk-libsparse-utils"
	img2simg "$N/root.img" "$P/nura-root.simg"
	{
		echo "# Nura (postmarketOS) image this payload is from: $(basename "$NURA")"
		echo "BOOT_UUID=$BOOT_UUID"
		echo "ROOT_UUID=$ROOT_UUID"
		echo "NURA_CMDLINE='$NURA_CMDLINE'"
	} > "$P/nura.env"
	if [ -n "$ANDROID" ]; then
		mkdir -p "$P/android"
		for f in vendor.img Image.gz gts3.dtb initramfs cmdline; do cp -L "$ANDROID/$f" "$P/android/"; done
	fi
	rm -f "$N/root.img" "$N/boot.img"
	ls -l "$P" "$P/android" 2> /dev/null
	exit 0
fi

# Android's boot files
if [ -n "$ANDROID" ]; then
	mkdir -p "$B/gts3/android"
	cp "$ANDROID/Image.gz" "$ANDROID/gts3.dtb" "$B/gts3/android/"
	cp "$ANDROID/initramfs" "$B/gts3/android/initramfs"
	ANDROID_CMDLINE="$(cat "$ANDROID/cmdline") gsi.parts=1"
fi

mkdir -p "$B/extlinux"
{
	echo "# Galaxy Tab S3 dual boot (docs/dual-boot.md). lk2nd boots the default"
	echo "# label; switch with gts3-boot, or Volume Down at power-on for lk2nd's menu."
	echo "default nura"
	echo
	echo "label nura"
	echo "	linux /vmlinuz"
	echo "	initrd /initramfs"
	echo "	fdt /$DTB"
	echo "	append $NURA_CMDLINE"
	if [ -n "$ANDROID" ]; then
		echo
		echo "label android"
		echo "	linux /gts3/android/Image.gz"
		echo "	initrd /gts3/android/initramfs"
		echo "	fdt /gts3/android/gts3.dtb"
		echo "	append $ANDROID_CMDLINE"
	fi
} > "$B/extlinux/extlinux.conf"

# Android's system image
if [ -n "$ANDROID" ]; then
	say "Android: the system image from $(basename "$GSI")"
	case "$GSI" in
	*.zip)
		n=$(unzip -Z1 "$GSI" | grep -E '(^|/)system\.img$' | head -n 1)
		[ -n "$n" ] || die "no system.img in $GSI"
		unzip -p "$GSI" "$n" > "$W/system.in" && unpack "$W/system.in" "$W/system.img" && rm -f "$W/system.in" ;;
	*) unpack "$GSI" "$W/system.img" ;;
	esac
fi

say "the layout"
IMG=${IMAGE:-$W/userdata.img}
rm -f "$IMG"
truncate -s "$SIZE" "$IMG"
if [ -n "$SHARED" ]; then
	# shared gets the rest; Nura what's left of the disk before it
	nura_mib=$(( SIZE / MiB - 1 - 512 ))
	for s in "$SYSTEM" "$VENDOR" "$DATA" "$SHARED"; do
		case "$s" in *G) nura_mib=$((nura_mib - ${s%G} * 1024)) ;; *M) nura_mib=$((nura_mib - ${s%M})) ;; *) die "sizes in M or G" ;; esac
	done
	SHARED=yes BOOT=512M SYSTEM=$SYSTEM VENDOR=$VENDOR DATA=$DATA NURA=${nura_mib}M sh "$LAYOUT" "$IMG" yes > /dev/null
else
	SHARED=no BOOT=512M SYSTEM=$SYSTEM VENDOR=$VENDOR DATA=$DATA sh "$LAYOUT" "$IMG" yes > /dev/null
fi

say "boot partition"
set -- $(part "$IMG" 1)
rm -f "$W/bootpart.img"
mke2fs -q -F -t ext2 -b 1024 -L pmOS_boot -U "$BOOT_UUID" -d "$B" "$W/bootpart.img" $(($2 / 2)) ||
	die "the boot files don't fit the boot partition"
part_in "$IMG" 1 "$W/bootpart.img"
rm -f "$W/bootpart.img"

say "Nura's root"
set -- $(part "$IMG" 5)
e2fsck -fy "$N/root.img" > /dev/null 2>&1 || [ $? -le 1 ] || die "e2fsck of Nura's root failed"
resize2fs "$N/root.img" $(($2 / 8))  > /dev/null 2>&1 || die "Nura's root doesn't fit"
# the hook for mkinitfs, so a new initramfs (kernel update) has it too
debugfs -w -R "mkdir /etc/mkinitfs/hooks" "$N/root.img" > /dev/null 2>&1 || true
debugfs -w -R "write $HOOK /etc/mkinitfs/hooks/20-gts3-dualboot.sh" "$N/root.img" > /dev/null 2>&1
for f in "uid 0" "gid 0" "mode 0100644"; do
	debugfs -w -R "sif /etc/mkinitfs/hooks/20-gts3-dualboot.sh $f" "$N/root.img" > /dev/null 2>&1
done
debugfs -R "stat /etc/mkinitfs/hooks/20-gts3-dualboot.sh" "$N/root.img" 2> /dev/null | grep -q "Size:" ||
	die "can't add the hook to Nura's root"
part_in "$IMG" 5 "$N/root.img"
rm -f "$N/root.img" "$N/boot.img"

if [ -n "$ANDROID" ]; then
	say "Android: system, vendor"
	set -- $(part "$IMG" 2)
	[ "$(stat -c %s "$W/system.img")" -le $(($2 * 512)) ] || die "the system image is larger than android_system ($SYSTEM)"
	part_in "$IMG" 2 "$W/system.img"
	set -- $(part "$IMG" 3)
	[ "$(stat -c %s "$ANDROID/vendor.img")" -le $(($2 * 512)) ] || die "vendor.img is larger than android_vendor ($VENDOR)"
	part_in "$IMG" 3 "$ANDROID/vendor.img"
	rm -f "$W/system.img"
fi

sfdisk -l "$IMG" | sed -n '/^Device/,$p'
if [ -n "$IMAGE" ]; then
	echo "Image: $IMG (raw; not flashed)"
	exit 0
fi

need img2simg "android-sdk-libsparse-utils"
say "sparse image"
img2simg "$IMG" "$W/userdata.simg"
rm -f "$IMG"
echo "This ERASES the userdata partition (Android data, postmarketOS)."
printf 'Type yes to flash: '
read -r a
[ "$a" = yes ] || exit 1
fastboot flash userdata "$W/userdata.simg"
if [ -n "$LK2ND" ]; then
	fastboot flash lk2nd "$LK2ND"
fi
fastboot reboot
echo "Done: the tablet boots Nura; gts3-boot switches."
