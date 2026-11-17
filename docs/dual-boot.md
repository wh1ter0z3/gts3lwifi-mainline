# Dual boot: Android and Nura

Goal: one tablet with both systems, switchable at any time, without a PC.
Android runs on the open Android 15 vendor with any GSI. Nura is
postmarketOS with any UI.

## How it boots

lk2nd stays where it is (first 4 MiB of the boot partition). It reads
`extlinux/extlinux.conf` from an ext2 partition and boots its `default`
label. Each system is one label:

```
default nura

label nura
	linux /gts3/nura/Image.gz
	initrd /gts3/nura/initramfs
	fdt /gts3/nura/gts3.dtb
	append console=tty0 ... pmos_boot_uuid=<uuid> pmos_root_uuid=<uuid>

label android
	linux /gts3/android/Image.gz
	initrd /gts3/android/initramfs
	fdt /gts3/android/gts3.dtb
	append console=tty0 ... gsi.dir=<dir>
```

Switching is a change of the `default` line:

- From either system: `gts3-boot` lists the labels, and
  `gts3-boot android reboot` (or `nura`) switches and restarts
  ([scripts/dualboot/gts3-boot](../scripts/dualboot/gts3-boot)). In Android it
  mounts the partition labelled `pmOS_boot` itself (root, busybox `findfs`).
- On the tablet: hold Volume Down while lk2nd starts. Its menu now lists
  the labels first.
- From a computer, in lk2nd's fastboot: `fastboot oem boot-label android`.

If a label chosen from the menu or from fastboot can't be booted, lk2nd
reboots, so the tablet comes back up in the default label.

## Two lk2nd limits, and what changed

**1. lk2nd only reads MBR partition tables.** The boot partition is nested
in userdata, and that table is GPT, so lk2nd never saw it. The fix is a
hybrid MBR in userdata's first sector:

- entry 0 is the boot partition (type 0x83);
- entry 1 is the 0xEE GPT entry, so Linux, gsi-init and sfdisk still use
  the GPT.

[scripts/dualboot/hybrid-mbr.py](../scripts/dualboot/hybrid-mbr.py)
`<userdata device> [--write]` writes it and prints the old and new bytes.
Back up the sector first.

**2. lk2nd-v1 limits the kernel to 32 MiB.** It loads kernels at a fixed
32 MiB slot, but ours are 34.5 MiB including BSS (`image_size` 0x2280000).
v1 refused with "Kernel too big: 35383808 > 33554432".

The v2 patches ([lk2nd/v2](../lk2nd/v2), on upstream fd380ac63) fix this
and add the switching:

| Patch | What it does |
|---|---|
| 0001, 0002 | Split-display cont-splash refresh. Not tested on a panel yet |
| 0003 | Tab S3 device trees |
| 0004 | USB through the MUIC at high speed only, as in v1 |
| 0005 | 70 MiB boot window for msm8996, 0x80000000 to 0x84600000. Linux reserves nothing below 0x84700000 here, and lk2nd maps 88 MiB from the start of DDR |
| 0006 | Labels in the menu, and `fastboot oem boot-label` |

Upstream lk2nd also places the DTB and initramfs after the kernel instead
of at fixed offsets.

[scripts/dualboot/pack-lk2nd-from-v1.py](../scripts/dualboot/pack-lk2nd-from-v1.py)
`<lk2nd-v1.img> <lk.bin> <mkbootimg> <out.img>` packs a build for S-Boot. It
keeps everything from the released v1 image (stock device trees with the
lk2nd node, header, cmdline, trailer) and swaps only the gzip'd lk.bin.
Repacking v1's own lk.bin this way gives v1's kernel section byte for byte.

Build:

```
make BOOTLOADER_OUT=out TOOLCHAIN_PREFIX=arm-none-eabi- \
	LK2ND_PARTITION_SIZE="4*1024*1024" lk2nd-msm8996
```

## Tested on the SM-T820 (2026-11-17, v2 started from v1 with `fastboot boot`)

- `default nura`: Nura boots from internal storage. /proc/cmdline is the
  label's own.
- `default android`: Android (AOSP 16 GSI on the vendor) boots to
  `sys.boot_completed=1`.
- No bootable label: lk2nd falls back to fastboot, USB works, and
  `fastboot boot` of RAM images works.
- `fastboot oem boot-label nope` fails cleanly. `oem boot-label android`
  boots Android.
- `gts3-boot` in Android and in Nura: list, switch, and Android → Nura with
  `reboot`.
- With the hybrid MBR, Linux still reads the GPT (p2 is 23.9 GiB, not the
  MBR's 1 MiB).

Not tested yet:

- v2 started by S-Boot itself (that needs v2 flashed);
- the menu on a screen;
- the cont-splash patches on a panel.

## Layout for users (planned)

Inside userdata, GPT plus the hybrid MBR entry for the first partition:

| boot (ext2) | system 8G | vendor 1G | Android data | Nura | Shared (exFAT) |

- boot holds extlinux.conf and both kernels.
- gsi-init gets a partition mode next to today's image files.
- The installer (PC and OrangeFox) creates the layout, writes the hybrid
  MBR and extlinux.conf, and flashes lk2nd v2.
