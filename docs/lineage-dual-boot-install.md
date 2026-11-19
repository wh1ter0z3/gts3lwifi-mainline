# Installing LineageOS next to Nura

For the SM-T820 (Wi-Fi) and SM-T825 (LTE). The tablet ends up with:

- **LineageOS 24**, started by a plain power-on, with its
  /data on its own partition;
- **Nura** (postmarketOS with Plasma), started through the recovery path;
- **OrangeFox** on our kernel, in the same menu as Nura.

LineageOS is installed from its own zip, unchanged, so its own updater keeps
working. How it works: [dual-boot.md](dual-boot.md).

**Want our own Android instead?** Any GSI on our open vendor and our
mainline kernel: [dual-boot-install.md](dual-boot-install.md). The README
compares the two.

**This has not run on a tablet with a screen yet.** On our screenless
SM-T820 LineageOS started with its /data in the right place, and the recovery
path reached Nura, but whether LineageOS's own interface comes up is still
unknown. Please report (see [Report](#report)).

## What gets destroyed

| Partition | What happens | How it comes back |
|---|---|---|
| userdata ("Internal storage") | **Everything is erased** in step 2 | Only from your own backup |
| boot | lk2nd, then LineageOS's boot image over it | Any LineageOS or ROM install |
| recovery | lk2nd goes here | Flash your old recovery again |
| system | LineageOS | A stock or ROM install |
| efs, modem, apnhlos | Not touched | |

The microSD card isn't touched: both systems share it as their storage.

## What you need

- The dual boot release: `gts3-dualboot-test2.zip`, `nura-root.img.xz` and
  its `.sha256`, plus the lk2nd files for your model, from
  [the release page](https://github.com/wh1ter0z3/gts3lwifi-mainline/releases/tag/dualboot-test2).
- The LineageOS 24 zip for your model, from
  [the LineageOS 24 releases](https://github.com/jaydens-tasty-slop/tabs3-lineageos/releases).
- The add-on: `gts3-lineage-data.zip`. It is not in a release yet, so build
  it from the repository:

  ```
  sh scripts/dualboot/lineage/mklineagezips.sh <static aarch64 busybox> <out dir>
  ```

- Battery above 50 %, and a PC for step 1.

## 1. lk2nd in the recovery partition

LineageOS owns the boot partition, so lk2nd lives in the recovery partition
instead. Samsung's bootloader starts it whenever the tablet is asked for
recovery, and that is how Nura gets its turn.

Download mode: power off, then hold **Power + Volume Down + Home**. Then
flash your model's lk2nd image to **recovery**, not to boot:

- Odin: `lk2nd-gts3lwifi-recovery-odin.tar` in the **AP** slot (SM-T825:
  `lk2nd-gts3llte-recovery-odin.tar`). These hold the same lk2nd as the
  plain `-odin.tar` files, named so Odin writes it to the recovery
  partition.
- heimdall: `heimdall flash --RECOVERY lk2nd-gts3lwifi.img` (SM-T825:
  `lk2nd-gts3llte.img`).
- Or from a recovery you already have:
  `dd if=lk2nd-gts3lwifi.img of=/dev/block/bootdevice/by-name/recovery`

Back up your old recovery partition first if you want it back later.

## 2. The layout, Nura and the recovery

Install the dual boot release as in
[dual-boot-install.md](dual-boot-install.md), **without a GSI** (so without
our Android): way A from your existing recovery, or way B from a PC. It
erases internal storage, makes the layout, writes Nura, and puts lk2nd in
the boot partition.

The tablet now starts Nura.

## 3. LineageOS and the add-on

Both go in from **our** OrangeFox, the `orangefox` entry: power on holding
**Volume Down** for lk2nd's menu and pick it.

1. Copy the LineageOS 24 zip and `gts3-lineage-data.zip` to the microSD card.
2. Install the LineageOS 24 zip. It writes its boot image over lk2nd in the boot
   partition, which is expected here.
3. Install `gts3-lineage-data.zip`. It prints which partition it found and
   ends with "Done".
4. **Format Data**, so LineageOS starts on an empty /data.
5. Reboot, System.

LineageOS's first start takes a few minutes.

If you install LineageOS again later, or it updates itself, the add-on comes
back by itself. A LineageOS installed from scratch after a **Format Data**
needs step 3 again.

## Switching

Once LineageOS is installed, a plain start always boots **LineageOS**, and
the recovery path always reaches **lk2nd**, which starts its default entry
(`nura` after step 2). Hold **Volume Down** while it starts for its menu,
and pick `nura` or `orangefox` for this one start.

| From | To | How |
|---|---|---|
| LineageOS | Nura or OrangeFox | Reboot to recovery: Advanced restart in its power menu, or `adb reboot recovery`. Hold Volume Down for the menu if you want the other one |
| off | LineageOS | Power on |
| off | Nura or OrangeFox | Power on holding **Power + Volume Up + Home** (the recovery path) |
| Nura | LineageOS | `sudo reboot` |
| Nura | OrangeFox | `sudo gts3-boot orangefox`, then `sudo systemctl reboot --reboot-argument=recovery` |
| OrangeFox | LineageOS | Reboot, System |
| OrangeFox | Nura | Reboot, Recovery |

`sudo gts3-boot <entry>` in Nura sets which entry lk2nd starts by itself
(`sudo gts3-boot` lists them), so `sudo gts3-boot nura` keeps Nura the one
the recovery path comes up in. Its `reboot` argument is for our Android dual
boot: here a plain reboot goes to LineageOS whatever the default entry is.

## Undo

- **Back to our Android dual boot:** install the dual boot release again
  (way A or B) with a GSI. It puts lk2nd back in the boot partition.
- **Back to LineageOS alone:** Format Data in any recovery and install
  LineageOS. Flash your old recovery image over lk2nd if you want the old
  recovery back.
- **Back to stock:** Samsung's firmware with Odin.

## Report

- Does LineageOS's interface come up, and does touch work?
- Settings, Storage: does it show about 4 GB of internal storage (the
  layout's Android data partition)?
- Wi-Fi, Bluetooth, sound, camera, the S-Pen, the keyboard cover.
- Reboot to recovery: does lk2nd's menu appear, and does Nura start?
- From Nura, does `sudo reboot` come back to LineageOS?
- A LineageOS update: does it install, and is /data still the same
  afterwards?
- Anything that froze, restarted or crashed, and when.
