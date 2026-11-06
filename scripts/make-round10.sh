#!/bin/sh
# Round 9: msm.ko with the MDP5 split-display patch, for the round 8 boot image.
# Run after building the module in the envkernel shell:
#   cd ~/Projects/samsung-tab-s3/linux && make -j$(nproc) modules
set -eu
P=$(cd "$(dirname "$0")" && pwd)
KO=$HOME/Projects/samsung-tab-s3/linux/.output/drivers/gpu/drm/msm/msm.ko
SRC=$HOME/Projects/samsung-tab-s3/linux/drivers/gpu/drm/msm/disp/mdp5/mdp5_ctl.c
OUT=$P/tester-round10
[ -f "$KO" ] || { echo "error: $KO not found" >&2; exit 1; }
[ "$KO" -nt "$SRC" ] || { echo "error: msm.ko is older than the mdp5 sources; rebuild with 'make -j\$(nproc) modules' in envkernel" >&2; exit 1; }
strings -a "$KO" | grep -q "split_start_both" && strings -a "$KO" | grep -q "split display v5" && strings -a "$KO" | grep -q "dpu_msm8996_cfg\|DPU" || { echo "error: msm.ko does not contain the split display code" >&2; exit 1; }
strings -a "$KO" | grep -m1 "^vermagic=" | grep -q "^vermagic=6.19.5 " || { echo "error: unexpected vermagic" >&2; exit 1; }
STRIP=$(command -v llvm-strip || ls /usr/lib/llvm-*/bin/llvm-strip 2>/dev/null | head -1 || true)
PKO=$HOME/Projects/samsung-tab-s3/linux/.output/drivers/gpu/drm/panel/panel-samsung-ams968hh01.ko
[ "$PKO" -nt "$HOME/Projects/samsung-tab-s3/linux/drivers/gpu/drm/panel/panel-samsung-ams968hh01.c" ] || { echo "error: panel module is older than its source; rebuild with 'make -j\$(nproc) modules'" >&2; exit 1; }
strings -a "$PKO" | grep -q "mdnie_bypass" || { echo "error: panel module lacks the mdnie code" >&2; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/msm" "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/panel"
"$STRIP" --strip-debug -o "$OUT/panel.ko" "$PKO" 2>/dev/null || cp "$PKO" "$OUT/panel.ko"
zstd -q -19 --rm "$OUT/panel.ko" -o "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/panel/panel-samsung-ams968hh01.ko.zst"
if [ -n "$STRIP" ]; then "$STRIP" --strip-debug -o "$OUT/msm.ko" "$KO"; else cp "$KO" "$OUT/msm.ko"; fi
zstd -q -19 --rm "$OUT/msm.ko" -o "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/msm/msm.ko.zst"
tar -C "$OUT" -czf "$OUT/msm-split-display-v6.tar.gz" lib
rm -rf "$OUT/lib"
cat > "$OUT/README-round10.txt" <<'TXT'
Round 10: two display drivers to try. RAM boot only, same boot image as
round 8 (boot-display-touch.img). Two modules are new: msm.ko (v6) and the panel driver (colour fix).

A) Install the module (same as before, but check the checksum and reboot
   cleanly, never cut the power):
     scp msm-split-display-v6.tar.gz test@172.16.42.1:
     ssh test@172.16.42.1
     sudo tar -C / -xzf msm-split-display-v6.tar.gz && sudo depmod 6.19.5 && sync && sync
     sha256sum /lib/modules/6.19.5/kernel/drivers/gpu/drm/msm/msm.ko.zst
   (must match MODULE-SHA256 in this folder)
     sudo reboot

B) Test 1, MDP5 driver (v5 fix):
     fastboot boot boot-display-touch.img
   After a minute, over ssh:
     sha256sum /lib/modules/6.19.5/kernel/drivers/gpu/drm/msm/msm.ko.zst
     sudo dmesg | grep -iE "msm|mdss|dsi|panel|ams968|drm|pp done|split|bonded|dpu" > display10-mdp5.txt; sync
     sudo reboot

C) Test 2, DPU driver (the newer display driver, selected by cmdline):
     fastboot --cmdline "console=tty0 msm.prefer_mdp5=0 pmos_boot_uuid=6a690fa9-50e7-4341-a1c4-a2da8bd45995 pmos_root_uuid=6c62b2bd-62ef-48c9-8231-73857f873a6a pmos_rootfsopts=defaults" boot boot-display-touch.img
   After a minute, over ssh:
     sudo dmesg | grep -iE "msm|mdss|dsi|panel|ams968|drm|pp done|split|bonded|dpu" > display10-dpu.txt; sync

Send display10-mdp5.txt, display10-dpu.txt, the sha256sum output, and
PHOTOS (not video) of the screen in each test: one of the console text,
and one of each colour fill. Colour test (red, green, blue, 4 s each):
  for c in '\x00\x00\xff\x00' '\x00\xff\x00\x00' '\xff\x00\x00\x00'; do printf "$c" > /tmp/px; cp /tmp/px /tmp/buf; for i in $(seq 1 22); do cat /tmp/buf /tmp/buf > /tmp/buf2 && mv /tmp/buf2 /tmp/buf; done; sudo sh -c 'head -c 12582912 /tmp/buf > /dev/fb0'; sleep 4; done
TXT
(cd "$OUT" && sha256sum msm-split-display-v6.tar.gz > SHA256SUMS && tar -xzOf msm-split-display-v6.tar.gz lib/modules/6.19.5/kernel/drivers/gpu/drm/msm/msm.ko.zst | sha256sum > MODULE-SHA256)
ZIP=$P/gts3lwifi-test-round10.zip; rm -f "$ZIP"
(cd "$OUT" && zip -q -9 "$ZIP" ./*)
ls -la "$OUT" "$ZIP"; echo "post this file: $ZIP"
