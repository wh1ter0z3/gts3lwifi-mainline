# Treble layout: system and vendor partitions on the Tab S3

Design, 2026-11-16. Nothing here has been run on a tablet yet.

The Tab S3 shipped with Android 7 and has no `vendor` partition. Until now
our images are files on the userdata partition, loop-mounted by gsi-init
(the same idea as Valera1978's 2018 Treble project, which kept the files in
the system partition). This makes the tablet a plain Treble device: a GSI's
`system.img` goes to the `system` partition, our `vendor.img` to `vendor`,
both flashable by name from fastboot (lk2nd) or the recovery.

## The stock table (SM-T820, read from the tablet 2026-11-16)

29.12 GiB eMMC, 61071360 sectors of 512 bytes, GPT, 47 partitions. All but
the last four are firmware, bootloader and data partitions that this change
leaves byte for byte as they are (efs, modem, apnhlos, persist, boot,
recovery, ...). The last four are contiguous and end the disk:

| # | Name | Start | End | Sectors | Size |
|---|---|---|---|---|---|
| 44 | system | 1163264 | 9355263 | 8192000 | 3.9 GiB |
| 45 | cache | 9355264 | 9764863 | 409600 | 200 MiB |
| 46 | hidden | 9764864 | 9846783 | 81920 | 40 MiB |
| 47 | userdata | 9846784 | 61071326 | 51224543 | 24.4 GiB |

The whole table: `t820-table.txt` (no per-device identifiers). The SM-T825's
has to be read the same way before the zip supports it.

## The new table

Same start, same order apart from the new partition; every start on a 1 MiB
boundary:

| # | Name | Start | End | Sectors | Size |
|---|---|---|---|---|---|
| 44 | system | 1163264 | 17940479 | 16777216 | 8 GiB |
| 48 | vendor | 17940480 | 20037631 | 2097152 | 1 GiB |
| 45 | cache | 20037632 | 20447231 | 409600 | 200 MiB |
| 46 | hidden | 20447232 | 20529151 | 81920 | 40 MiB |
| 47 | userdata | 20529152 | 61071326 | 40542175 | 19.3 GiB |

- **system 8 GiB:** OneUI 8's system image (MysticGSI) is 6.3 GB; the stock
  3.9 GiB fits AOSP/LineageOS GSIs only. One layout for every system image.
- **vendor 1 GiB:** our vendor is a few hundred MB (Mesa, the HALs, the
  kernel modules); room to grow.
- **cache and hidden** keep their names, numbers and sizes, so nothing that
  looks them up by name sees a difference. userdata gives up 5.1 GiB.
- Partition numbers 44-47 and their unique GUIDs stay; `vendor` is entry 48.

## The firmware the system partition held

The vendor reads the tablet's firmware from its own partitions at boot
(apnhlos, modem, efs), with one exception: the Wi-Fi board data comes from
the stock system partition (`etc/firmware/bdwlan30.bin`), which a GSI
overwrites. The repartition zip copies that one file into the new vendor
partition (`/vendor/firmware/board/bdwlan30.bin`) before anything is
changed, from the stock system partition or, after a LineageOS install,
from where that ROM keeps it. It's the tablet's own file and never leaves it.

## How it's done: two OrangeFox zips

`gts3-treble.zip` (repartition) and `gts3-treble-undo.zip`, both running
under their own static busybox and sgdisk, as our TWRP installer zip does.

Repartition, in this order, stopping at the first problem with nothing
changed:
1. Model from the bootloader version (T820/T825); refuse others.
2. Read the GPT; refuse unless partitions 44-47 match the stock table above
   exactly (name, start, end), or the new table (then: nothing to do).
3. Save the GPT (`sgdisk --backup`) and `bdwlan30.bin` to the microSD card
   (refuse without one): the undo zip needs them.
4. Delete 44-47, create the new five, check the result against the table
   above; on any mismatch, restore the saved GPT.
5. Re-read the table (`blockdev --rereadpt`), format vendor (ext4, the board
   file put in), cache and userdata; system stays empty for the GSI.
6. Tell the user to reboot the recovery before flashing anything (the
   recovery's own partition list is read at its start).

Undo restores the saved GPT and formats system, cache and userdata; the
other way back is the stock firmware in Odin with its PIT and re-partition
ticked.

## Then: booting from partitions

A standard Android boot image behind lk2nd replaces gsi-init for users:
Android's own first-stage init with this fstab (`/vendor/etc/fstab.gts3`):
`system` and `vendor` by name (`first_stage_mount`), `userdata`, the
firmware partitions read only under `/vendor/firmware_mnt` (apnhlos, modem;
efs for the Bluetooth address), zram swap. Kernel modules from
`/vendor/lib/modules`, loaded by first-stage init and ueventd. gsi-init
stays the developer's way: RAM boots from image files, no repartition.

## Recovery: OrangeFox with vendor

From awesometic's TWRP device trees (Android 9 base, Apache-2.0), ported to
OrangeFox's `fox_12.1`, one tree for both models: no decryption (its
Samsung/Qualcomm libraries stay out; our data isn't encrypted), and the
fstab gains `/vendor` and `/vendor_image`, so Install Image offers "Vendor"
and backups include it. The prebuilt kernel stays his downstream 3.18 for
now (S-Boot starts the recovery partition directly).

## Open questions, settled on the T820 before any tester sees this

1. **Samsung's download mode keeps its own copy of the layout (PIT).** If
   Odin later flashes a single partition by the PIT's offsets, it would
   write over the new layout. Check: `heimdall print-pit` before and after
   the repartition. If the PIT is separate, the rule is "after this, Odin
   only with the full stock firmware and its PIT".
2. Whether S-Boot or lk2nd checks anything about the table at boot.
3. The SM-T825's table (tester).
