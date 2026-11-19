# Installing the dual boot (test 1)

For the SM-T820 (Wi-Fi) and SM-T825 (LTE). Internal storage gets two systems
side by side, and lk2nd picks one at every start:

- **Nura** (postmarketOS) with Plasma;
- **Android**: any GSI on our open vendor and our kernel (optional);
- **OrangeFox** on our kernel, as a third entry (`orangefox`) in lk2nd's menu.

You switch between them on the tablet, without a PC. How it works:
[dual-boot.md](dual-boot.md).

**Want LineageOS 24 instead of our open Android?** It also keeps Nura,
from its own zip: [lineage-dual-boot-install.md](lineage-dual-boot-install.md).
The README compares the two.

**This is a test build.** It was made and tested on an SM-T820 board
without a screen. Untested so far:
- the screen in lk2nd's menu, in Android and in our OrangeFox (Nura's screen
  works);
- the SM-T825 as a whole: its lk2nd v2, and Android on it.

So please report what you see at every step (see [Report](#report)).

## What gets destroyed

| Partition | What happens | How it comes back |
|---|---|---|
| userdata ("Internal storage") | **Everything is erased**: Android's apps and data, a postmarketOS install | Only from your own backup |
| boot | lk2nd v2 goes into its first 4 MiB | Your old lk2nd, a ROM's boot image, or Odin |
| recovery | Not touched: your TWRP or OrangeFox stays there | |
| system, efs, modem, apnhlos | Not touched | |

Copy off what you want to keep first. The microSD card isn't touched: both
systems use it as their shared storage.

Nura and our Android read the tablet's own firmware (Wi-Fi, Bluetooth, audio,
video, GPU) from the system, modem, apnhlos and efs partitions at every
start. Don't wipe them.

## What you need

- These files from the release, in one folder:
  - `gts3-dualboot-test2.zip`: the installer, with lk2nd v2 for both models,
    our vendor and kernel, the recovery, Nura's boot files, and the PC
    installer in `pc/`;
  - `nura-root.img.xz` and `nura-root.img.xz.sha256`: Nura's system (1.5 GB).
- For Android, a GSI: an arm64 A/B system image of Android 15, 16 or 17. We
  test with TrebleDroid's AOSP 16, `system-td-arm64-vanilla.img.xz` from
  [TrebleDroid's releases](https://github.com/TrebleDroid/treble_experimentations/releases)
  (sha256 `d06c66e45c6d1e8d61b45d1261a65cb7ccf2e7946739602011e500a12ae55ad6`).
  Leave it out for Nura only.
- Battery above 50 %.
- Either TWRP / OrangeFox on the tablet (way A, no PC) or a Linux PC (way B).

Check your model in Settings, About tablet, or on the back: SM-T825 has a SIM
slot.

## A. From TWRP or OrangeFox (no PC)

1. Copy the zip, `nura-root.img.xz` with its `.sha256`, and the GSI into one
   folder on the **microSD card** or a USB drive. Not on internal storage:
   that's what gets erased.
2. Start the recovery: power off, then hold **Power + Volume Up + Home**.
3. Install, pick `gts3-dualboot-test2.zip`, swipe. It checks the files first.
   If one is damaged it stops before changing anything. Then it takes 10 to
   15 minutes.
4. It ends with "Done". Reboot, System.

## B. From a Linux PC

The PC needs:
- fastboot;
- e2fsprogs 1.47 or newer;
- simg2img and img2simg;
- sfdisk, cpio, unzip, xz, python3;
- about 30 GB free in the folder you run it from.

To install them on Debian/Ubuntu:
`apt install fastboot e2fsprogs android-sdk-libsparse-utils fdisk cpio unzip xz-utils python3`.
On Arch: `pacman -S android-tools e2fsprogs util-linux cpio unzip xz python`.

1. **lk2nd.** Skip this if you have it from a postmarketOS install: any
   version works. If not:
   - Put the tablet in Download mode: power off, then hold
     **Power + Volume Down + Home**.
   - Flash `lk2nd-gts3lwifi.img` (SM-T820) or
     `lk2nd-gts3llte.img` (SM-T825) from the release. With heimdall:
     `heimdall flash --BOOT <file>`. With Odin: the matching `-odin.tar` in
     the AP slot.

   Use your model's file: the other one makes the bootloader loop.
2. **lk2nd's fastboot.** Power off, then power on holding **Volume Down only**
   (both volume keys is Samsung's emergency mode). Keep holding it past the
   Samsung logo until lk2nd's screen: fastboot works while it's shown. Check
   with `fastboot devices`.
3. In the folder with the files:

   ```
   unzip gts3-dualboot-test2.zip 'pc/*'
   sh pc/install-dualboot.sh --zip gts3-dualboot-test2.zip --root nura-root.img.xz \
       --gsi system-td-arm64-vanilla.img.xz
   ```

   Leave out `--gsi ...` for Nura only. The installer:
   1. asks lk2nd for the model;
   2. builds the userdata image on the PC (10 to 20 minutes);
   3. asks you to type `yes`;
   4. flashes userdata, then lk2nd v2, and restarts the tablet.

## First start

lk2nd starts Nura. You see the Samsung logo, then the postmarketOS splash,
then Plasma's login screen. The user is `user` with the password `147147`.
Plasma asks you to change it.

## Switching

- **lk2nd's menu** works always: power on holding **Volume Down**, then pick
  `nura`, `android` or `orangefox` with the volume keys and Power. That boots
  it once. The next normal start is the default again. The menu's own items
  below them (Reboot, Continue, Recovery, ...) are lk2nd's usual ones:
  *Recovery* there is the recovery partition, not ours.
- **From Nura**, to make Android the default: open Konsole and run
  `sudo gts3-boot android reboot`. `sudo gts3-boot` lists the entries.
- **Back to Nura** as the default: start Nura from lk2nd's menu, then run
  `sudo gts3-boot nura`.

Android's first start takes a few minutes: Samsung logo, a black screen for
a while, then the GSI's setup.

## The recovery

The `orangefox` entry in lk2nd's menu is OrangeFox on our kernel. It knows the
dual boot: its /data is Android's data partition. "Format Data" and a factory
reset there erase Android's data only, never Nura or the layout. It lists
Nura and the Android images for backups.

**The recovery in the recovery partition doesn't know the layout.** That's the
one Power + Volume Up + Home starts. Never "Format Data" or "Wipe Data" in it:
it would erase Nura and Android together. Use the recovery from lk2nd's menu.

## Undo

- To go back to a ROM, use the old recovery (Power + Volume Up + Home):
  - Format Data (this erases both systems);
  - install the ROM as usual (it replaces lk2nd).
- Stock: flash Samsung's firmware with Odin.
- **SM-T825**, if the tablet loops at the Samsung logo after the install:
  lk2nd v2 doesn't start on your board. Put it in Download mode and flash
  your old lk2nd. Userdata stays as it is, and please send us that report.

## Report

Please say which way you installed (A or B), your model, and for each step
what you saw:

1. lk2nd's menu (Volume Down at power-on): readable? Does it list `nura`,
   `android`, `orangefox`?
2. Nura: did it start, and do the screen and touch work? Wi-Fi?
3. Android: did it reach the setup? Screen, touch, rotation, Wi-Fi,
   Bluetooth, sound, video playback. Is the clock right?
4. `orangefox` from lk2nd's menu: does it show on the screen, react to touch,
   see the microSD card?
5. `sudo gts3-boot android reboot` in Nura, and back through the menu.
6. Anything that froze, restarted or crashed, and when.
