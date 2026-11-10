#!/bin/sh
# GloDroid's msm8916 vendor (Android 13, VNDK 33: Mesa freedreno,
# drm_hwcomposer, minigbm, generic HALs; built for cortex-a53, so it runs on
# the Kryo) made bootable on the Tab S3: our fstab and firmware, busybox debug
# shell, their kernel modules (for their 6.10 msm8916 kernel) out. File labels
# are kept (init won't start a service whose binary has none, even when
# permissive). Runs as root on the tablet.
# usage: [HEALTH=<binary>] [LIGHTS=<binary>] [SEHSTUBS=<binary>] mkvendor-glodroid.sh <glodroid vendor.img (raw ext4)> <common overlay> <busybox> <out.img>
set -eu
SRC=$1 COV=$2 BB=$3 OUT=$4
FIRMWARE=${FIRMWARE:-/lib/firmware}
W=$(mktemp -d /var/tmp/mkvg.XXXXXX)
trap 'umount "$W/src" 2>/dev/null || :; rm -rf "$W"' EXIT
mkdir "$W/src" "$W/v"
V=$W/v
mount -o ro,loop "$SRC" "$W/src"
tar -C "$W/src" --xattrs --xattrs-include='*' -cf - . | tar -C "$V" --xattrs --xattrs-include='*' -xpf -
umount "$W/src"
# init.common.rc mounts /vendor/etc/fstab.${ro.hardware}; gsi-init passes gts3
install -m 0644 -o 0 -g 0 "$COV/etc/fstab.gts3" "$V/etc/"
# Our mixer configuration for their tinyhal audio HAL, which reads
# /vendor/etc/audio.<ro.product.device>.xml: the vendor's name (idol347) wins,
# ro.product.property_source_order starts with odm,vendor
if [ -f "$COV/etc/audio.gts3.xml" ]; then
	cat "$COV/etc/audio.gts3.xml" > "$V/etc/audio.idol347.xml"
fi
# OneUI's libeffectsconfig looks for Samsung's file name (and its audioserver
# aborts at the first sound without an effects configuration)
[ -f "$V/etc/audio_effects_sec.xml" ] || cp -a "$V/etc/audio_effects.xml" "$V/etc/audio_effects_sec.xml"
rm -rf "$V/lib/modules"; mkdir -m 0755 "$V/lib/modules"
# Their phone's modem (they start remoteproc1, our MSS), rild, rmtfs, radio HAL
rm -f "$V/etc/init/modem_manager.rc" "$V/etc/init/rild.rc" "$V/etc/init/init.rmtfs.rc" \
	"$V/etc/init/android.hardware.mmradio.rc" "$V/etc/vintf/manifest/android.hardware.mmradio.xml"
# No vibration motor on the Tab S3: don't declare their force-feedback
# vibrator HAL (OneUI's libvibratorservice also dereferences a Samsung extension
# of it that isn't there; with no HAL it reports no vibrator)
rm -f "$V/etc/init/vibrator-glodroid.rc" "$V/etc/vintf/manifest/vibrator-glodroid.xml" \
	"$V/bin/hw/android.hardware.vibrator-service.glodroid"
# Wi-Fi Direct: the QCA6174 (ath10k) does P2P; OneUI enables Wi-Fi display
# and its DisplayManagerService dies without this feature
[ -f "$V/etc/permissions/android.hardware.wifi.direct.xml" ] ||
	printf '<?xml version="1.0" encoding="utf-8"?>\n<permissions>\n    <feature name="android.hardware.wifi.direct" />\n</permissions>\n' \
		> "$V/etc/permissions/android.hardware.wifi.direct.xml"
# Software KeyMint needs patch levels, or keystore2 fails earlyBootEnded (-68)
# and boot stalls; theirs is empty. The system's date (GSIs are newer).
sed -i '/^ro.vendor.build.security_patch=/d' "$V/build.prop"
printf 'ro.vendor.build.security_patch=%s\nro.vendor.boot_security_patch=%s\n' \
	"${PATCH:-2024-08-05}" "${PATCH:-2024-08-05}" >> "$V/build.prop"
# For any system image: name the EGL driver (GloDroid's own system finds it
# without), and give first boots time (a big GSI's package scan trips the
# watchdog otherwise)
printf 'ro.hardware.egl=mesa\nro.hw_timeout_multiplier=4\n' >> "$V/build.prop"
# HEALTH=<binary>: our health HAL (gsi/health/health-mainline.c: IHealth v1
# plus the ISehHealth extension OneUI needs) over theirs; written into the
# existing file so its label, init service and VINTF entry stay as they are
if [ -n "${HEALTH:-}" ]; then
	cat "$HEALTH" > "$V/bin/hw/android.hardware.health-service.example"
fi
# LIGHTS=<binary>: our lights HAL (gsi/health/lights-mainline.c: backlight,
# touch key LED, and the notification light OneUI's LightsService assumes)
if [ -n "${LIGHTS:-}" ]; then
	cat "$LIGHTS" > "$V/bin/hw/android.hardware.lights-service"
fi
# SEHSTUBS=<binary>: stand-ins (gsi/health/seh-stubs.c) for the Samsung HALs a
# OneUI system_server waits for forever; a plain GSI never asks for them
SEH="vendor.samsung.hardware.displayquality.ISehDisplayQuality vendor.samsung.hardware.khdm.ISehKhdm vendor.samsung.hardware.tlc.ddar.ISehDdar"
if [ -n "${SEHSTUBS:-}" ]; then
	install -m 0755 -o 0 -g 2000 "$SEHSTUBS" "$V/bin/hw/vendor.samsung.hardware.seh-stubs"
	{ echo "service vendor.seh-stubs /vendor/bin/hw/vendor.samsung.hardware.seh-stubs $SEH"
	  echo "    class hal"
	  echo "    user system"
	  echo "    group system"
	  echo "    seclabel u:r:hal_health_default:s0"; } > "$V/etc/init/vendor.samsung.hardware.seh-stubs.rc"
	{ echo '<manifest version="1.0" type="device">'
	  for i in $SEH; do
		echo "    <hal format=\"aidl\"><name>${i%.*}</name><fqname>${i##*.}/default</fqname></hal>"
	  done
	  echo '</manifest>'; } > "$V/etc/vintf/manifest/vendor.samsung.hardware.seh-stubs.xml"
fi
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
