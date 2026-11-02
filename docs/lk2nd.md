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

## Reproducible packing (confirmed on hardware, 2026-09-22)

`scripts/pack-lk2nd-samsung.sh <donor-boot.img> <out.img>` builds lk2nd with a
4 MiB reserved area, merges the lk2nd node into the stock device trees taken
from any boot image for this tablet, and packs it like the booting variant
with an empty ramdisk. Tested on an SM-T820: boots to the lk2nd menu, keys,
reboot and power-off work. Released as `lk2nd-gts3lwifi.img` (Odin tar:
`lk2nd-gts3lwifi-odin.tar`).

With this image `fastboot flash boot` is safe: lk2nd keeps the first 4 MiB of
the boot partition for itself and stores the OS image behind it, so pmOS can
be installed normally and boots without a PC.

Known limitation: the "Recovery" menu entry only reboots. Use the key combo
(Power + Volume Up + Home) to reach the stock recovery / TWRP instead.

Keys: Volume Up = PM8994 GPIO 3, Home = PM8994 GPIO 2, Volume Down = PON RESIN.

Flashing without a display, from TWRP over adb (boot is mmcblk0p28):

    adb push lk2nd.img /tmp/lk2nd.img
    adb shell dd if=/tmp/lk2nd.img of=/dev/block/mmcblk0p28
    adb reboot
    fastboot devices        # lk2nd answers here if it booted

Recovery: boot TWRP again (Power + Volume Up + Home) and write the previous
image back the same way, or flash from Download mode with Odin/heimdall.

## Split-display autorefresh (lk2nd/0001-cont-splash-autorefresh-split-display.patch)

`lk2nd.pass-simplefb=autorefresh` assumed the stock bootloader had already
programmed the ping-pong tear check and only topped up its vsync counter on
ping-pong 0. Samsung's S-Boot never configures tear check (PP0/PP1
SYNC_CONFIG_VSYNC read back as 0) and this panel is a split display with
two control paths in command mode (CTL_0 TOP 0x21f20, CTL_1 TOP 0x21f30),
so Linux's framebuffer was never re-sent: writing /dev/fb0 changed nothing.

The patch programs the whole tear-check block (internal ~60 Hz vsync
counter, no TE input) plus autorefresh on ping-pong 0 and, when control
path 1 is in command mode, on ping-pong 1, then kicks CTL_0 and, unless
the split-display link (SPLIT_DPL_EN) is set, CTL_1. It also enables the
MDSS vsync clock (`mdss_vsync_clk`, MMSS 0x8c2328) that clocks the
ping-pong vsync counter; a bootloader that never uses tear check leaves it
off, and v3 (which lacked this) changed nothing on hardware.
Registers read on the device: SPLIT_DPL_EN = 1, splash on VIG0 and VIG1
both reading 0x9a401000, DMA pipes unused. `lk2nd-samsung-gts3lwifi-v4.img`
carries the patch; untested at the time of writing.

## LTE model (SM-T825, gts3llte)

Same packing with the LTE donor's four stock device trees (Rev00-Rev05,
msm-id 246, board-id 8/x) and `LK2ND_DTB=msm8996-samsung-gts3llte`:
`LK2ND_DTB=msm8996-samsung-gts3llte scripts/pack-lk2nd-samsung.sh <t825-boot.img> lk2nd-gts3llte.img`
(donor used: LineageOS 21 gts3llte boot.img). Built 2026-10-22, not yet
tested on hardware. Checksums in `lk2nd/lk2nd-gts3llte.sha256`.

## Preview release (2026-11-02)

`scripts/preview/lk2nd-images.py` builds both models from upstream lk2nd at the commit in
`scripts/preview/lock.json`, plus `lk2nd/preview-base.patch` (the SM-T820 entry, high-speed-only
USB and the MUIC switch) and, for the SM-T825, `lk2nd/preview-t825.patch`.

| Image | State |
|---|---|
| `lk2nd-gts3lwifi.img` | sha256 `008ae3e9...`, on our SM-T820 since 2026-10-28. A rebuild has the same lk.bin and device trees; only the empty ramdisk's timestamp differs |
| `lk2nd-gts3llte.img` | New. The SM-T825 entry now has the MUIC switch and high-speed-only USB: the stock SM-T825 device tree has the MUIC on the same bus and address as the SM-T820, and TLMM 32 and 33 aren't reserved. Not run on hardware. The older `lk2nd-gts3llte.img` above has neither |

## Installed pmOS behind lk2nd (2026-10-28)

The 4 MiB lk2nd image is in the boot partition (mmcblk0p28, 76 MiB) and the
pmOS boot.img sits behind it at offset 4 MiB, so the tablet boots pmOS
without a PC. Written with lk2nd's fastboot:

    fastboot flash boot boot.img

Read back from Linux afterwards: the first 2375696 bytes still equal
`lk2nd-gts3lwifi.img`, and the 23834624 bytes at 4 MiB equal the flashed
image.

Use the boot.img that the device itself generated (`/boot/boot.img`, copy it
off with scp). `pmbootstrap flasher flash_kernel` flashes the boot.img from
pmbootstrap's rootfs chroot, which is only refreshed by `pmbootstrap
install`; after a `pmbootstrap sideload` it is out of date.

After a kernel or deviceinfo update on the device, mkinitfs regenerates
/boot/boot.img but the copy behind lk2nd stays old. Flash the new one again
(fastboot as above, or from the running system):

    sudo dd if=/boot/boot.img of=/dev/mmcblk0p28 bs=1M seek=4 conv=fsync

To get to lk2nd's fastboot for test images: hold Volume Down while lk2nd
starts, or, without touching the tablet, clear the stored image and reboot;
lk2nd then finds nothing to boot and stays in fastboot:

    sudo dd if=/dev/zero of=/dev/mmcblk0p28 bs=1M seek=4 count=1 conv=fsync

`reboot bootloader` is not a reliable way back: the reset goes through
Samsung's S-Boot first, which may treat that reason as Download mode.
