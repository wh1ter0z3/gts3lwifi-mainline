#!/bin/sh
# Assembles the OneUI test package for the Tab S3 (standalone gsi-init, no
# pmOS on the tablet): boot images for both models, the vendor image and board
# files of a tested image directory, lk2nd, the installer (release/); and the
# TWRP installer zip of the same files (<out dir>-twrp.zip).
# usage: mkrelease.sh <kernel arch/arm64/boot dir> <payload dir> <lk2nd dir> <out dir>
#   payload: vendor.img (tools/mkvendor-standalone.sh), odm/, system-add/,
#            vendor-bind/, system-hide: as in the image directory tested
#   lk2nd:   lk2nd-gts3lwifi.img, lk2nd-gts3llte.img and their -odin.tar
# BUSYBOX as for mkgsiboot.sh.
set -eu
[ $# -eq 4 ] || { echo "usage: $0 <kernel boot dir> <payload dir> <lk2nd dir> <out dir>" >&2; exit 1; }
KB=$1 PL=$2 LK=$3 OUT=$4
HERE=$(cd "$(dirname "$0")/.." && pwd)
[ ! -e "$OUT" ] || { echo "$OUT exists" >&2; exit 1; }
mkdir -p "$OUT"

for m in apq8096-samsung-gts3lwifi msm8996-samsung-gts3llte; do
	WDT=1 sh "$HERE/tools/mkgsiboot.sh" "$KB/Image.gz" "$KB/dts/qcom/$m.dtb" - \
		"$OUT/boot-${m#*-samsung-}.img" | head -n 1
done
cp "$PL/vendor.img" "$OUT/"
for d in odm system-add vendor-bind; do [ ! -d "$PL/$d" ] || cp -R "$PL/$d" "$OUT/"; done
[ ! -f "$PL/system-hide" ] || cp "$PL/system-hide" "$OUT/"
find "$OUT" -name '*.pre-*' -exec rm -f {} +
for m in gts3lwifi gts3llte; do cp "$LK/lk2nd-$m.img" "$LK/lk2nd-$m-odin.tar" "$OUT/"; done
cp "$HERE/release/install-oneui.sh" "$HERE/release/README.txt" "$OUT/"
chmod 0755 "$OUT/install-oneui.sh"

# Nothing from Samsung or Qualcomm may be in it (the vendor image's free
# blocks included): no firmware image version strings anywhere
if LC_ALL=C grep -rlaE 'OEM_IMAGE_VERSION_STRING|QC_IMAGE_VERSION_STRING' "$OUT"; then
	echo "firmware found in the package (above): not for publishing" >&2
	exit 1
fi
(cd "$OUT" && find . -type f ! -name SHA256SUMS | sed 's|^\./||' | LC_ALL=C sort |
	xargs sha256sum > SHA256SUMS)
echo "package in $OUT ($(du -sh "$OUT" | cut -f1))"
# The same for TWRP / OrangeFox, without a PC (release/twrp, unzip64/)
sh "$HERE/tools/mktwrpzip.sh" "$OUT" "$HERE/unzip64/gsi-unzip" "$OUT-twrp.zip"
