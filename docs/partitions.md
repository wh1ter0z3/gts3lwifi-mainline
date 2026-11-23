# Internal storage: what goes where

`setup.sh` / `setup.ps1` (and the installer zip on its own in a recovery) give
what you install the end of the eMMC. Samsung's last four partitions are
contiguous and run to the end of the eMMC, the same on the SM-T820 and the
SM-T825:

| # | Samsung's | Start (sector) | Size |
|---|---|---|---|
| 44 | system | 1163264 | 4000 MiB |
| 45 | cache | 9355264 | 200 MiB |
| 46 | hidden | 9764864 | 40 MiB |
| 47 | userdata | 9846784 | the rest (24.4 GiB) |

Everything before them (bootloaders, `boot`, `recovery`, `misc`, `efs`,
`persist`, `modem`, ...) is firmware and is never touched, except `boot`
(lk2nd) and, with LineageOS, `recovery` (lk2nd).

## Samsung's partition table stays as it is

Samsung's bootloader write-protects the start of the eMMC (the first 8 MiB
write-protect group, with the GPT) whenever it starts anything but Download
mode. A write there fails with an I/O error (tested on the SM-T820,
2026-11-23), and only Odin can change the table. (The kernel's cache made
an `sgdisk` on the eMMC look successful until the next start; the backup
GPT at the end of the eMMC isn't protected, so the two copies then disagree.)

So our layout is a GPT of its own inside Samsung's partitions, as the dual
boot always had inside userdata. Going back to stock needs no partition
table: flash stock firmware with Odin.

## The layouts

| Install | Where our GPT is | Our partitions |
|---|---|---|
| Nura only | start of system, spanning system, cache, hidden, userdata (28.6 GiB) | gts3_boot 512 MiB, nura (~28 GiB) |
| Android only (our vendor + a GSI) | same | gts3_boot, android_system 4 GiB, android_vendor 1 GiB, android_data (the rest) |
| Nura and Android | same | gts3_boot, android_system, android_vendor, android_data 4 GiB, nura (the rest) |
| LineageOS only | start of userdata | gts3_boot, android_data (the rest) |
| Nura and LineageOS | start of userdata | gts3_boot, android_data 8 GiB, nura (the rest) |

- **LineageOS** (Awesometic's 18-21, Jayden's 24, other builds for the
  tablet) keeps Samsung's system, cache and hidden: its zip writes `system`
  and `boot` by name, and its fstab mounts `cache`. Its /data is
  android_data, through the add-on in [scripts/dualboot/lineage](../scripts/dualboot/lineage)
  (a dm-linear device made at boot), which the installer runs right after
  LineageOS's own installer. lk2nd moves to the recovery partition; what was
  there is copied to the PC first (`gts3-recovery-before-<model>-<serial>.img.gz`).
  A plain start boots LineageOS; Power + Volume Up + Home, or "reboot
  recovery", gives lk2nd's menu (Nura, OrangeFox). See
  [dual-boot.md](dual-boot.md), "LineageOS next to Nura".
- **Our Android** boots through lk2nd's `android` label; gsi-init finds its
  partitions by name in our GPT (`gsi.disk=system` for the layout in system).
- Sizes: `SYSTEM`, `VENDOR`, `DATA` (our Android's data next to Nura),
  `LOS_DATA` (LineageOS's data next to Nura): installer environment
  variables, in M or G.
- If Samsung's last four partitions aren't as above (a Treble repartition, a
  custom PIT), or with `GTS3_NESTED=1`, our layouts go in userdata too.

## Who reads our GPT

Everything looks for a GPT header at the start of Samsung's `system`
partition first, then of `userdata`:

- **lk2nd** reads a table inside a partition only as MBR: a hybrid MBR there
  (entry 0 = gts3_boot) points it at the boot partition.
- **Nura's initramfs** (`20-gts3-dualboot.sh`, device package): reads the GPT
  through a read-only loop device on the eMMC from there on, then maps each
  partition with dm-linear onto Samsung's partitions under it, one segment
  per partition it spans. Not onto the whole eMMC: that claims the whole
  eMMC, and no other partition (`efs`, with the Bluetooth address) could be
  mounted. dm-linear passes the eMMC's inline encryption engine through, so
  fscrypt with `inlinecrypt` still encrypts in hardware.
- **OrangeFox** (`gts3-dualboot-fox`): loop devices on the eMMC at the same
  offsets, `/dev/block/gts3/<name>`. On the layout in system, Samsung's
  System and Cache go from its list (they hold Nura) and /data is
  android_data or nothing, so no wipe can reach Nura. On an SM-T825 it also
  reports the device as `gts3llte`, so LineageOS zips for the T825 pass
  their device check.
- **gsi-init** (our Android): a loop device on the eMMC from the
  partition's start, with its partitions.

## The installer

1. Checks Samsung's last four partitions; frees them (unmounts, detaches old
   loop devices).
2. Clears the start of the other place (userdata or system), so an older
   layout there isn't found.
3. Writes our GPT and the hybrid MBR through a loop device over exactly the
   layout's area (system's start to userdata's end), then a loop device per
   partition.
4. Boot partition, Nura's root, Android's images, as before. Nura's root is
   unpacked with the zip's own xz (XZ Utils 5.8, static, `twrp/mkxz.sh`): it
   unpacks the image's blocks on all four cores, about 5 times busybox's
   xzcat, so the eMMC is the limit (about a minute for the root, against 17).
5. With LineageOS: its installer from its zip, then the /data add-on.
6. lk2nd v2 last: into `boot`, or into `recovery` with LineageOS.

## Tested (2026-11-23, SM-T820)

Nura only, from setup.sh, on a tablet that had the dual boot layout in
userdata: lk2nd finds gts3_boot, Nura's root is `/dev/mapper/gts3-nura`
(27.1 GiB, `inlinecrypt`), efs mounts, Bluetooth gets its address, kernel r18
and device r25 install on top (the initramfs keeps the hook).

Not yet on a tablet: the Android and LineageOS layouts, setup.ps1.
