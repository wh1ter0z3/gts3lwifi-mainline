#!/bin/sh
# Round 9: msm.ko with the MDP5 split-display patch, for the round 8 boot image.
# Run after building the module in the envkernel shell:
#   cd ~/Projects/samsung-tab-s3/linux && make -j$(nproc) modules
set -eu
P=$(cd "$(dirname "$0")" && pwd)
KO=$HOME/Projects/samsung-tab-s3/linux/.output/drivers/gpu/drm/msm/msm.ko
SRC=$HOME/Projects/samsung-tab-s3/linux/drivers/gpu/drm/msm/disp/mdp5/mdp5_ctl.c
OUT=$P/tester-round9c
[ -f "$KO" ] || { echo "error: $KO not found" >&2; exit 1; }
[ "$KO" -nt "$SRC" ] || { echo "error: msm.ko is older than the mdp5 sources; rebuild with 'make -j\$(nproc) modules' in envkernel" >&2; exit 1; }
strings -a "$KO" | grep -q "split_start_both" && strings -a "$KO" | grep -q "split dbg" || { echo "error: msm.ko does not contain the split display code" >&2; exit 1; }
strings -a "$KO" | grep -m1 "^vermagic=" | grep -q "^vermagic=6.19.5 " || { echo "error: unexpected vermagic" >&2; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/msm"
STRIP=$(command -v llvm-strip || ls /usr/lib/llvm-*/bin/llvm-strip 2>/dev/null | head -1 || true)
if [ -n "$STRIP" ]; then "$STRIP" --strip-debug -o "$OUT/msm.ko" "$KO"; else cp "$KO" "$OUT/msm.ko"; fi
zstd -q -19 --rm "$OUT/msm.ko" -o "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/msm/msm.ko.zst"
tar -C "$OUT" -czf "$OUT/msm-split-display-v3.tar.gz" lib
rm -rf "$OUT/lib"
cat > "$OUT/README-round9c.txt" <<'TXT'
Round 9c: split display, diagnostic build (v3). RAM boot only.
Boot image: the one from round 8 (boot-display-touch.img), not included again. It prints a few "split dbg" lines in
dmesg that tell us whether the second DSI link is getting frames.

IMPORTANT: run "sync" before every reboot/power cut, otherwise files
written just before (like the dmesg dumps) end up empty.

1) Boot boot-display-touch.img, then over ssh:
     scp msm-split-display-v3.tar.gz test@172.16.42.1:
     ssh test@172.16.42.1
     sudo tar -C / -xzf msm-split-display-v3.tar.gz && sudo depmod 6.19.5 && sync
   Reboot into lk2nd (sync first) and boot the same image again:
     fastboot boot boot-display-touch.img

2) Over ssh, after about a minute:
     sudo dmesg | grep -iE "msm|mdss|dsi|panel|ams968|drm|pp done|split|bonded" > display9c.txt; sync
   Send display9c.txt and say what the screen did (photo welcome).
TXT
(cd "$OUT" && sha256sum msm-split-display-v3.tar.gz > SHA256SUMS)
ZIP=$P/gts3lwifi-test-round9c.zip; rm -f "$ZIP"
(cd "$OUT" && zip -q -9 "$ZIP" ./*)
ls -la "$OUT" "$ZIP"; echo "post this file: $ZIP"
