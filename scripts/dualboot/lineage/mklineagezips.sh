#!/bin/sh
# Makes the LineageOS add-on zip for OrangeFox (update-binary-data):
#   gts3-lineage-data.zip: LineageOS's /data on the layout's android_data
# and, with --adbdebug, the test-only one for a tablet without a screen
# (update-binary-adbdebug: adb on, no authorization; never in a release).
# usage: mklineagezips.sh <busybox (static aarch64)> <out dir> [--adbdebug]
set -eu
[ $# -ge 2 ] || { sed -n '2,6p' "$0" >&2; exit 1; }
BB=$1 OUT=$2 DBG=${3:-}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
python3 - "$HERE" "$BB" "$OUT" "$DBG" <<'PY'
import os, sys, zipfile
here, bb, out, dbg = sys.argv[1:]
def mk(name, ub, extra):
    path = os.path.join(out, name)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        def add(src, arc, mode):
            i = zipfile.ZipInfo(arc, date_time=(2026, 1, 1, 0, 0, 0))
            i.compress_type = zipfile.ZIP_DEFLATED
            i.external_attr = (0o100000 | mode) << 16
            z.writestr(i, open(src, "rb").read())
        add(os.path.join(here, ub), "META-INF/com/google/android/update-binary", 0o755)
        z.writestr("META-INF/com/google/android/updater-script", "# see update-binary\n")
        for src, arc, mode in extra:
            add(src, arc, mode)
    print(path, os.path.getsize(path), "bytes")
mk("gts3-lineage-data.zip", "update-binary-data",
   [(bb, "tools/busybox", 0o755), (os.path.join(here, "60-gts3-data.sh"), "addon.d/60-gts3-data.sh", 0o755)])
if dbg == "--adbdebug":
    mk("gts3-lineage-adbdebug-TEST-ONLY.zip", "update-binary-adbdebug", [])
PY
