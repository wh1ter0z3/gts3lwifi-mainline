#!/bin/sh
# Round 9: msm.ko with the MDP5 split-display patch, for the round 8 boot image.
# Run after building the module in the envkernel shell:
#   cd ~/Projects/samsung-tab-s3/linux && make -j$(nproc) modules
set -eu
P=$(cd "$(dirname "$0")" && pwd)
KO=$HOME/Projects/samsung-tab-s3/linux/.output/drivers/gpu/drm/msm/msm.ko
SRC=$HOME/Projects/samsung-tab-s3/linux/drivers/gpu/drm/msm/disp/mdp5/mdp5_ctl.c
OUT=$P/tester-round9
[ -f "$KO" ] || { echo "error: $KO not found" >&2; exit 1; }
[ "$KO" -nt "$SRC" ] || { echo "error: msm.ko is older than the mdp5 sources; rebuild with 'make -j\$(nproc) modules' in envkernel" >&2; exit 1; }
strings -a "$KO" | grep -q "split_start_both" || { echo "error: msm.ko does not contain the split display code" >&2; exit 1; }
strings -a "$KO" | grep -m1 "^vermagic=" | grep -q "^vermagic=6.19.5 " || { echo "error: unexpected vermagic" >&2; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/msm"
STRIP=$(command -v llvm-strip || ls /usr/lib/llvm-*/bin/llvm-strip 2>/dev/null | head -1 || true)
if [ -n "$STRIP" ]; then "$STRIP" --strip-debug -o "$OUT/msm.ko" "$KO"; else cp "$KO" "$OUT/msm.ko"; fi
zstd -q -19 --rm "$OUT/msm.ko" -o "$OUT/lib/modules/6.19.5/kernel/drivers/gpu/drm/msm/msm.ko.zst"
tar -C "$OUT" -czf "$OUT/msm-split-display.tar.gz" lib
rm -rf "$OUT/lib"
cp "$P/tester-round8/boot-display-touch.img" "$OUT/"
cat > "$OUT/README-round9.txt" <<'TXT'
Round 9: display fix attempt (MDP5 split display). RAM boot only.
Same boot image as round 8; only the msm.ko kernel module is new.

1) Boot boot-display-touch.img (fastboot boot ...), then over ssh install
   the new module (it replaces the one from the round 7 modules tarball):
     scp msm-split-display.tar.gz test@172.16.42.1:
     ssh test@172.16.42.1
     sudo tar -C / -xzf msm-split-display.tar.gz && sudo depmod 6.19.5 && sync
   Reboot into lk2nd and boot the same image again:
     fastboot boot boot-display-touch.img
   Watch the screen: the console text should now be readable, in the
   right proportions, with white text on black. Video or photo welcome.

2) Over ssh:
     sudo dmesg | grep -iE "msm|mdss|dsi|panel|ams968|drm|pp done|split|bonded" > display9.txt
   Then the colour test (fills the screen red, then green, then blue):
     for c in '\x00\x00\xff\x00' '\x00\xff\x00\x00' '\xff\x00\x00\x00'; do
       printf "$c" > /tmp/px; cp /tmp/px /tmp/buf
       for i in $(seq 1 22); do cat /tmp/buf /tmp/buf > /tmp/buf2 && mv /tmp/buf2 /tmp/buf; done
       sudo sh -c 'head -c 12582912 /tmp/buf > /dev/fb0'; sleep 4
     done
   Tell us which colour appears each time (expected: red, green, blue).

3) If the picture is still wrong or the screen stays black, try once more
   with the START signal written on both controllers:
     fastboot --cmdline "console=tty0 msm.split_start_both=1 pmos_boot_uuid=6a690fa9-50e7-4341-a1c4-a2da8bd45995 pmos_root_uuid=6c62b2bd-62ef-48c9-8231-73857f873a6a pmos_rootfsopts=defaults" boot boot-display-touch.img
   and send display9b.txt made the same way as in step 2.

Send display9.txt (and display9b.txt if used), the colour results and
photos/video of the screen.
TXT
(cd "$OUT" && sha256sum boot-display-touch.img msm-split-display.tar.gz > SHA256SUMS)
ZIP=$P/gts3lwifi-test-round9.zip; rm -f "$ZIP"
(cd "$OUT" && zip -q -9 "$ZIP" ./*)
ls -la "$OUT" "$ZIP"; echo "post this file: $ZIP"
