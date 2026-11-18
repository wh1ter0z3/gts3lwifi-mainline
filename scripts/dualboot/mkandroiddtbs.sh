#!/bin/sh
# The device trees of a dual boot release's android/ (docs/dual-boot.md), for
# both models, from the Android kernel's own DTBs:
#   dtb/<name>.dtb       Android: the kernel's, plus the APSS watchdog
#   recovery/<name>.dtb  OrangeFox on that kernel: the same, plus
#                        /firmware/android with the ro.boot.* values (an
#                        "androidboot" on the command line would make lk2nd
#                        add S-Boot's own command line)
# The watchdog's clock is the sleep clock, looked up by phandle (the kernel's
# DTBs have no labels).
# usage: mkandroiddtbs.sh <kernel's arch/arm64/boot/dts/qcom> <android dir>
set -eu
[ $# -eq 2 ] || { sed -n '2,10p' "$0" >&2; exit 1; }
K=$1 OUT=$2
mkdir -p "$OUT/dtb" "$OUT/recovery"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
for m in apq8096-samsung-gts3lwifi msm8996-samsung-gts3llte; do
	dtc -q -I dtb -O dts -o "$W/$m.dts" "$K/$m.dtb"
	python3 - "$W/$m.dts" <<'PY'
import re, sys
p = sys.argv[1]
s = open(p).read()
m = re.search(r'sleep-clk\s*\{[^}]*?phandle = <(0x[0-9a-f]+)>', s, re.S)
assert m, "no sleep-clk phandle in the DTB"
wdt = """
/ { soc@0 { watchdog@9830000 {
	compatible = "qcom,apss-wdt-msm8994", "qcom,kpss-wdt";
	reg = <0x09830000 0x1000>;
	interrupts = <0 28 1>, <0 29 1>;
	clocks = <%s>;
}; }; };
""" % m.group(1)
fw = """
/ { firmware { android {
	compatible = "android,firmware";
	hardware = "gts3ml";
	boot_devices = "soc@0/7464900.mmc";
	bootdevice = "7464900.mmc";
	serialno = "gts3recovery";
}; }; };
"""
open(p[:-4] + "-android.dts", "w").write(s + wdt)
open(p[:-4] + "-recovery.dts", "w").write(s + wdt + fw)
PY
	dtc -q -I dts -O dtb -o "$OUT/dtb/$m.dtb" "$W/$m-android.dts"
	dtc -q -I dts -O dtb -o "$OUT/recovery/$m.dtb" "$W/$m-recovery.dts"
done
ls -l "$OUT/dtb" "$OUT/recovery"
