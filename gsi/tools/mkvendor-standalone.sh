#!/bin/sh
# Makes the standalone vendor image (no pmOS on the tablet) from a working
# vendor.img (tools/mkvendor-glodroid.sh): what it took from the pmOS root
# comes along instead, and nothing from Samsung stays in it.
#  - firmware: only what linux-firmware and wireless-regdb ship (ath10k
#    QCA6174 firmware and generic board data, the QCA Bluetooth patch and NVM,
#    the A530 GPU microcode, regulatory.db). The tablet's own firmware is read
#    from its partitions at boot: gsi-init mounts apnhlos, modem, system and
#    efs read only on /vendor/firmware/.part/, and relative links here give the
#    names the drivers ask for (as pmOS's gts3-firmware copies them): GPU zap
#    shader, Venus, SLPI, TrustZone apps (apnhlos), ADSP, MBA, modem (modem),
#    Wi-Fi board data (system: etc/firmware/bdwlan30.bin), sensor registry
#    with the factory calibration (efs). Links to segments a firmware doesn't
#    have are never followed.
#  - lib/modules/<version>: the kernel's modules, uncompressed (busybox
#    modprobe reads no zstd), with depmod's index
#  - bin/gts3-bdaddr (bdaddr/), etc/modprobe.d/
# Runs as root on the tablet, with e2fsprogs (pmOS's, in a chroot: its root in
# $ER, default /mnt/vendor/pmos, as Android sees it in gsi-init's pmOS mode).
# usage: mkvendor-standalone.sh <vendor.img> <modules.tar (lib/modules tree)> <gts3-bdaddr> <modprobe.d dir> <out.img>
set -eu
SRC=$1 MODS=$2 BDADDR=$3 MPD=$4 OUT=$5
ER=${ER:-/mnt/vendor/pmos}
E2="chroot $ER"	# e2fsck (paths relative to that root)
W=$(mktemp -d)
trap 'umount "$W/v" 2>/dev/null || :; rm -rf "$W"' EXIT

cp "$SRC" "$OUT"
case "$OUT" in "$ER"/*) O=${OUT#"$ER"} ;; *) O=$OUT ;; esac
$E2 /usr/sbin/e2fsck -fy "$O" >/dev/null 2>&1 || :
mkdir "$W/v"
mount -o loop,rw "$OUT" "$W/v"
V=$W/v

# firmware: the redistributable files only
F=$V/firmware
mkdir "$W/keep"
for p in ath10k/QCA6174/hw3.0/firmware-6.bin ath10k/QCA6174/hw3.0/board-2.bin \
	 ath10k/QCA6174/hw3.0/notice_ath10k_firmware-6.txt \
	 qca/rampatch_00440302.bin qca/nvm_00440302.bin \
	 qcom/a530_pfp.fw qcom/a530_pm4.fw qcom/a530v3_gpmu.fw2 \
	 regulatory.db regulatory.db.p7s; do
	d=$(dirname "$p")
	for f in "$F/$p" "$F/$p.zst" "$F/$p.xz"; do
		[ -f "$f" ] || continue
		mkdir -p "$W/keep/$d"
		cp -a "$f" "$W/keep/$d/"
	done
done
# linux-firmware's generic QCA6174 board data, for a tablet without Samsung's
[ -f "$F/ath10k/QCA6174/hw3.0/board.bin.zst" ] &&
	cp -a "$F/ath10k/QCA6174/hw3.0/board.bin.zst" "$W/keep/ath10k/QCA6174/hw3.0/"
rm -rf "$F" && mkdir -m 0755 "$F"
cp -a "$W/keep/." "$F/"

mkdir -p "$F/.part/apnhlos" "$F/.part/modem" "$F/.part/system" "$F/.part/efs"
G=$F/qcom/msm8996/samsung/gts3
mkdir -p "$G/tz" "$F/qcom/sensors" "$F/ath10k/QCA6174/hw3.0"
# <dir> <firmware> <partition> <relative path up to firmware/>
segments() {
	ln -sf "$4.part/$3/image/$2.mdt" "$1/$2.mdt"
	i=0
	while [ $i -lt 32 ]; do
		n=$(printf 'b%02d' $i)
		ln -sf "$4.part/$3/image/$2.$n" "$1/$2.$n"
		i=$((i + 1))
	done
}
for fw in a530_zap venus slpi; do segments "$G" $fw apnhlos ../../../../; done
for fw in adsp modem; do segments "$G" $fw modem ../../../../; done
ln -sf ../../../../.part/modem/image/mba.mbn "$G/mba.mbn"
ln -sf ../../../../.part/modem/image/modem_pr "$G/modem_pr"
for fw in cmnlib cmnlib64 skeymast dualfp authhat widevine; do
	segments "$G/tz" $fw apnhlos ../../../../../
done
ln -sf ../../../.part/system/etc/firmware/bdwlan30.bin "$F/ath10k/QCA6174/hw3.0/board.bin"
for m in samsung,gts3lwifi samsung,gts3llte; do
	ln -sf ../../.part/efs/FactoryApp/sensors/sns.reg "$F/qcom/sensors/sns.reg-$m"
done
chown -R 0:0 "$F"

rm -rf "$V/lib/modules" && mkdir -m 0755 "$V/lib/modules"
tar -C "$V/lib" -xf "$MODS"
chown -R 0:0 "$V/lib/modules"
install -m 0755 -o 0 -g 2000 "$BDADDR" "$V/bin/gts3-bdaddr"
mkdir -p "$V/etc/modprobe.d"
cp "$MPD"/*.conf "$V/etc/modprobe.d/"
chown -R 0:0 "$V/etc/modprobe.d"; chmod 0644 "$V/etc/modprobe.d"/*.conf

echo "firmware: $(find "$F" -type f | wc -l) files, $(find "$F" -type l | wc -l) links; modules: $(ls "$V/lib/modules")"
df -h "$V" | tail -1
# Zero the free blocks: what was removed above (the firmware of the image it
# started from, Samsung's among it) must not stay in the image's free space
dd if=/dev/zero of="$V/.zero" bs=1048576 2>/dev/null || :
rm -f "$V/.zero"
sync
umount "$V"
$E2 /usr/sbin/e2fsck -fy "$O" >/dev/null 2>&1 || :
echo "built $OUT"
