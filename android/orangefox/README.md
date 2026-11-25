# OrangeFox for the Galaxy Tab S3 (SM-T820, SM-T825)

OrangeFox Recovery (`fox_12.1`) for the Galaxy Tab S3, made for the Treble
layout of the gts3lwifi-mainline project: system and vendor partitions that a
GSI and our open vendor image are flashed to by name. It works on the stock
layout too (there is just no vendor partition to show).

From awesometic's TWRP device trees
([gts3lwifi](https://github.com/awesometic/android_device_samsung_gts3lwifi-twrp),
[gts3llte](https://github.com/awesometic/android_device_samsung_gts3llte-twrp),
Apache-2.0) and his prebuilt kernels (Linux 3.18.140, source:
[android_kernel_samsung_msm8996](https://github.com/awesometic/android_kernel_samsung_msm8996)).
What's different:

- **Vendor partition**: Backup, Wipe and Install Image ("Vendor Image") know it,
  and "System Image" takes a GSI's system.img.
- **Open source only**: no decryption, so none of his Samsung/Qualcomm
  libraries or `qseecomd`. Our Android's /data isn't encrypted.
- **lk2nd stays safe**: the boot partition (lk2nd at its start, Android's boot
  image 4 MiB in) is backed up but never flashed whole, the Magisk addon and
  OrangeFox's boot image patching are left out.
- **Ramdisk under 12 MiB**: S-Boot can't load a bigger one (it copies the
  ramdisk to 0x83400000, and 0x84000000 is protected), so the image is LZMA
  compressed and leaves out bash, nano and the CJK font of the extra
  languages. Extras kept: GNU tar and sed, xz.

## Layout

```
common/     BoardConfigCommon.mk, recovery.fstab (Android format: /misc),
            recovery/root (init; system/etc/twrp.flags: the partition list)
gts3lwifi/  SM-T820: product, board config, prebuilt kernel
gts3llte/   SM-T825: the same
tools/      screen.sh: the recovery's screen on the PC over adb;
            faketap.c: a touchscreen and keys for a board without a panel
patches/    for the OrangeFox source tree (bootable_recovery: bootable/recovery)
```

## Building

The OrangeFox sync tools, then this tree at `device/samsung/gts3l-recovery`:

```bash
git clone https://gitlab.com/OrangeFox/sync.git
cd sync && ./orangefox_sync.sh --branch 12.1 --path /path/to/fox_12.1
cd /path/to/fox_12.1
git clone https://github.com/wh1ter0z3/gts3lwifi-mainline /tmp/gts3lwifi-mainline
cp -a /tmp/gts3lwifi-mainline/android/orangefox device/samsung/gts3l-recovery
git -C bootable/recovery am "$PWD"/device/samsung/gts3l-recovery/patches/bootable_recovery/*.patch
export ALLOW_MISSING_DEPENDENCIES=true LC_ALL=C FOX_BUILD_DEVICE=gts3lwifi
source build/envsetup.sh
lunch twrp_gts3lwifi-eng          # twrp_gts3llte-eng for the SM-T825
mka adbd recoveryimage
```

The image: `out/target/product/gts3lwifi/OrangeFox-*.img`.

The patches let the GUI's DRM backend (minuitwrp) drive mainline msm, for
OrangeFox on our mainline kernel: it was written for Qualcomm's SDE driver,
split the screen across two planes and set a zpos mainline doesn't take, so
the kernel refused every commit and only the kernel console showed. The third
turns touches with the picture: TW_ROTATION rotates only what is drawn, and
the touchscreen reports in the panel's landscape frame, so taps landed
elsewhere and the lock screen couldn't be swiped open.

An incremental build doesn't always refresh the ramdisk: libraries are
copied into `recovery/root` only when it is made, and OrangeFox's own
changes to it (such as leaving out the Magisk entries) run only with it.
After changing a library, copy it from `system/lib64` to
`recovery/root/system/lib64` and delete
`obj/PACKAGING/recovery_intermediates/ramdisk_files-timestamp` and
`ramdisk-recovery.*` before building again.

## Installing

- From TWRP or OrangeFox: Install → Install Image → the .img → Recovery.
- From a PC in download mode (Power + Volume Down + Home):
  `heimdall flash --RECOVERY OrangeFox-...img`, or Odin with the image in a
  tar (`tar -H ustar -cf recovery.tar recovery.img`) in the AP slot.
- Start it: Power + Volume Up + Home, or `adb reboot recovery` from our
  Android. Not lk2nd's menu → Recovery: that starts the recovery's 3.18
  kernel from lk2nd, which resets.

## Seeing it without a screen

There is no scrcpy in a recovery: scrcpy streams from Android's own screen
and encoder, which a recovery doesn't run. `tools/screen.sh` takes a
screenshot about once a second over adb (`fb2png`) and keeps it in a PNG that
an image viewer reloads; on our mainline kernel (DRM, no /dev/graphics/fb0)
fb2png gives a black picture. The recovery's own screenshot (Power + Volume
Down) works: it saves what is drawn to /sdcard/Fox/screenshots, in the
panel's landscape frame (turn it 90° clockwise to see it upright). Commands
work without a screen too: `adb shell twrp help` (install, wipe, backup,
restore, mount, print).

Touch without a panel: `tools/faketap.c` (build it static with the NDK's
`aarch64-linux-android30-clang -static`, push it to /tmp) makes a uinput
touchscreen with the real one's frame (2048 x 1536, the panel's) and keys:
`faketap 1000 50 swipe 2000 767 1000 767 1800 593 shot` wakes the screen,
swipes the lock screen open, taps Wipe and takes a screenshot. Portrait
(x, y) is panel (y, 1535 - x). The first event after the screen has blanked
only wakes it.

## Testing order

1. SM-T820 bare board (the developer's): starts, adb, partitions listed,
   backup of boot/efs to the microSD card, screenshots.
2. Stock layout and Treble layout, on the T820 first.
3. A tester with a screen: touch, rotation, brightness, then the T825.
