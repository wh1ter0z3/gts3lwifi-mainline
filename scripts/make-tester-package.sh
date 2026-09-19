#!/bin/sh
# Bundle round-1 test files for a remote tester.
# Run after: pmbootstrap build --force linux-postmarketos-qcom-msm8996 && pmbootstrap install
set -eu
P=$(cd "$(dirname "$0")" && pwd)
EXPORT=/tmp/postmarketOS-export
OUT=$P/tester-round1
pmbootstrap export "$EXPORT" >/dev/null
[ -e "$EXPORT/boot.img" ] || { echo "error: boot.img not found; run pmbootstrap install first" >&2; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT"
cp -L "$EXPORT/boot.img" "$OUT/boot.img"
cp "$P/lk2nd/lk2nd.img" "$P/lk2nd/lk2nd-odin.tar" "$P/lk2nd/lk2nd-wipe-odin.tar" "$P/TESTING.md" "$OUT/"
(cd "$OUT" && sha256sum boot.img lk2nd.img lk2nd-odin.tar lk2nd-wipe-odin.tar > SHA256SUMS)
ZIP=$P/gts3lwifi-test-round1.zip; rm -f "$ZIP"
(cd "$OUT" && zip -q -9 "$ZIP" ./*)
ls -la "$ZIP"; echo "post this file: $ZIP"
