#!/bin/sh
# Check a Galaxy Tab S3 running the `android` branch kernel (7.3.1 with
# gts3-android.config) under postmarketOS: the kernel features Android
# needs, the TrustZone stack and the fingerprint sensor control.
#
#   sudo sh android-kernel-check.sh [dir-with-test-binaries]
#
# Writes everything to ./android-kernel-check-<model>-<date>.log as well.
# Optional test binaries (in the given directory, default: next to this
# script): qseecom-km (keymaster through /dev/qseecom) and esfp0-test
# (fingerprint control); without them those two checks are skipped.
#
# Changes it makes: loads the qcom_qseecom_dev and egis_et5xx_ctrl modules,
# mounts binderfs on a temporary directory and unmounts it, and copies
# the cmnlib and skeymast TrustZone apps from the tablet's own apnhlos
# partition (mounted read-only) to /lib/firmware/updates/qcom/msm8996/
# samsung/gts3/tz/ if they are not there yet. Nothing else is written.

BIN=${1:-$(cd "$(dirname "$0")" && pwd)}
TZ=/lib/firmware/updates/qcom/msm8996/samsung/gts3/tz

[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
# run once more with the output going through tee into the log
if [ -z "${AKC_LOG:-}" ]; then
	MODEL=$(tr -d '\0' < /proc/device-tree/model 2>/dev/null | tr ' ' '_')
	AKC_LOG=$PWD/android-kernel-check-${MODEL:-unknown}-$(date +%Y%m%d-%H%M).log
	export AKC_LOG
	sh "$0" "$@" 2>&1 | tee "$AKC_LOG"
	exit
fi
LOG=$AKC_LOG

ok() { echo "  ok    $*"; }
bad() { echo "  FAIL  $*"; }
check() { if eval "$2" >/dev/null 2>&1; then ok "$1"; else bad "$1"; fi; }

echo "== $(date)  $(tr -d '\0' < /proc/device-tree/model)"
echo "   kernel $(uname -r)  $(uname -v)"
echo "   system: $(systemctl is-system-running 2>/dev/null)"
systemctl --failed --no-legend 2>/dev/null | sed 's/^/   failed unit: /'

echo "== Android kernel features"
check "SELinux in the active LSMs ($(cat /sys/kernel/security/lsm 2>/dev/null))" \
	"grep -q selinux /sys/kernel/security/lsm"
D=$(mktemp -d)
if mount -t binder binder "$D" 2>/dev/null; then
	ok "binderfs mounts: $(ls "$D" | tr '\n' ' ')"
	umount "$D"
else
	bad "binderfs does not mount"
fi
rmdir "$D"
check "ext4 file encryption" "[ -e /sys/fs/ext4/features/encryption ]"
check "ext4 fs-verity" "[ -e /sys/fs/ext4/features/verity ]"
check "userfaultfd" "[ -e /dev/userfaultfd ]"
check "/proc/config.gz" "[ -e /proc/config.gz ]"
check "sw_sync" "[ -e /sys/kernel/debug/sync/sw_sync ] || zcat /proc/config.gz | grep -qx CONFIG_SW_SYNC=y"
check "dm-verity" "zcat /proc/config.gz | grep -qx CONFIG_DM_VERITY=y"

echo "== TrustZone buffer pool"
grep CmaTotal /proc/meminfo | sed 's/^/   /'
HEAP=$(ls /dev/dma_heap/ 2>/dev/null | grep '^qseecom@')
check "DMA-BUF heap ${HEAP:-qseecom@...}" "[ -n '$HEAP' ]"
dmesg | grep -E 'qseecom|reserved mem.*qseecom' | head -4 | sed 's/^/   /'

echo "== /dev/qseecom and keymaster"
modprobe qcom_qseecom_dev
check "/dev/qseecom" "[ -e /dev/qseecom ]"
if [ ! -e "$TZ/skeymast.mdt" ]; then
	PART=$(readlink -f /dev/disk/by-partlabel/apnhlos 2>/dev/null)
	M=$(mktemp -d)
	if [ -b "$PART" ] && mount -o ro "$PART" "$M" 2>/dev/null; then
		mkdir -p "$TZ"
		cp "$M"/image/cmnlib.* "$M"/image/skeymast.* "$TZ"/ 2>/dev/null &&
			ok "copied cmnlib + skeymast from apnhlos ($PART)" ||
			bad "cmnlib/skeymast not found on apnhlos"
		umount "$M"
	else
		bad "apnhlos partition not found or not mountable"
	fi
	rmdir "$M"
fi
if [ -x "$BIN/qseecom-km" ]; then
	"$BIN/qseecom-km" 2>&1 | sed 's/^/   /'
	dmesg | grep -E 'qseecom qseecom: (cmnlib|skeymast)' | tail -2 | sed 's/^/   /'
else
	echo "   (no qseecom-km binary in $BIN, skipped)"
fi

echo "== Fingerprint sensor control"
modprobe egis_et5xx_ctrl
check "/dev/esfp0" "[ -e /dev/esfp0 ]"
for a in name vendor adm type_check; do
	echo "   $a: $(cat /sys/class/fingerprint/fingerprint/$a 2>/dev/null)"
done
if [ -x "$BIN/esfp0-test" ]; then
	"$BIN/esfp0-test" 2>&1 | sed 's/^/   /'
else
	echo "   (no esfp0-test binary in $BIN, skipped)"
fi
dmesg | grep 'egis-et5xx-ctrl' | tail -6 | sed 's/^/   /'

echo "== Kernel errors (first 40)"
dmesg --level=err,crit,alert,emerg 2>/dev/null | head -40 | sed 's/^/   /'
echo "== done, log: $LOG"
