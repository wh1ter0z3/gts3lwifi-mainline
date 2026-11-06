#!/bin/sh
# Round 9: msm.ko with the MDP5 split-display patch, for the round 8 boot image.
# Run after building the module in the envkernel shell:
#   cd ~/Projects/samsung-tab-s3/linux && make -j$(nproc) modules
set -eu
P=$(cd "$(dirname "$0")" && pwd)
KO=$HOME/Projects/samsung-tab-s3/linux/.output/drivers/gpu/drm/msm/msm.ko
SRC=$HOME/Projects/samsung-tab-s3/linux/drivers/gpu/drm/msm/disp/mdp5/mdp5_ctl.c
OUT=$P/tester-round15
[ -f "$KO" ] || { echo "error: $KO not found" >&2; exit 1; }
[ "$KO" -nt "$SRC" ] || { echo "error: msm.ko is older than the mdp5 sources; rebuild with 'make -j\$(nproc) modules' in envkernel" >&2; exit 1; }
strings -a "$KO" | grep -q "split_start_both" && strings -a "$KO" | grep -q "split display v5" && strings -a "$KO" | grep -q "rgb_swap" && strings -a "$KO" | grep -q "enabling DSI test pattern" || { echo "error: msm.ko does not contain the split display code" >&2; exit 1; }
strings -a "$KO" | grep -m1 "^vermagic=" | grep -q "^vermagic=6.19.5 " || { echo "error: unexpected vermagic" >&2; exit 1; }
STRIP=$(command -v llvm-strip || ls /usr/lib/llvm-*/bin/llvm-strip 2>/dev/null | head -1 || true)
PKO=$HOME/Projects/samsung-tab-s3/linux/.output/drivers/gpu/drm/panel/panel-samsung-ams968hh01.ko
[ "$PKO" -nt "$HOME/Projects/samsung-tab-s3/linux/drivers/gpu/drm/panel/panel-samsung-ams968hh01.c" ] || { echo "error: panel module is older than its source; rebuild with 'make -j\$(nproc) modules'" >&2; exit 1; }
strings -a "$PKO" | grep -q "gamma_ref" || { echo "error: panel module lacks the mdnie code" >&2; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/msm" "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/panel"
"$STRIP" --strip-debug -o "$OUT/panel.ko" "$PKO" 2>/dev/null || cp "$PKO" "$OUT/panel.ko"
zstd -q -19 --rm "$OUT/panel.ko" -o "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/panel/panel-samsung-ams968hh01.ko.zst"
if [ -n "$STRIP" ]; then "$STRIP" --strip-debug -o "$OUT/msm.ko" "$KO"; else cp "$KO" "$OUT/msm.ko"; fi
zstd -q -19 --rm "$OUT/msm.ko" -o "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/msm/msm.ko.zst"
tar -C "$OUT" -czf "$OUT/msm-split-display-v10.tar.gz" lib
rm -rf "$OUT/lib"
cat > "$OUT/README-round15.txt" <<'TXT'
Round 15: DSI test pattern + hardware state dump. Modules v10 (msm + panel).
RAM boot only, same boot image as round 8 (boot-display-touch.img).
(If round 14's six images were not photographed yet, please do that too.)

1) Install:
     scp msm-split-display-v10.tar.gz test@172.16.42.1:
     ssh test@172.16.42.1
     sudo tar -C / -xzf msm-split-display-v10.tar.gz && sudo depmod 6.19.5 && sync && sync
     sudo reboot
   then from lk2nd (normal boot):
     fastboot boot boot-display-touch.img

2) After a minute, over ssh (copy the block whole):
     sudo mount -t debugfs none /sys/kernel/debug 2>/dev/null
     sudo sh -c 'cat /sys/kernel/debug/dri/0/state > ~test/drm-state.txt; cat /sys/kernel/debug/dri/0/kms > ~test/drm-kms.txt'
     echo 0 | sudo tee /sys/class/graphics/fbcon/cursor_blink
     echo 1 | sudo tee /sys/kernel/debug/dri/0/dsi_tpg
   The screen should now show the DSI controller's own test pattern (a
   checkered pattern) generated inside the DSI hardware, not by the
   display engine. Take a PHOTO of the whole screen.
   Then:
     sudo dmesg | grep -iE "msm|mdss|dsi|panel|ams968|drm|pp done|split|bonded|mdnie|readback|tpg" > display15.txt; sync

Send: the photo of the test pattern, display15.txt, drm-state.txt,
drm-kms.txt.
TXT
(cd "$OUT" && sha256sum msm-split-display-v10.tar.gz > SHA256SUMS && tar -xzOf msm-split-display-v10.tar.gz lib/modules/6.19.5/kernel/drivers/gpu/drm/msm/msm.ko.zst | sha256sum > MODULE-SHA256)
ZIP=$P/gts3lwifi-test-round15.zip; rm -f "$ZIP"
(cd "$OUT" && zip -q -9 "$ZIP" ./*)
ls -la "$OUT" "$ZIP"; echo "post this file: $ZIP"
