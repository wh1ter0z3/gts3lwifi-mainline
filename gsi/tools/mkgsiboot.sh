#!/bin/sh
# Build the boot image that starts Android from image files (see ramdisk/init).
# usage: [WDT=1] [VKMS=1] [EXTRA_CMDLINE=...] mkgsiboot.sh <Image.gz> <board.dtb> <pmos root uuid> <out.img>
#   WDT=1    add the APSS watchdog node (gsi-init feeds it)
#   VKMS=1   gsi.vkms=1: virtual display, for a board without panel
set -eu
[ $# -eq 4 ] || { echo "usage: $0 <Image.gz> <board.dtb> <pmos root uuid> <out.img>" >&2; exit 1; }
KERNEL=$1 DTB=$2 UUID=$3 OUT=$4
HERE=$(cd "$(dirname "$0")/.." && pwd)
MKBOOTIMG=${MKBOOTIMG:-$HOME/Projects/samsung-tab-s3/lk2nd/lk2nd/scripts/mkbootimg}
BUSYBOX=${BUSYBOX:-$HERE/ramdisk-bin-busybox}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT

dtc -q -I dtb -O dts -o "$W/board.dts" "$DTB"
if [ "${WDT:-0}" = 1 ]; then
	python3 - "$W/board.dts" <<'PY'
import re, sys
p = sys.argv[1]; s = open(p).read()
m = re.search(r'sleep-clk\s*\{[^}]*?phandle = <(0x[0-9a-f]+)>', s, re.S)
assert m, "no sleep-clk phandle in the DTB"
s += """
/ { soc@0 { watchdog@9830000 {
	compatible = "qcom,apss-wdt-msm8994", "qcom,kpss-wdt";
	reg = <0x09830000 0x1000>;
	interrupts = <0 28 1>, <0 29 1>;
	clocks = <%s>;
}; }; };
""" % m.group(1)
open(p, 'w').write(s)
PY
fi
dtc -q -I dts -O dtb -o "$W/board.dtb" "$W/board.dts"
cat "$KERNEL" "$W/board.dtb" > "$W/kernel"

mkdir -p "$W/rd/bin"
cp "$HERE/ramdisk/init" "$W/rd/init"
cp "$BUSYBOX" "$W/rd/bin/busybox"
chmod 0755 "$W/rd/init" "$W/rd/bin/busybox"
(cd "$W/rd" && find . | cpio -o -H newc -R 0:0 --quiet | gzip -9) > "$W/ramdisk"

CMDLINE="console=tty0 clk_ignore_unused pd_ignore_unused msm.separate_gpu_kms=1"
CMDLINE="$CMDLINE sysctl.kernel.firmware_config.ignore_sysfs_fallback=1"
CMDLINE="$CMDLINE firmware_class.path=/vendor/firmware printk.devkmsg=on panic=10"
# Keep the console log (ramoops) short: lk2nd stages only its first ~43 KB
CMDLINE="$CMDLINE loglevel=4 log_buf_len=8M"
CMDLINE="$CMDLINE pmos_root_uuid=$UUID"
# No "androidboot" here: lk2nd would take the image for a downstream Android
# boot and append Samsung's stock arguments (cma=32M, androidboot.hardware=qcom,
# ...) and skip its mainline device tree fixups. gsi-init adds the androidboot
# parameters itself (see ramdisk/init).
[ "${VKMS:-0}" = 1 ] && CMDLINE="$CMDLINE gsi.vkms=1"
[ -z "${EXTRA_CMDLINE:-}" ] || CMDLINE="$CMDLINE $EXTRA_CMDLINE"

python3 "$MKBOOTIMG" --kernel "$W/kernel" --ramdisk "$W/ramdisk" --cmdline "$CMDLINE" \
	--base 0x80000000 --kernel_offset 0x00008000 --ramdisk_offset 0x02200000 \
	--tags_offset 0x02000000 --second_offset 0x00f00000 --pagesize 4096 --output "$OUT"
echo "built $OUT ($(stat -c %s "$OUT") bytes)"
echo "cmdline: $CMDLINE"
