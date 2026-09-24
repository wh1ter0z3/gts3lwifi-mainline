#!/bin/sh
# Build a RAM-bootable test boot image from a DTS, reusing the kernel and
# initramfs of the boot.img that pmbootstrap already built (no kernel rebuild).
# Usage: [EXTRA_CMDLINE="..."] make-test-image.sh <board.dts> <out.img>
set -eu
[ $# -eq 2 ] || { echo "usage: $0 <board.dts> <out.img>" >&2; exit 1; }
DTS=$(readlink -f "$1"); OUT=$2
HERE=$(cd "$(dirname "$0")" && pwd)
BOOT=${BOOT:-$HOME/.local/var/pmbootstrap/chroot_rootfs_samsung-gts3lwifi/boot/boot.img}
MKBOOTIMG=${MKBOOTIMG:-$HOME/samsung/lk2nd/lk2nd/scripts/mkbootimg}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
mkdir "$W/k"; tar -xzf "$HERE/base/kernel-dts-v6.19.5-msm8996.tar.gz" -C "$W/k"
Q=$W/k/arch/arm64/boot/dts/qcom
cpp -nostdinc -I "$W/k/include" -I "$Q" -I "$W/k/scripts/dtc/include-prefixes" -undef -D__DTS__ \
	-x assembler-with-cpp "$DTS" | dtc -q -I dts -O dtb -o "$W/board.dtb" -
python3 - "$BOOT" "$W" <<'PY'
import sys, struct, zlib
b = open(sys.argv[1], 'rb').read(); w = sys.argv[2]
ks, ka, rs, ra, ss, sa, ta, ps = struct.unpack('<8I', b[8:40])
k = b[ps:ps+ks]; d = zlib.decompressobj(31); d.decompress(k); gz = k[:len(k)-len(d.unused_data)]
off = ps + (ks + ps - 1) // ps * ps
open(w+'/Image.gz', 'wb').write(gz); open(w+'/ramdisk', 'wb').write(b[off:off+rs])
open(w+'/cmdline', 'w').write(b[64:576].rstrip(bytes(1)).decode())
PY
cat "$W/Image.gz" "$W/board.dtb" > "$W/kernel"
# EXTRA_CMDLINE replaces the console/earlycon part of the original cmdline
# and keeps the pmOS partition parameters that follow it.
[ -z "${EXTRA_CMDLINE:-}" ] || printf '%s %s' "$EXTRA_CMDLINE" \
	"$(sed -E 's/(^| )(earlycon|console=[^ ]+)//g; s/^ +//' "$W/cmdline")" > "$W/cmdline"
python3 "$MKBOOTIMG" --kernel "$W/kernel" --ramdisk "$W/ramdisk" --cmdline "$(cat "$W/cmdline")" \
	--base 0x80000000 --kernel_offset 0x00008000 --ramdisk_offset 0x02200000 \
	--tags_offset 0x02000000 --second_offset 0x00f00000 --pagesize 4096 --output "$OUT"
echo "built $OUT ($(stat -c %s "$OUT") bytes) from $DTS"
