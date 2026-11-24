#!/bin/sh
# Installs Nura (postmarketOS), our Android (any GSI on our vendor), or both
# side by side on a Galaxy Tab S3 that runs lk2nd, from a Linux PC:
# builds the userdata partition's image here with the dual boot layout
# (docs/dual-boot.md), flashes it and lk2nd v2 over lk2nd's fastboot. The
# tablet's own firmware stays on its partitions.
#
# usage: install-dualboot.sh --zip <release zip> [--root <nura-root.img.xz>] [--gsi <GSI>] [options]
#    or: install-dualboot.sh --nura <Nura userdata image> [options]
#   --zip FILE       the dual boot release's zip (the OrangeFox installer):
#                    Nura's boot files, our Android files, the recovery, lk2nd
#   --root FILE      with --zip: the release's nura-root.img.xz (Nura); with
#                    --zip, --root and --gsi pick the setup: Nura only, Android
#                    only, or both side by side
#   --nura FILE      instead of --zip: Nura's userdata image (.simg/.img, may
#                    be .zst/.xz), e.g. pmos-...-userdata.simg.zst
#   --gsi FILE       Android: the GSI (system.img, .img.xz/.gz/.zst, or a zip
#                    with system.img inside); with --android, else no Android
#   --android DIR    our Android files: Image.gz, initramfs (gsi-init with
#                    gsi.parts), cmdline, vendor.img, dtb/<model's dtb> (or
#                    gts3.dtb); recovery/ (OrangeFox on that kernel: ramdisk,
#                    cmdline, <model's dtb>) adds an "orangefox" entry, also
#                    without --gsi
#   --lk2nd FILE     lk2nd v2 image for this model (flashed after userdata;
#                    with --zip: the zip's, unless given)
#   --system SIZE --vendor SIZE --data SIZE   Android's partitions
#                    (default 4G, 1G, 4G); Nura gets the rest
#   --shared SIZE    also a shared exFAT partition (default: none, the
#                    microSD card is the shared storage)
#   --image FILE     only build the image (raw, sparse file) here; for tests
#   --size BYTES     with --image: the userdata size (else asked from lk2nd)
#   --model T820|T825  with --image: the model (else asked from lk2nd)
#   --payload DIR    don't install: write the files twrp/update-binary (the
#                    OrangeFox installer) needs into DIR: nura-boot.tar,
#                    nura-root.img.xz, nura.env, and android/ from --android
# ERASES the userdata partition: Android data, a postmarketOS install on it.
# Needs: fastboot, e2fsprogs (mke2fs, e2fsck, resize2fs, debugfs), sfdisk,
# simg2img and img2simg (android-sdk-libsparse-utils / android-tools),
# python3, cpio, zstd/xz for compressed images, xz for --payload. WORK=<dir> (default: ./work)
# needs room for the Nura image unpacked and the userdata image's data.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
PATH=$PATH:/usr/sbin:/sbin
MiB=1048576

die() { echo "error: $*" >&2; exit 1; }
# Nura's root mount options with inlinecrypt, so fscrypt encrypts on the
# eMMC's inline crypto engine (docs/encryption.md); harmless without fscrypt
ice_cmdline() {
	case " $1 " in
	*inlinecrypt*) printf '%s' "$1" ;;
	*" pmos_rootfsopts="*) printf '%s' "$1" | sed -E 's/(pmos_rootfsopts=[^ ]*)/\1,inlinecrypt/' ;;
	*) printf '%s pmos_rootfsopts=defaults,inlinecrypt' "$1" ;;
	esac
}
need() { command -v "$1" >/dev/null 2>&1 || die "$1 not found ($2)"; }
say() { echo "== $*"; }

NURA= GSI= ANDROID= LK2ND= IMAGE= SIZE= MODEL= PAYLOAD= ZIPIN= ROOTIN=
SYSTEM=4G VENDOR=1G DATA=4G SHARED=
while [ $# -gt 0 ]; do
	case "$1" in
	--nura) NURA=$2 ;; --gsi) GSI=$2 ;; --android) ANDROID=$2 ;; --lk2nd) LK2ND=$2 ;;
	--zip) ZIPIN=$2 ;; --root) ROOTIN=$2 ;;
	--system) SYSTEM=$2 ;; --vendor) VENDOR=$2 ;; --data) DATA=$2 ;; --shared) SHARED=$2 ;;
	--image) IMAGE=$2 ;; --size) SIZE=$2 ;; --model) MODEL=$2 ;; --payload) PAYLOAD=$2 ;;
	*) sed -n '2,/^set -eu/p' "$0" | sed '$d' >&2; exit 1 ;;
	esac
	shift 2
done
if [ -n "$ZIPIN" ]; then
	[ -f "$ZIPIN" ] || die "--zip: no such file '$ZIPIN'"
	[ -z "$ROOTIN" ] || [ -f "$ROOTIN" ] || die "--root: no such file '$ROOTIN'"
	[ -n "$ROOTIN$GSI" ] || die "--zip needs --root nura-root.img.xz (Nura), --gsi (Android), or both"
	[ -z "$NURA$ANDROID$PAYLOAD" ] || die "--zip brings Nura and our Android: no --nura, --android or --payload"
else
	[ -f "$NURA" ] || die "--nura: no such file '$NURA'"
fi
[ -z "$GSI" ] || [ -n "$ANDROID$ZIPIN" ] || die "--gsi needs --android (our vendor and boot files)"
# Nura: always without --zip (--nura); with --zip if --root is given
HASNURA=yes
[ -z "$ZIPIN" ] || [ -n "$ROOTIN" ] || HASNURA=
if [ -n "$ANDROID" ]; then
	for f in Image.gz initramfs cmdline; do
		[ -s "$ANDROID/$f" ] || die "--android: no $f in $ANDROID"
	done
	[ -z "$GSI" ] || [ -s "$ANDROID/vendor.img" ] || die "--android: no vendor.img in $ANDROID"
fi
for t in mke2fs e2fsck resize2fs debugfs sfdisk blkid simg2img python3 cpio; do need $t "see the usage"; done
LAYOUT=$HERE/gts3-layout
[ -x "$LAYOUT" ] || LAYOUT=$HERE/../dualboot/gts3-layout
[ -f "$LAYOUT" ] || die "gts3-layout not next to this script"
HOOK=$HERE/20-gts3-dualboot.sh
[ -f "$HOOK" ] || HOOK=$HERE/../../pmaports/device-samsung-gts3lwifi/20-gts3-dualboot.sh
[ -f "$HOOK" ] || [ -n "$ZIPIN" ] || die "20-gts3-dualboot.sh not found"

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
# the number of the partition named $2 on disk image $1
pnum() { sfdisk -d "$1" | sed -n "s/^[^ ]*[^0-9]\([0-9]*\) : .*name=\"$2\".*/\1/p"; }
# a partition of disk image $1 into file $3 / from file $3 (sparse)
part_out() { set -- "$1" "$3" $(part "$1" "$2"); dd if="$1" of="$2" bs=512 skip="$3" count="$4" conv=sparse status=none; }
part_in() { set -- "$1" "$3" $(part "$1" "$2"); dd if="$2" of="$1" bs=512 seek="$3" conv=sparse,notrunc status=none; }

N=$W/nura
if [ -n "$ZIPIN" ]; then
	need unzip unzip
	need xz xz-utils
	say "the zip: $(basename "$ZIPIN")"
	rm -rf "$W/zip"
	unzip -q -o "$ZIPIN" -x 'META-INF/*' 'tools/*' -d "$W/zip" || die "can't unpack $ZIPIN"
	(cd "$W/zip" && grep -v ' tools/' SHA256SUMS | sha256sum -c --quiet) || die "the zip's files are damaged, download it again"
	. "$W/zip/nura.env"
	[ -z "$HASNURA" ] || tar -C "$W/bootfs" -xf "$W/zip/nura-boot.tar" || die "Nura's boot files"
	[ -d "$W/zip/android" ] && ANDROID=$W/zip/android
	[ -n "$GSI" ] && [ -z "$ANDROID" ] && die "this zip has no Android files"
	[ -n "$LK2ND" ] || LK2ND=$W/zip/lk2nd-$(case $MODEL in T820) echo gts3lwifi ;; T825) echo gts3llte ;; esac).img
	[ -s "$LK2ND" ] || die "the zip has no lk2nd for SM-$MODEL"
	if [ -n "$HASNURA" ] && [ -f "$ROOTIN.sha256" ]; then
		say "checking $(basename "$ROOTIN")"
		[ "$(sha256sum "$ROOTIN" | cut -d' ' -f1)" = "$(cut -d' ' -f1 "$ROOTIN.sha256")" ] || die "$(basename "$ROOTIN") is damaged, download it again"
	fi
	if [ -n "$HASNURA" ]; then
		say "Nura: unpacking $(basename "$ROOTIN")"
		xz -dc "$ROOTIN" | dd of="$N/root.img" bs=1M conv=sparse status=none || die "unpacking $ROOTIN"
	fi
	B=$W/bootfs
fi
# our Android's device tree and the recovery's for this model
if [ -n "$ANDROID" ] && [ -z "$PAYLOAD" ]; then
	ADTB=$ANDROID/dtb/$DTB
	[ -s "$ADTB" ] || ADTB=$ANDROID/gts3.dtb
	[ -s "$ADTB" ] || die "--android: no dtb/$DTB in $ANDROID"
	RDTB=
	if [ -s "$ANDROID/recovery/ramdisk" ]; then
		RDTB=$ANDROID/recovery/$DTB
		[ -s "$RDTB" ] && [ -s "$ANDROID/recovery/cmdline" ] || die "--android: no recovery/$DTB or recovery/cmdline"
	fi
fi
# The hook that finds Nura's root in the layout (device package r15 has
# it): in its initramfs now, as a second cpio archive, and for mkinitfs.
# For mkinitfs only if the image's device package is older than r15: a copy
# in /etc would shadow the package's own when it changes.
add_hook() {
	debugfs -R "stat /usr/share/mkinitfs/hooks/20-gts3-dualboot.sh" "$N/root.img" 2> /dev/null |
		grep -q "Size:" && return 0
	debugfs -w -R "mkdir /etc/mkinitfs/hooks" "$N/root.img" > /dev/null 2>&1 || true
	debugfs -w -R "write $HOOK /etc/mkinitfs/hooks/20-gts3-dualboot.sh" "$N/root.img" > /dev/null 2>&1
	for f in "uid 0" "gid 0" "mode 0100644"; do
		debugfs -w -R "sif /etc/mkinitfs/hooks/20-gts3-dualboot.sh $f" "$N/root.img" > /dev/null 2>&1
	done
	debugfs -R "stat /etc/mkinitfs/hooks/20-gts3-dualboot.sh" "$N/root.img" 2> /dev/null | grep -q "Size:" ||
		die "can't add the hook to Nura's root"
}
if [ -z "$ZIPIN" ]; then
say "Nura: unpacking $(basename "$NURA")"
unpack "$NURA" "$W/nura/disk.img"
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

# ... and in its initramfs now, as a second cpio archive
mkdir -p "$W/hook/hooks"
cp "$HOOK" "$W/hook/hooks/20-gts3-dualboot.sh"
(cd "$W/hook" && printf 'hooks\nhooks/20-gts3-dualboot.sh\n' | cpio -o -H newc -R 0:0 --quiet | gzip -9n) >> "$B/initramfs"
fi

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
	tune2fs -O encrypt "$N/root.img" > /dev/null || die "can't turn on ext4's encrypt feature in Nura's root"
	add_hook
	# raw and xz'd, for busybox's unxz in the recovery (a sparse image can't
	# be read from a pipe, and release files must stay under 2 GiB)
	need xz xz-utils
	say "xz of Nura's root (some minutes)"
	xz -c -6 -T"${XZ_THREADS:-2}" "$N/root.img" > "$P/nura-root.img.xz.part" || die "xz of Nura's root"
	mv "$P/nura-root.img.xz.part" "$P/nura-root.img.xz"
	(cd "$P" && sha256sum nura-root.img.xz > nura-root.img.xz.sha256)
	{
		echo "# Nura (postmarketOS) image this payload is from: $(basename "$NURA")"
		echo "BOOT_UUID=$BOOT_UUID"
		echo "ROOT_UUID=$ROOT_UUID"
		echo "NURA_CMDLINE='$NURA_CMDLINE'"
		echo "ROOT_BYTES=$(stat -c %s "$N/root.img")"
	} > "$P/nura.env"
	if [ -n "$ANDROID" ]; then
		rm -rf "$P/android"
		cp -rL "$ANDROID" "$P/android"
	fi
	rm -f "$N/root.img" "$N/boot.img"
	ls -l "$P" "$P/android" 2> /dev/null
	exit 0
fi

# Android's boot files, and the recovery's (OrangeFox on Android's kernel)
if [ -n "$ANDROID" ]; then
	mkdir -p "$B/gts3/android"
	cp "$ANDROID/Image.gz" "$B/gts3/android/"
	if [ -n "$GSI" ]; then
		cp "$ADTB" "$B/gts3/android/gts3.dtb"
		cp "$ANDROID/initramfs" "$B/gts3/android/initramfs"
		# USB is Android's (adb, MTP) and Android sleeps; GTS3_GSI_USB=net
		# keeps gsi-init's debug network link instead (headless boards)
		ANDROID_CMDLINE="$(cat "$ANDROID/cmdline") gsi.parts=1 gsi.usb=${GTS3_GSI_USB:-android}"
	fi
	if [ -n "$RDTB" ]; then
		mkdir -p "$B/gts3/recovery"
		cp "$ANDROID/recovery/ramdisk" "$B/gts3/recovery/ramdisk"
		cp "$RDTB" "$B/gts3/recovery/gts3.dtb"
		RECOVERY_CMDLINE=$(cat "$ANDROID/recovery/cmdline")
	fi
fi

mkdir -p "$B/extlinux"
{
	echo "# Galaxy Tab S3 (docs/dual-boot.md). lk2nd boots the default label;"
	echo "# switch with gts3-boot, or Volume Down at power-on for lk2nd's menu."
	if [ -n "$HASNURA" ]; then
		echo "default nura"
		echo
		echo "label nura"
		echo "	linux /vmlinuz"
		echo "	initrd /initramfs"
		echo "	fdt /$DTB"
		echo "	append $(ice_cmdline "$NURA_CMDLINE")"
	else
		echo "default android"
	fi
	if [ -n "$GSI" ]; then
		echo
		echo "label android"
		echo "	linux /gts3/android/Image.gz"
		echo "	initrd /gts3/android/initramfs"
		echo "	fdt /gts3/android/gts3.dtb"
		echo "	append $ANDROID_CMDLINE"
	fi
	if [ -n "${RECOVERY_CMDLINE:-}" ]; then
		echo
		echo "label orangefox"
		echo "	linux /gts3/android/Image.gz"
		echo "	initrd /gts3/recovery/ramdisk"
		echo "	fdt /gts3/recovery/gts3.dtb"
		echo "	append $RECOVERY_CMDLINE"
	fi
} > "$B/extlinux/extlinux.conf"

# Android's system image
if [ -n "$GSI" ]; then
	say "Android: the system image from $(basename "$GSI")"
	case "$GSI" in
	*.zip)
		n=$(unzip -Z1 "$GSI" | grep -E '(^|/)system\.img$' | head -n 1)
		[ -n "$n" ] || die "no system.img in $GSI"
		unzip -p "$GSI" "$n" > "$W/system.in" && unpack "$W/system.in" "$W/system.img" && rm -f "$W/system.in" ;;
	*) unpack "$GSI" "$W/system.img" ;;
	esac
fi

if [ -n "$HASNURA" ] && [ -n "$GSI" ]; then SETUP="Nura and Android"
elif [ -n "$HASNURA" ]; then SETUP="Nura only"
else SETUP="Android only"; fi
say "the layout: $SETUP"
IMG=${IMAGE:-$W/userdata.img}
rm -f "$IMG"
truncate -s "$SIZE" "$IMG"
# Nura only: no Android system or vendor, and android_data only for the
# recovery's /data (it must not be userdata: that holds the layout).
# Android only: no Nura, android_data to the end.
NURAOPT=
if [ -z "$GSI" ] && [ -n "$HASNURA" ]; then SYSTEM=0 VENDOR=0 DATA=256M; fi
[ -n "$HASNURA" ] || NURAOPT=no
if [ -n "$SHARED" ] && [ -n "$HASNURA" ]; then
	# shared gets the rest; Nura what's left of the disk before it
	nura_mib=$(( SIZE / MiB - 1 - 512 ))
	for s in "$SYSTEM" "$VENDOR" "$DATA" "$SHARED"; do
		case "$s" in *G) nura_mib=$((nura_mib - ${s%G} * 1024)) ;; *M) nura_mib=$((nura_mib - ${s%M})) ;; *) die "sizes in M or G" ;; esac
	done
	SHARED=yes BOOT=512M SYSTEM=$SYSTEM VENDOR=$VENDOR DATA=$DATA NURA=${nura_mib}M sh "$LAYOUT" "$IMG" yes > /dev/null
else
	SHARED=no BOOT=512M SYSTEM=$SYSTEM VENDOR=$VENDOR DATA=$DATA NURA=${NURAOPT:-auto} sh "$LAYOUT" "$IMG" yes > /dev/null
fi

say "boot partition"
set -- $(part "$IMG" 1)
rm -f "$W/bootpart.img"
mke2fs -q -F -t ext2 -b 1024 -L pmOS_boot -U "$BOOT_UUID" -d "$B" "$W/bootpart.img" $(($2 / 2)) ||
	die "the boot files don't fit the boot partition"
part_in "$IMG" 1 "$W/bootpart.img"
rm -f "$W/bootpart.img"

if [ -n "$HASNURA" ]; then
	say "Nura's root"
	set -- $(part "$IMG" "$(pnum "$IMG" nura)")
	e2fsck -fy "$N/root.img" > /dev/null 2>&1 || [ $? -le 1 ] || die "e2fsck of Nura's root failed"
	resize2fs "$N/root.img" $(($2 / 8))  > /dev/null 2>&1 || die "Nura's root doesn't fit"
	tune2fs -O encrypt "$N/root.img" > /dev/null || die "can't turn on ext4's encrypt feature in Nura's root"
	add_hook
	part_in "$IMG" "$(pnum "$IMG" nura)" "$N/root.img"
fi
rm -f "$N/root.img" "$N/boot.img"

if [ -n "$GSI" ]; then
	say "Android: system, vendor"
	set -- $(part "$IMG" "$(pnum "$IMG" android_system)")
	[ "$(stat -c %s "$W/system.img")" -le $(($2 * 512)) ] || die "the system image is larger than android_system ($SYSTEM)"
	part_in "$IMG" "$(pnum "$IMG" android_system)" "$W/system.img"
	set -- $(part "$IMG" "$(pnum "$IMG" android_vendor)")
	[ "$(stat -c %s "$ANDROID/vendor.img")" -le $(($2 * 512)) ] || die "vendor.img is larger than android_vendor ($VENDOR)"
	part_in "$IMG" "$(pnum "$IMG" android_vendor)" "$ANDROID/vendor.img"
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
if [ -n "$HASNURA" ]; then
	echo "Done ($SETUP): the tablet boots Nura."
else
	echo "Done ($SETUP): the tablet boots Android."
fi
