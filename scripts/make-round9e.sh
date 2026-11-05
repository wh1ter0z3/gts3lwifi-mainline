#!/bin/sh
# Round 9: msm.ko with the MDP5 split-display patch, for the round 8 boot image.
# Run after building the module in the envkernel shell:
#   cd ~/Projects/samsung-tab-s3/linux && make -j$(nproc) modules
set -eu
P=$(cd "$(dirname "$0")" && pwd)
KO=$HOME/Projects/samsung-tab-s3/linux/.output/drivers/gpu/drm/msm/msm.ko
SRC=$HOME/Projects/samsung-tab-s3/linux/drivers/gpu/drm/msm/disp/mdp5/mdp5_ctl.c
OUT=$P/tester-round9e
[ -f "$KO" ] || { echo "error: $KO not found" >&2; exit 1; }
[ "$KO" -nt "$SRC" ] || { echo "error: msm.ko is older than the mdp5 sources; rebuild with 'make -j\$(nproc) modules' in envkernel" >&2; exit 1; }
strings -a "$KO" | grep -q "split_start_both" && strings -a "$KO" | grep -q "split display v5" || { echo "error: msm.ko does not contain the split display code" >&2; exit 1; }
strings -a "$KO" | grep -m1 "^vermagic=" | grep -q "^vermagic=6.19.5 " || { echo "error: unexpected vermagic" >&2; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/msm"
STRIP=$(command -v llvm-strip || ls /usr/lib/llvm-*/bin/llvm-strip 2>/dev/null | head -1 || true)
if [ -n "$STRIP" ]; then "$STRIP" --strip-debug -o "$OUT/msm.ko" "$KO"; else cp "$KO" "$OUT/msm.ko"; fi
zstd -q -19 --rm "$OUT/msm.ko" -o "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/msm/msm.ko.zst"
tar -C "$OUT" -czf "$OUT/msm-split-display-v5.tar.gz" lib
rm -rf "$OUT/lib"
cat > "$OUT/README-round9e.txt" <<'TXT'
Round 9e: split display v5. The round 9d log was identical to 9c, so the
new module may not have survived the reboot. This build prints its version
("split display v5") so we can tell. RAM boot only, same boot image as
round 8 (boot-display-touch.img).

1) Boot boot-display-touch.img, then over ssh:
     scp msm-split-display-v5.tar.gz test@172.16.42.1:
     ssh test@172.16.42.1
     sudo tar -C / -xzf msm-split-display-v5.tar.gz && sudo depmod 6.19.5 && sync && sync
     sha256sum /lib/modules/6.19.5/kernel/drivers/gpu/drm/msm/msm.ko.zst
   (compare with MODULE-SHA256 in this folder; they must match)
   Then reboot cleanly, do not just cut power:
     sudo reboot
   and boot the same image again from lk2nd:
     fastboot boot boot-display-touch.img

2) Over ssh, after about a minute:
     sha256sum /lib/modules/6.19.5/kernel/drivers/gpu/drm/msm/msm.ko.zst
     sudo dmesg | grep -iE "msm|mdss|dsi|panel|ams968|drm|pp done|split|bonded" > display9e.txt; sync
   Send display9e.txt, the sha256sum output, and what the screen did.
TXT
(cd "$OUT" && sha256sum msm-split-display-v5.tar.gz > SHA256SUMS && tar -xzOf msm-split-display-v5.tar.gz lib/modules/6.19.5/kernel/drivers/gpu/drm/msm/msm.ko.zst | sha256sum > MODULE-SHA256)
ZIP=$P/gts3lwifi-test-round9e.zip; rm -f "$ZIP"
(cd "$OUT" && zip -q -9 "$ZIP" ./*)
ls -la "$OUT" "$ZIP"; echo "post this file: $ZIP"
