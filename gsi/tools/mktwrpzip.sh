#!/bin/sh
# Makes the TWRP / OrangeFox installer zip from a release package directory
# (tools/mkrelease.sh): the boot images, lk2nd images, vendor image and board
# files, gsi-unzip (unzip64/) and release/twrp/update-binary.
# usage: mktwrpzip.sh <package dir> <gsi-unzip binary> <out.zip>
set -eu
[ $# -eq 3 ] || { echo "usage: $0 <package dir> <gsi-unzip> <out.zip>" >&2; exit 1; }
PKG=$1 UNZIP=$2 OUT=$3
HERE=$(cd "$(dirname "$0")/.." && pwd)
python3 - "$PKG" "$UNZIP" "$HERE/release/twrp/update-binary" "$OUT" <<'PY'
import hashlib, os, sys, zipfile
pkg, unzip, ub, out = sys.argv[1:]
files = []
for top in ['boot-gts3lwifi.img', 'boot-gts3llte.img', 'lk2nd-gts3lwifi.img', 'lk2nd-gts3llte.img',
            'vendor.img', 'system-hide', 'README.txt', 'odm', 'system-add', 'vendor-bind']:
    p = os.path.join(pkg, top)
    if os.path.isdir(p):
        for d, _, fs in os.walk(p):
            files += [os.path.relpath(os.path.join(d, f), pkg) for f in sorted(fs)]
    elif os.path.isfile(p):
        files.append(top)
sums = []
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, allowZip64=False) as z:
    def add(src, name, mode):
        i = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
        i.compress_type = zipfile.ZIP_DEFLATED
        i.external_attr = (0o100000 | mode) << 16
        data = open(src, 'rb').read()
        z.writestr(i, data)
        return data
    add(ub, 'META-INF/com/google/android/update-binary', 0o755)
    z.writestr('META-INF/com/google/android/updater-script', '# see update-binary\n')
    for f in files:
        data = add(os.path.join(pkg, f), f, 0o644)
        sums.append('%s  %s\n' % (hashlib.sha256(data).hexdigest(), f))
    data = add(unzip, 'tools/gsi-unzip', 0o755)
    sums.append('%s  tools/gsi-unzip\n' % hashlib.sha256(data).hexdigest())
    z.writestr('SHA256SUMS', ''.join(sums))
print('%s: %d files' % (out, len(files) + 3))
PY
