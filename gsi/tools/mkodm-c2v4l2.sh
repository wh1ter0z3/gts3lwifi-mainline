#!/bin/sh
# Makes the <dir>/odm overlay with a Codec2 V4L2 service (AOSP external/v4l2_codec2):
# the Venus H.264 encoder for MediaCodec (scrcpy, screen recording).
#
# The service and its libraries come from GloDroid's Raspberry Pi 4 build
# (Android 15, a VNDK-less vendor: its libraries come along and stay apart, in
# /odm/lib64/c2v4l2, for this one service); our init rc, VINTF fragment and
# codec list from odm-c2v4l2/; the vendor's own codec lists from the GloDroid
# msm8916 vendor image (our media_codecs.xml in /odm/etc replaces its one).
# Wants kernel patches 0194 and 0195 (RGBA input under the name the component
# asks for, bitstream buffers for dma-bufs), and an Android 15+ system image.
#
# usage: mkodm-c2v4l2.sh <gd-aosp-gd_rpi4-*.tar.xz> <glodroid msm8916 vendor.img> <out dir>
# then copy <out dir>/. to the tablet's <gsi dir>/odm/
# needs: tar, xz, python3, debugfs, readelf; about 2.5 GB of disk next to
# <out dir> for a while (the super image), little memory: xz runs on one
# thread, only the vendor partition is unpacked, and never on a tmpfs
set -eu
RPI=${1:?usage: mkodm-c2v4l2.sh <gd_rpi4 tar.xz> <msm8916 vendor.img> <out dir>}
GDV=${2:?vendor.img}
OUT=${3:?out dir}
HERE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$OUT"
T=$(cd "$OUT/.." && pwd)/.mkodm-c2v4l2.$$
mkdir "$T"
trap 'rm -rf "$T"' EXIT
if [ "$(stat -f -c %T "$T")" = tmpfs ]; then
	echo "$T is on a tmpfs (memory): put <out dir> on a disk" >&2
	exit 1
fi
LOW="nice -n 19 ionice -c 3"

SVC=android.hardware.media.c2@1.0-service-v4l2
$LOW tar --use-compress-program='xz -d -T1' -xf "$RPI" -C "$T" ./super.img
mkdir "$T/parts"
$LOW python3 "$HERE/tools/lpunpack.py" "$T/super.img" "$T/parts" vendor_a >/dev/null
rm "$T/super.img"
mkdir "$T/v"
debugfs -R "rdump /bin /lib64 $T/v" "$T/parts/vendor_a.img" >/dev/null 2>&1 || :
rm -r "$T/parts"
[ -f "$T/v/bin/hw/$SVC" ] || { echo "no $SVC in the vendor image" >&2; exit 1; }

mkdir -p "$OUT/bin/hw" "$OUT/lib64/c2v4l2"
cp "$T/v/bin/hw/$SVC" "$OUT/bin/hw/"
# the service's libraries: everything it needs that the vendor has (the rest
# is LLNDK, from the system image)
python3 - "$T/v/lib64" "$OUT/bin/hw/$SVC" "$OUT/lib64/c2v4l2" <<'EOF'
import os, re, shutil, subprocess, sys
lib, start, out = sys.argv[1:]
def needed(p):
    d = subprocess.run(['readelf', '-dW', p], capture_output=True, text=True).stdout
    return re.findall(r'\(NEEDED\)\s+Shared library: \[(.+?)\]', d)
seen, todo = set(), [start]
while todo:
    for n in needed(todo.pop()):
        p = os.path.join(lib, n)
        if n not in seen and os.path.exists(p):
            seen.add(n)
            shutil.copy(p, out)
            todo.append(p)
print(f"{len(seen)} libraries")
EOF
chmod 0755 "$OUT/bin/hw/$SVC"

cp -r "$HERE/odm-c2v4l2/etc" "$OUT/"
for f in media_codecs_ffmpeg_c2.xml media_codecs_google_c2_audio.xml media_codecs_google_c2_video.xml; do
	debugfs -R "dump /etc/$f $OUT/etc/$f" "$GDV" >/dev/null 2>&1
	[ -s "$OUT/etc/$f" ] || { echo "no /etc/$f in $GDV" >&2; exit 1; }
done
echo "done: $OUT"
