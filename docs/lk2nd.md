# lk2nd on the Galaxy Tab S3

The files in `lk2nd/` are the device entries for lk2nd's source tree
(`lk2nd/device/dts/msm8996/`). lk2nd builds fine with them, but Samsung's
S-Boot is picky about how the resulting boot image is packed.

## What is known (tested on an SM-T820)

| Packing | Result |
|---|---|
| lk2nd's default `lk2nd.img` (load addresses from base 0x0) | does not boot |
| Stock addresses, but only lk2nd's own small device tree appended | does not boot |
| Stock addresses + gzip'd `lk.bin` + the four stock Samsung device trees, ramdisk and cmdline of an Android boot image | **boots**, lk2nd shows "Unknown (FIXME!)" because the stock device trees have no lk2nd node |

Stock values: base 0x80000000, kernel +0x8000, ramdisk +0x2200000,
tags +0x2000000, second 0, page size 4096, `SEANDROIDENFORCE` trailer.

With the booting variant, lk2nd falls back to its default keys (Volume Down
works). Its image is larger than the 512 KiB lk2nd reserves in the boot
partition, so **never use `fastboot flash boot`** with it - it overwrites
lk2nd. `fastboot boot` is safe.

## Reproducible packing (not yet confirmed on hardware)

`scripts/pack-lk2nd-samsung.sh <donor-boot.img> <out.img>` builds lk2nd with a
4 MiB reserved area, merges the lk2nd node into the stock device trees taken
from any boot image for this tablet, and packs it like the booting variant
with an empty ramdisk. If S-Boot accepts it, lk2nd identifies the tablet,
uses the correct keys and `fastboot flash boot` becomes safe.

Keys: Volume Up = PM8994 GPIO 3, Home = PM8994 GPIO 2, Volume Down = PON RESIN.

Flashing without a display, from TWRP over adb (boot is mmcblk0p28):

    adb push lk2nd.img /tmp/lk2nd.img
    adb shell dd if=/tmp/lk2nd.img of=/dev/block/mmcblk0p28
    adb reboot
    fastboot devices        # lk2nd answers here if it booted

Recovery: boot TWRP again (Power + Volume Up + Home) and write the previous
image back the same way, or flash from Download mode with Odin/heimdall.
