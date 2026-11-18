#!/bin/sh
# Makes the OrangeFox / TWRP dual boot installer zip (twrp/update-binary):
# the payload of `install-dualboot.sh --payload` except Nura's root (users
# put nura-root.simg next to the zip), lk2nd v2 for each model there is one
# of, a static aarch64 busybox (Alpine's busybox-static) and, for GSI zips
# over 4 GB, gsi-unzip (gsi branch, gsi/unzip64).
# usage: mkdualbootzip.sh <payload dir> <lk2nd dir> <busybox> <gsi-unzip|-> <out.zip>
#   lk2nd dir: lk2nd-gts3lwifi.img and/or lk2nd-gts3llte.img (v2)
set -eu
[ $# -eq 5 ] || { sed -n '2,9p' "$0" >&2; exit 1; }
PAY=$1 LK=$2 BB=$3 UNZ=$4 OUT=$5
HERE=$(cd "$(dirname "$0")" && pwd)
[ -f "$PAY/nura-boot.tar" ] && [ -f "$PAY/nura.env" ] || { echo "no payload in $PAY" >&2; exit 1; }
python3 - "$PAY" "$LK" "$BB" "$UNZ" "$HERE/update-binary" "$OUT" <<'PY'
import hashlib, os, sys, zipfile
pay, lk, bb, unz, ub, out = sys.argv[1:]
files = [(os.path.join(pay, f), f) for f in ("nura-boot.tar", "nura.env")]
if os.path.isdir(os.path.join(pay, "android")):
    files += [(os.path.join(pay, "android", f), "android/" + f)
              for f in ("vendor.img", "Image.gz", "gts3.dtb", "initramfs", "cmdline")]
for m in ("gts3lwifi", "gts3llte"):
    p = os.path.join(lk, f"lk2nd-{m}.img")
    if os.path.isfile(p):
        files.append((p, f"lk2nd-{m}.img"))
tools = [(bb, "tools/busybox")] + ([(unz, "tools/gsi-unzip")] if unz != "-" else [])
sums = []
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, allowZip64=False) as z:
    def add(src, name, mode):
        i = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
        i.compress_type = zipfile.ZIP_DEFLATED
        i.external_attr = (0o100000 | mode) << 16
        h = hashlib.sha256()
        with open(src, "rb") as f, z.open(i, "w") as w:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
                w.write(chunk)
        return h.hexdigest()
    add(ub, "META-INF/com/google/android/update-binary", 0o755)
    z.writestr("META-INF/com/google/android/updater-script", "# see update-binary\n")
    for src, name in files:
        sums.append(f"{add(src, name, 0o644)}  {name}\n")
    for src, name in tools:
        sums.append(f"{add(src, name, 0o755)}  {name}\n")
    z.writestr("SHA256SUMS", "".join(sums))
print(out, os.path.getsize(out), "bytes;", len(files) + len(tools), "files")
PY
