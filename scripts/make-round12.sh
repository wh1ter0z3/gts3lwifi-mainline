#!/bin/sh
# Round 9: msm.ko with the MDP5 split-display patch, for the round 8 boot image.
# Run after building the module in the envkernel shell:
#   cd ~/Projects/samsung-tab-s3/linux && make -j$(nproc) modules
set -eu
P=$(cd "$(dirname "$0")" && pwd)
KO=$HOME/Projects/samsung-tab-s3/linux/.output/drivers/gpu/drm/msm/msm.ko
SRC=$HOME/Projects/samsung-tab-s3/linux/drivers/gpu/drm/msm/disp/mdp5/mdp5_ctl.c
OUT=$P/tester-round12
[ -f "$KO" ] || { echo "error: $KO not found" >&2; exit 1; }
[ "$KO" -nt "$SRC" ] || { echo "error: msm.ko is older than the mdp5 sources; rebuild with 'make -j\$(nproc) modules' in envkernel" >&2; exit 1; }
strings -a "$KO" | grep -q "split_start_both" && strings -a "$KO" | grep -q "split display v5" && strings -a "$KO" | grep -q "rgb_swap" || { echo "error: msm.ko does not contain the split display code" >&2; exit 1; }
strings -a "$KO" | grep -m1 "^vermagic=" | grep -q "^vermagic=6.19.5 " || { echo "error: unexpected vermagic" >&2; exit 1; }
STRIP=$(command -v llvm-strip || ls /usr/lib/llvm-*/bin/llvm-strip 2>/dev/null | head -1 || true)
PKO=$HOME/Projects/samsung-tab-s3/linux/.output/drivers/gpu/drm/panel/panel-samsung-ams968hh01.ko
[ "$PKO" -nt "$HOME/Projects/samsung-tab-s3/linux/drivers/gpu/drm/panel/panel-samsung-ams968hh01.c" ] || { echo "error: panel module is older than its source; rebuild with 'make -j\$(nproc) modules'" >&2; exit 1; }
strings -a "$PKO" | grep -q "colmod" || { echo "error: panel module lacks the mdnie code" >&2; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/msm" "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/panel"
"$STRIP" --strip-debug -o "$OUT/panel.ko" "$PKO" 2>/dev/null || cp "$PKO" "$OUT/panel.ko"
zstd -q -19 --rm "$OUT/panel.ko" -o "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/panel/panel-samsung-ams968hh01.ko.zst"
if [ -n "$STRIP" ]; then "$STRIP" --strip-debug -o "$OUT/msm.ko" "$KO"; else cp "$KO" "$OUT/msm.ko"; fi
zstd -q -19 --rm "$OUT/msm.ko" -o "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/msm/msm.ko.zst"
tar -C "$OUT" -czf "$OUT/msm-split-display-v8.tar.gz" lib
rm -rf "$OUT/lib"
cat > "$OUT/README-round12.txt" <<'TXT'
Round 12: colour experiments, all chosen from the boot command line.
RAM boot only, same boot image as round 8 (boot-display-touch.img).
Modules v8 (msm + panel).

1) Install once:
     scp msm-split-display-v8.tar.gz test@172.16.42.1:
     ssh test@172.16.42.1
     sudo tar -C / -xzf msm-split-display-v8.tar.gz && sudo depmod 6.19.5 && sync && sync
     sudo reboot

2) Colour order experiments. Boot with each of these in turn (one boot
   each). After boot, run the colour test (below) and note the colours for
   RED, GREEN, BLUE, WHITE. A photo of each fill is even better.

   Test A (reference, no change):
     fastboot boot boot-display-touch.img
   Test B (BGR order):
     fastboot --cmdline "console=tty0 msm.rgb_swap=2 pmos_boot_uuid=6a690fa9-50e7-4341-a1c4-a2da8bd45995 pmos_root_uuid=6c62b2bd-62ef-48c9-8231-73857f873a6a pmos_rootfsopts=defaults" boot boot-display-touch.img
   Test C (GBR order):
     fastboot --cmdline "console=tty0 msm.rgb_swap=5 pmos_boot_uuid=6a690fa9-50e7-4341-a1c4-a2da8bd45995 pmos_root_uuid=6c62b2bd-62ef-48c9-8231-73857f873a6a pmos_rootfsopts=defaults" boot boot-display-touch.img
   Test D (BRG order):
     fastboot --cmdline "console=tty0 msm.rgb_swap=3 pmos_boot_uuid=6a690fa9-50e7-4341-a1c4-a2da8bd45995 pmos_root_uuid=6c62b2bd-62ef-48c9-8231-73857f873a6a pmos_rootfsopts=defaults" boot boot-display-touch.img
   Test E (24 bpp pixel format command sent to the panel):
     fastboot --cmdline "console=tty0 panel_samsung_ams968hh01.colmod=0x77 pmos_boot_uuid=6a690fa9-50e7-4341-a1c4-a2da8bd45995 pmos_root_uuid=6c62b2bd-62ef-48c9-8231-73857f873a6a pmos_rootfsopts=defaults" boot boot-display-touch.img

   Colour test (red, green, blue, white, 4 s each):
     for c in '\x00\x00\xff\x00' '\x00\xff\x00\x00' '\xff\x00\x00\x00' '\xff\xff\xff\x00'; do printf "$c" > /tmp/px; cp /tmp/px /tmp/buf; for i in $(seq 1 22); do cat /tmp/buf /tmp/buf > /tmp/buf2 && mv /tmp/buf2 /tmp/buf; done; sudo sh -c 'head -c 12582912 /tmp/buf > /dev/fb0'; sleep 4; done

   If one of B/C/D shows red, green, blue, white correctly, stop there and
   tell us which one. (Values 1 and 4 also exist if none of these match.)

3) Once, from Test A, over ssh:
     sudo dmesg | grep -iE "msm|mdss|dsi|panel|ams968|drm|pp done|split|bonded|mdnie|dspp" > display12.txt; sync

Send display12.txt and the colour results per test.
TXT
(cd "$OUT" && sha256sum msm-split-display-v8.tar.gz > SHA256SUMS && tar -xzOf msm-split-display-v8.tar.gz lib/modules/6.19.5/kernel/drivers/gpu/drm/msm/msm.ko.zst | sha256sum > MODULE-SHA256)
ZIP=$P/gts3lwifi-test-round12.zip; rm -f "$ZIP"
(cd "$OUT" && zip -q -9 "$ZIP" ./*)
ls -la "$OUT" "$ZIP"; echo "post this file: $ZIP"
