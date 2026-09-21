#!/bin/sh
# Pack lk2nd the way Samsung's S-Boot on the Galaxy Tab S3 accepts it:
# gzip'd lk.bin + the stock Samsung device trees (with the lk2nd node merged
# in), stock load addresses, page size 4096, SEANDROIDENFORCE trailer.
#
# Usage: pack-lk2nd-samsung.sh <donor-boot.img> <out.img> [ramdisk]
#   donor-boot.img  any boot image for this tablet whose kernel has the stock
#                   device trees appended (stock, LineageOS, ...). Its cmdline
#                   and os_version are reused. Only the device trees are taken.
#   ramdisk         optional; default is an empty gzip'd cpio archive
# Env: LK2ND=~/samsung/lk2nd  LK2ND_DTB=msm8996-samsung-gts3lwifi
#      LK2ND_PARTITION_SIZE="4*1024*1024"
set -eu
[ $# -ge 2 ] || { sed -n '2,13p' "$0"; exit 1; }
DONOR=$(readlink -f "$1"); OUT=$2; RD=${3:-}
LK2ND=${LK2ND:-$HOME/samsung/lk2nd}; NAME=${LK2ND_DTB:-msm8996-samsung-gts3lwifi}
PSIZE=${LK2ND_PARTITION_SIZE:-4*1024*1024}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
make -C "$LK2ND" BOOTLOADER_OUT="$W/out" TOOLCHAIN_PREFIX=arm-none-eabi- \
	LK2ND_PARTITION_SIZE="$PSIZE" lk2nd-msm8996 -j"$(nproc)" >"$W/build.log" 2>&1 ||
	{ tail -20 "$W/build.log"; exit 1; }
if [ -z "$RD" ]; then (cd "$W" && mkdir e && cd e && find . | cpio -o -H newc 2>/dev/null | gzip -9n > "$W/ramdisk"); RD=$W/ramdisk; fi
python3 - "$DONOR" "$W" "$NAME" <<'PY'
import sys, struct, zlib, re, subprocess
donor, w, name = sys.argv[1:]
b = open(donor, 'rb').read()
ks, ka, rs, ra, ss, sa, ta, ps = struct.unpack('<8I', b[8:40])
k = b[ps:ps+ks]
if k[:2] == b'\x1f\x8b':
    d = zlib.decompressobj(31); d.decompress(k); k = d.unused_data
lk = subprocess.run(['dtc', '-I', 'dtb', '-O', 'dts', f'{w}/out/build-lk2nd-msm8996/lk2nd/device/dts/msm8996/{name}.dtb'],
                    capture_output=True).stdout.decode()
nodes = ''.join(re.search(r'^\t' + n + r' \{\n.*?^\t\};\n', lk, re.S | re.M).group(0) for n in ('lk2nd', 'lk2nd-hw'))
out = b''; n = 0
for m in re.finditer(b'\xd0\x0d\xfe\xed', k):
    tot = struct.unpack('>I', k[m.start()+4:m.start()+8])[0]
    if not 100000 < tot < 2000000: continue
    src = subprocess.run(['dtc', '-I', 'dtb', '-O', 'dts', '-'], input=k[m.start():m.start()+tot], capture_output=True).stdout.decode()
    if re.search(r'^\tlk2nd \{', src, re.M): sys.exit("donor device trees already contain an lk2nd node")
    r = subprocess.run(['dtc', '-q', '-I', 'dts', '-O', 'dtb', '-p', '4096', '-'], input=(src + '\n/ {\n' + nodes + '};\n').encode(), capture_output=True)
    if r.returncode: sys.exit(r.stderr.decode()[:400])
    out += r.stdout; n += 1
if not n: sys.exit("no stock device trees found in donor image")
gz = zlib.compressobj(9, zlib.DEFLATED, 31)
open(w + '/kernel', 'wb').write(gz.compress(open(f'{w}/out/build-lk2nd-msm8996/lk.bin', 'rb').read()) + gz.flush() + out)
open(w + '/cmdline', 'w').write(b[64:576].rstrip(bytes(1)).decode())
open(w + '/osver', 'w').write(str(struct.unpack('<I', b[44:48])[0]))
print(f"merged lk2nd node into {n} stock device trees")
PY
python3 "$LK2ND/lk2nd/scripts/mkbootimg" --kernel "$W/kernel" --ramdisk "$RD" --cmdline "$(cat "$W/cmdline")" \
	--base 0x80000000 --kernel_offset 0x00008000 --ramdisk_offset 0x02200000 --tags_offset 0x02000000 \
	--second_offset 0x00f00000 --pagesize 4096 --output "$OUT"
python3 - "$OUT" "$(cat "$W/osver")" <<'PY'
import sys, struct
p, osv = sys.argv[1], int(sys.argv[2]); b = bytearray(open(p, 'rb').read())
struct.pack_into('<I', b, 28, 0); struct.pack_into('<I', b, 44, osv)   # second_addr, os_version as in stock
open(p, 'wb').write(b + b'SEANDROIDENFORCE')
print(f"{p}: {len(b)+16} bytes")
PY
