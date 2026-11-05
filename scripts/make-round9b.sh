#!/bin/sh
# Round 9: msm.ko with the MDP5 split-display patch, for the round 8 boot image.
# Run after building the module in the envkernel shell:
#   cd ~/Projects/samsung-tab-s3/linux && make -j$(nproc) modules
set -eu
P=$(cd "$(dirname "$0")" && pwd)
KO=$HOME/Projects/samsung-tab-s3/linux/.output/drivers/gpu/drm/msm/msm.ko
SRC=$HOME/Projects/samsung-tab-s3/linux/drivers/gpu/drm/msm/disp/mdp5/mdp5_ctl.c
OUT=$P/tester-round9b
[ -f "$KO" ] || { echo "error: $KO not found" >&2; exit 1; }
[ "$KO" -nt "$SRC" ] || { echo "error: msm.ko is older than the mdp5 sources; rebuild with 'make -j\$(nproc) modules' in envkernel" >&2; exit 1; }
strings -a "$KO" | grep -q "split_start_both" && strings -a "$KO" | grep -q "split display, master" || { echo "error: msm.ko does not contain the split display code" >&2; exit 1; }
strings -a "$KO" | grep -m1 "^vermagic=" | grep -q "^vermagic=6.19.5 " || { echo "error: unexpected vermagic" >&2; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/msm"
STRIP=$(command -v llvm-strip || ls /usr/lib/llvm-*/bin/llvm-strip 2>/dev/null | head -1 || true)
if [ -n "$STRIP" ]; then "$STRIP" --strip-debug -o "$OUT/msm.ko" "$KO"; else cp "$KO" "$OUT/msm.ko"; fi
zstd -q -19 --rm "$OUT/msm.ko" -o "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/msm/msm.ko.zst"
tar -C "$OUT" -czf "$OUT/msm-split-display-v2.tar.gz" lib
rm -rf "$OUT/lib"
cp "$P/tester-round8/boot-display-touch.img" "$OUT/"
cat > "$OUT/README-round9b.txt" <<'TXT'
Round 9b: second try of the split display fix. RAM boot only.
Same boot image as round 8/9; only msm.ko is new again (v2).

1) Boot boot-display-touch.img, then over ssh:
     scp msm-split-display-v2.tar.gz test@172.16.42.1:
     ssh test@172.16.42.1
     sudo tar -C / -xzf msm-split-display-v2.tar.gz && sudo depmod 6.19.5 && sync
   Reboot into lk2nd and boot the same image again:
     fastboot boot boot-display-touch.img
   Watch the screen (photo/video welcome).

2) Over ssh, after about a minute:
     sudo dmesg | grep -iE "msm|mdss|dsi|panel|ams968|drm|pp done|split|bonded" > display9b2.txt
   If text is visible on the screen, also run the colour test from round 9
   (red, green, blue) and tell us the colours you saw.

Send display9b2.txt and what the screen did.
TXT
(cd "$OUT" && sha256sum boot-display-touch.img msm-split-display-v2.tar.gz > SHA256SUMS)
ZIP=$P/gts3lwifi-test-round9b.zip; rm -f "$ZIP"
(cd "$OUT" && zip -q -9 "$ZIP" ./*)
ls -la "$OUT" "$ZIP"; echo "post this file: $ZIP"
