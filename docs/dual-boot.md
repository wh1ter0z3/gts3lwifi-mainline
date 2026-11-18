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
  ([gts3-boot](../pmaports/device-samsung-gts3lwifi/gts3-boot), /usr/bin in
  Nura from device package r15). In Android it
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

## Tested on the SM-T820 (2026-11-17)

v2 is flashed over v1 on our SM-T820: the first 2375696 bytes of the boot
partition, read back with matching sha256 4b4f3941…. S-Boot starts it, and
without a computer:

- a plain reboot boots Nura;
- `gts3-boot android reboot` in Nura boots Android to `sys.boot_completed=1`;
- `gts3-boot nura reboot` in Android boots Nura.

Before flashing, with v2 started from v1 by `fastboot boot`:

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

- the menu on a screen;
- the cont-splash patches on a panel.

For a headless tablet, a label with no kernel is a way into fastboot for RAM
boot tests: `gts3-boot fastboot reboot`, then `fastboot boot <img>` or
`fastboot oem boot-label nura`. Our SM-T820 has one:

```
label fastboot
	linux /gts3/no-kernel
```

## The layout

[scripts/dualboot/gts3-layout](../scripts/dualboot/gts3-layout) `<disk> [yes]`
makes it. The disk is the userdata partition (through a loop device, so the
layout is nested in it), a microSD card, or an image file. It **erases** the
disk.

| # | GPT name | Content | Default size |
|---|---|---|---|
| 1 | gts3_boot | ext2 `pmOS_boot`: extlinux.conf, Nura's kernel (`/vmlinuz`, updated by Nura itself), Android's in `/gts3/android` | 512M |
| 2 | android_system | raw: the GSI's system image | 8G |
| 3 | android_vendor | raw: our vendor image | 1G |
| 4 | android_data | ext4 `android_data`: /data, and gsi-init's `.gts3` (keep, logs, board files) | 6G |
| 5 | nura | ext4 `pmOS_root` | 7G |
| 6 | shared | exFAT `Shared`, for both | the rest |

Sizes are set with `BOOT= SYSTEM= VENDOR= DATA= NURA=`. `SHARED=no` leaves
the shared partition out and gives nura the rest: the microSD card is then
the storage both systems share (our SM-T820). The script also
writes the hybrid MBR entry for lk2nd in plain shell (no Python, so it also
runs in a recovery). It matches hybrid-mbr.py byte for byte.

The last partition ends exactly at the last usable sector. With a nested GPT, pmOS's
initramfs runs `parted resizepart 2 100%` at boot whenever there is free
space at the end. Partition 2 is android_system here, and parted refuses
("Can't have overlapping partitions", tested), but without a free tail it
never tries.

### Nura in the layout: the initramfs hook

pmOS's initramfs finds a GPT nested in userdata ("subpartitions") only if it
has **exactly two** partitions, and only while the root hasn't been found
(`mount_subpartitions()`: `part_count -eq 2`). The layout has five or six,
so Nura wouldn't find its root.

The device package (r15) installs a mkinitfs hook,
[20-gts3-dualboot.sh](../pmaports/device-samsung-gts3lwifi/20-gts3-dualboot.sh),
in `/usr/share/mkinitfs/hooks/`. It runs before `mount_subpartitions()` and
attaches userdata with partition scanning (`losetup -Pf`) when:
- the boot partition (`pmos_boot_uuid=`) isn't there;
- userdata's GPT has a `gts3_boot` partition.

pmOS then finds the root and boot partitions by UUID as usual. A plain pmOS
install (two partitions) is left to `mount_subpartitions()`, which also grows
its root at the first boot.

### Android in the layout

Android uses the layout through gsi-init's `gsi.parts=1` (gsi branch
dc08080). It finds the three partitions by name and attaches android_data to
loop7 once, so /data and `.gts3` share one device and the vendor's fstab
stays as it is. `gsi.disk=mmcblk1` points it at a microSD card, for tests
(the Android DTB has the SD slot). lk2nd can't read the microSD ("SD card
MMC is unavailable"), so it always boots from the boot partition inside
userdata.

## One clock for both systems

The PMIC's RTC keeps counting while the tablet is off, but nothing sets it:
- on our SM-T820 it read 1970-01-08 (days since its last reset);
- rtc-pm8xxx refuses to write it without `allow-set-time`;
- **with** `allow-set-time` the write hung the tablet until the watchdog reset
  it, so never set that property. Samsung's own Android doesn't write it
  either.

So before 2026-11-18:
- Android ran in 1970;
- Nura came up at the time it was last shut down (systemd's saved clock);
- they disagreed and fell behind with every reboot.

Now one offset, real time minus RTC, is kept in `/gts3/clock` on the shared
boot partition:
- **Nura:** [gts3-clock](../pmaports/device-samsung-gts3lwifi/gts3-clock)
  (device package r15):
  - `gts3-clock.service` sets the time from it early at boot and stores it at
    shutdown;
  - `gts3-clock-sync.path` stores it once NTP has set the time.
  It only stores a time that was loaded or synced at this boot, never
  systemd's fallback.
- **Android:** gsi-init (`gsi.parts=1`) sets the time from it before
  Android's init. A clock keeper stores it again when Android's time is set
  (more than 2 s off from where it started: network time, or by hand). It
  never stores a time before 2026-09.

Tested on the SM-T820:

| Test | Clock afterwards |
|---|---|
| Nura rebooted | within 1 s of the PC |
| Nura → Android | within 1 s |
| Android set an hour ahead, → Nura | Nura +3599 s |
| Nura set right again, → Android | within 1 s |

The timezone is each system's own setting (Nura: `timedatectl`; Android:
Settings, or `persist.sys.timezone`). The stored time is UTC.

## The microSD card: shared storage

On our SM-T820 the card is one exFAT partition labelled `Shared`.
- Nura mounts it (kernel exfat; `exfatprogs` to format).
- Android's vold takes it as portable storage.
The layout on internal storage then has no shared partition (`SHARED=no`).

## Migrating an installed Nura (our SM-T820, 2026-11-18)

Nura's root can't be shrunk while it runs, so the microSD card held a
staging copy. The whole move worked without touching anything until step 3:

1. rsync of the root, a tar of the boot partition, and the Android image
   files, onto the microSD (one ext4).
2. Booted Nura from that copy: an extlinux entry with its UUID as
   `pmos_root_uuid=`. It needs the hook, since the boot partition is still
   on userdata.
3. From the copy: gts3-layout on userdata (`SHARED=no BOOT=512M SYSTEM=4G
   VENDOR=1G DATA=4G`, nura 14.9G). Boot and root were made again with their
   old UUIDs (`mke2fs -U`), so Nura's fstab and the extlinux entries stay as
   they are.
4. The boot files went back from the tar, and the root back with rsync.
5. dd of the Android system and vendor images into their partitions.
   userdata.img went into android_data and was grown with resize2fs, so
   Android kept its data.
6. Nura booted from the layout (root on the nura partition, hook in the
   initramfs).
7. Android booted from its partitions: `boot_completed` after 1 minute.
8. `gts3-boot` switched both ways.

The script is specific to that tablet (its UUIDs and paths), so it isn't in
the repository. Every test boot was a one-off: the default label was a
no-kernel `fastboot` entry, and the test was booted with `fastboot oem
boot-label`. A failure then left the tablet in fastboot instead of in a loop.

Also found on the way: an initramfs that mkinitfs made from that tablet's
months-old packages (kernel 6.19.5) hung before USB came up with the 7.3.1
test kernel. That tablet boots the old initramfs with the hook appended as a
second cpio archive. A current install builds the hook in.

## LineageOS (or any other ROM) from OrangeFox

Flashing a ROM zip the normal way **ends the dual boot and erases Nura**:

- The ROM writes its boot image over the whole boot partition. lk2nd lives in
  its first 4 MiB, so it's gone, and S-Boot starts the ROM directly.
- The ROM uses the userdata partition as its /data. Our layout lives there,
  so the ROM can't mount it, and the "Format Data" it needs erases Nura,
  Android and Shared.
- Our Android and Nura read the Wi-Fi board file (bdwlan30.bin) from
  Samsung's system partition. A ROM replaces that partition.

Next to Nura, a ROM can only go in through our installer. It would:

- keep lk2nd and put the ROM's kernel in an extlinux entry (or behind lk2nd);
- point the ROM's /data at a partition of the layout;
- copy the firmware it needs out of the system partition first.

The ROM's updates would need the same treatment (an addon.d script). That's
the "repack every update" cost from the group's poll. Our own Android
doesn't have it: its kernel is in `/gts3/android`, and a GSI or vendor
update doesn't touch it.

## Installers

Both make the same install, on an erased userdata partition:
- the layout;
- Nura from its release image, keeping the boot and root UUIDs, with the
  initramfs hook built in;
- optionally our Android (GSI + vendor);
- lk2nd v2.

**PC:** [scripts/dualboot/install-dualboot.sh](../scripts/dualboot/install-dualboot.sh)
builds the userdata image here and flashes it with lk2nd's fastboot.
`--image` only builds it. Tested that way on 2026-11-18 (the layout, files,
UUIDs and hybrid MBR were right); not flashed to a tablet yet.

**OrangeFox / TWRP:** [scripts/dualboot/twrp/update-binary](../scripts/dualboot/twrp/update-binary).
`install-dualboot.sh --payload DIR` writes its inputs, and
[mkdualbootzip.sh](../scripts/dualboot/twrp/mkdualbootzip.sh) packs the zip.
nura-root.simg (with its .sha256) goes next to the zip, and so does the GSI
if wanted (named `*system*` or `*gsi*`). In the zip:
- the layout is made with the recovery's sgdisk (long options only, since
  Android's build has no short ones);
- the partitions are written through loop devices, with their nodes made
  from /sys if ueventd hasn't;
- the big files stream from the zip (/tmp is RAM in a recovery);
- it runs under its own busybox.

The payload drops `orphan_file` from Nura's root: the recoveries' e2fsprogs
(1.45) refuse a file system with it.

**Tested on 2026-11-18 in the mainline OrangeFox on our SM-T820**, with a
9 GB image file as userdata and a file as the boot partition:
- Nura plus a stand-in GSI;
- the layout, the hybrid MBR and both extlinux entries;
- Nura's UUIDs, its root grown and clean;
- the vendor image byte for byte, the system image, android_data;
- lk2nd v2.

**OrangeFox's dual boot mode** (recovery tree, `gts3-dualboot-fox`): when
userdata holds the layout, its partitions get loop devices and **/data is
android_data**. Format Data and Android's factory reset then wipe Android's
data only. Nura and the Android images are listed for backups. Works in
the mainline OrangeFox on the SM-T820: all five partitions, /data and
/sdcard on android_data, the exFAT microSD mounted.

**OrangeFox on our mainline kernel** (recovery tree, `GTS3_KERNEL=mainline`):
an extlinux entry with the Android kernel, its own ramdisk and a device tree
that carries `ro.boot.*` in /firmware/android. An `androidboot` on the
command line makes lk2nd append S-Boot's command line. It needs:
- 28 modules in the ramdisk;
- memfd instead of ashmem (`ro.vndk.version`, `sys.use_memfd`);
- vkms on a board without panel (`gts3.vkms=1`).

It runs on our bare SM-T820, where the 3.18 OrangeFox resets after a few
minutes, with adb, the GUI on vkms, the dual boot mode and the exFAT card.
USB is adb only: mainline has no MTP gadget function.
