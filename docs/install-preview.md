# Installing the postmarketOS preview on the Galaxy Tab S3

For the SM-T820 (Wi-Fi) and SM-T825 (LTE). It puts lk2nd in the boot partition and
postmarketOS with Plasma in userdata. Read
[the release notes](release-notes-preview.md) first.

Status of this guide: the commands for lk2nd and for flashing userdata come from
installs on one SM-T820. **This image has not been run on any hardware when this was
written**, and the SM-T825 lk2nd in it is a new build that has never run.

## What gets destroyed

| Partition | What happens | How it comes back |
|---|---|---|
| userdata | **Everything is erased**: apps, photos, Android's data | Only from your own backup |
| boot | Replaced by lk2nd. Android stops booting | Flash Android's boot image or a whole ROM ([back to Android](#back-to-android)) |
| system, recovery (TWRP), efs, modem, apnhlos | Not touched | |

Copy off whatever you want to keep before step 3. The internal storage that Android
shows as "Internal storage" is userdata.

Don't wipe `system` or `efs` in TWRP. postmarketOS reads your Wi-Fi board data from
`system` and your sensor calibration from `efs` on every boot.

## What you need

- The tablet, unlocked: *OEM unlocking* on in Android's developer options, and able to
  flash custom images. If you already run TWRP or LineageOS, it is.
- Battery above 50 %.
- A USB cable that carries data. If nothing shows up on the PC, try another cable before
  anything else: a charge-only cable looks just like a dead port.
- A Linux PC. Windows works too, see [Windows](#windows).
- Something to type on is a good idea: the keyboard cover, or a USB keyboard with an
  OTG adapter. You shouldn't need it, but the on-screen keyboard at the login screen
  hasn't been tested yet, and without any keyboard you can't log in.

Check your model: Settings, About tablet, or the back of the tablet.

| Model | Has a SIM slot | lk2nd files |
|---|---|---|
| SM-T820 | no | `lk2nd-gts3lwifi.img`, `lk2nd-gts3lwifi-odin.tar` |
| SM-T825 | yes | `lk2nd-gts3llte.img`, `lk2nd-gts3llte-odin.tar` |

The boot image and userdata image are the same for both.

If you flash the other model's lk2nd, it has no device tree for your board, so it
won't boot. We haven't tried it. Download mode is part of Samsung's bootloader and
should still work, so flash the right file from there.

## 1. Download and check

Download into one directory:

    pmos-gts3-preview1-userdata.simg.zst
    pmos-gts3-preview1-boot.img
    lk2nd-gts3lwifi.img and lk2nd-gts3lwifi-odin.tar, or the gts3llte pair
    SHA256SUMS

Install the tools. One of:

    sudo apt install fastboot zstd adb       # Debian, Ubuntu
    sudo dnf install android-tools zstd      # Fedora
    sudo pacman -S android-tools zstd        # Arch

Check and unpack:

    sha256sum --ignore-missing -c SHA256SUMS
    zstd -d pmos-gts3-preview1-userdata.simg.zst

**You should see** `OK` after each file you downloaded, then a new file
`pmos-gts3-preview1-userdata.simg`.

**If not:** any `FAILED` means a bad download. Download that file again.

## 2. Install lk2nd

**Already have an lk2nd that answers `fastboot devices`?** Keep it and go to step 3. It
has to be one that reserves 4 MiB of the boot partition, which every lk2nd for this
tablet from this project does. If you aren't sure, ask.

Otherwise pick one of these. Each only writes the boot partition.

**From TWRP** (Power + Volume Up + Home from off; let go when the TWRP logo shows):

    adb push lk2nd-gts3lwifi.img /sdcard/     # or lk2nd-gts3llte.img on an SM-T825

Then on the tablet: Install, Install Image (bottom right), pick the file, select the
partition *Boot*, swipe. Reboot, System.

**From Download mode with heimdall** (Linux; `sudo apt install heimdall-flash`):

Power + Volume Down + Home from off; press Volume Up at the warning. Then:

    heimdall flash --BOOT lk2nd-gts3lwifi.img  # or lk2nd-gts3llte.img on an SM-T825

**From Download mode with Odin:** see [Windows](#windows).

**You should see:** after the Samsung logo, lk2nd's menu with
"Samsung Galaxy Tab S3 (WiFi)" or "(LTE)" and a line saying it's in fastboot mode. It
stays there, since there is no system to boot yet.

**If not:**

| What you see | What to do |
|---|---|
| "Only official released binaries are allowed" | OEM unlock is off. Flash stock with Odin, turn it on in developer options, try again |
| Samsung logo forever, or a reboot loop | Hold Power + Volume Down about 10 s to reset, go to Download mode, check the file matches your model. Still no: restore Android's boot image and tell us your model and firmware build number |

Check from the PC, with the USB cable in:

    fastboot devices

**You should see** one line with a serial number and `fastboot`.

**If not:**

| What you see | What to do |
|---|---|
| Nothing | Try another cable and a USB port directly on the PC. `lsusb` should list `18d1:d00d` |
| `no permissions` | `sudo fastboot devices`, and use `sudo` for the rest of the commands |
| fastboot hangs | Press Ctrl-C, then unplug and replug the cable. Don't pipe fastboot output through `head`: lk2nd's USB gets stuck |

## 3. Try booting from RAM (writes nothing)

This is the check that the kernel and device tree work on your tablet, before you erase
anything. Most useful on the SM-T825.

    fastboot boot pmos-gts3-preview1-boot.img

**You should see:** `Finished` on the PC. Within a minute or so, a new USB network
interface on the PC with the address `172.16.42.2`:

    ip -br addr

After about 40 s the tablet shows `boot partition not found!`, if the screen has come up
at this stage at all. That's expected: nothing is installed yet. A black screen with
the USB network up is fine too. On an SM-T825, if you got this far, that is already a
useful result; note it for your report.

**If not:** wait 2 minutes, then hold Power + Volume Down about 10 s. Nothing has been
written. Note what the screen showed. If the tablet goes back to lk2nd saying
"DTB offset is incorrect", get lk2nd's log and stop here:

    fastboot oem log
    fastboot get_staged lk2nd.log

Get back to lk2nd: hold Power + Volume Down about 10 s until the screen goes off, let
go, and it starts lk2nd again. It stays in fastboot since nothing is installed.

## 4. Flash

**This erases userdata.** Last chance to back up.

    fastboot flash userdata pmos-gts3-preview1-userdata.simg
    fastboot flash boot pmos-gts3-preview1-boot.img
    fastboot reboot

**You should see:** `Sending sparse 'userdata' 1/N` through `N/N` with `OKAY` after each,
taking 5 to 10 minutes. Then `Sending 'boot'` and `OKAY`. `fastboot flash boot` is safe
here: lk2nd keeps its own first 4 MiB and puts the image behind them, so you see it
write a partition smaller than `boot`.

**If not:** if a step fails, run it again. If it fails again, send the output and
lk2nd's log (see step 3). The tablet can't be damaged at this stage beyond what's listed
in [What gets destroyed](#what-gets-destroyed).

## 5. First boot

**You should see:** Samsung logo, a moment of lk2nd, the postmarketOS splash, then
the Plasma login screen with one user. The first boot resizes the root filesystem to fill
userdata, so it takes longer than the rest. Expect a few minutes.

Log in:

1. Tap the password field. An on-screen keyboard should appear.
2. Type the default password: `147147`

If no on-screen keyboard appears, tap the field again, then try the keyboard cover or a
USB keyboard. Please report this: it's one of the things we most need to know.

After login, a window called *Set your own password* opens:

3. Tap **Change it now**. The Users page of System Settings opens.
4. Tap **Change Password** and enter a new one twice. Tap a password field to get the
   keyboard.

The window comes back at each login until the default password is gone. To change it
from a terminal instead, open Konsole and run `passwd`.

## 6. Reboot once

On first boot the tablet copied its own firmware from its partitions: GPU, video, audio
and sensor core from `apnhlos` and `modem`, Wi-Fi board data from `system`, sensor
calibration from `efs`. None of this is in the image, since nobody may redistribute it.
It stays in `/lib/firmware/updates`. Wi-Fi only picks up the board data on the next boot.

Reboot from the application menu (Leave, Restart).

**You should see** Wi-Fi networks in the panel's network icon. Connect to yours.

**If not:** open Konsole and run:

    sudo systemctl status gts3-firmware
    ls -R /lib/firmware/updates

You should see `a530_zap`, `venus`, `adsp` and `slpi` files and `ath10k/QCA6174/hw3.0/board.bin`.
If `board.bin` is missing, your `system` partition doesn't have
`etc/firmware/bdwlan30.bin`. Flash a stock or LineageOS `system` from TWRP and reboot.

**Connected, but no website opens?** With some home routers systemd-resolved's DNSSEC check
fails. We saw this on ours. Turn the check off:

    sudo mkdir -p /etc/systemd/resolved.conf.d
    printf '[Resolve]\nDNSSEC=no\n' | sudo tee /etc/systemd/resolved.conf.d/90-no-dnssec.conf
    sudo systemctl restart systemd-resolved

## 7. Check where it's running

In Konsole:

    uname -r
    cat /sys/firmware/devicetree/base/model; echo
    grep " / " /proc/mounts

**You should see** `7.3.1`, then your model, then a line with `/` on a `loop` device. If
it shows `mmcblk1p2` instead, there's a microSD with an old copy of this image in the
tablet. Take it out and reboot.

## Turning on SSH

SSH is off in this image, and stays off until you turn it on. Change the password first.
Then, in Konsole:

    sudo systemctl enable --now sshd

Log in as `user` with your password. Over USB the tablet is at `172.16.42.1`. The firewall
opens port 22 on every network once SSH runs, so it's reachable on Wi-Fi too. To turn it off
again: `sudo systemctl disable --now sshd`.

## Getting back to lk2nd later

Hold Volume Down while powering on. From there `fastboot boot` and `fastboot flash` work
as above.

## After a kernel update

Don't update for now ([why](release-notes-preview.md#warnings)). lk2nd boots the copy
behind it in the boot partition, not the one in `/boot`, so a kernel update needs this
afterwards, after checking that the first command prints `PARTNAME=boot`:

    grep PARTNAME=boot /sys/class/block/mmcblk0p28/uevent
    sudo dd if=/boot/boot.img of=/dev/mmcblk0p28 bs=1M seek=4 conv=fsync

## Forgot the password

There is no reset. Flash userdata again (step 4), which erases everything on it.

## Back to Android

TWRP is still in the recovery partition unless you replaced it.

**With TWRP and a LineageOS zip:**

1. Hold Power + Volume Down until the screen goes off, then at once Power + Volume Up +
   Home until the TWRP logo shows.
2. Wipe, Format Data, type `yes`. This erases postmarketOS. Android needs it: userdata now
   holds postmarketOS's partitions, not a filesystem Android can read.
3. Reboot to recovery again, so that TWRP sees the new filesystem.
4. Install the ROM zip. That also replaces lk2nd, since the zip writes the boot partition.
5. Reboot, System.

**With Odin and stock firmware:** Download mode (Power + Volume Down + Home from off,
Volume Up at the warning). Put the firmware's BL, AP and CSC files in their slots, and CP
if it has one. Use `CSC`, not `HOME_CSC`, so that userdata is erased. Start. This replaces
TWRP too. The Knox flag stays tripped; nothing resets it.

**Just the boot partition:** to run Android again with its userdata intact, you have to
have flashed nothing in step 4, and need Android's own `boot.img`: flash it with
`heimdall flash --BOOT boot.img` or from TWRP (Install Image, *Boot*).

## Windows

Odin is Samsung's internal flashing tool, for Windows only. Samsung doesn't publish it,
so you have to get it from a third-party site; decide for yourself if you trust that.

1. Download mode: Power + Volume Down + Home from off, Volume Up at the warning.
2. In Odin, press AP, select `lk2nd-gts3lwifi-odin.tar` (SM-T820) or
   `lk2nd-gts3llte-odin.tar` (SM-T825). Leave the other slots empty. Press Start.
3. **You should see** `PASS!`, and the tablet reboots into lk2nd.

Then the fastboot steps are the same with Google's
[platform-tools](https://developer.android.com/tools/releases/platform-tools). Windows
may need a driver before `fastboot devices` lists the tablet: in Device Manager, choose
the Android Bootloader Interface from Google's USB driver. Check the download
with `certutil -hashfile FILE SHA256` and compare by eye with `SHA256SUMS`. For `zstd`,
use the Windows build from [zstd's releases](https://github.com/facebook/zstd/releases):
`zstd.exe -d pmos-gts3-preview1-userdata.simg.zst`.

We haven't run any of the Windows steps with this image. Step 3's USB network may need a
driver on Windows too; skip it, or use Linux for that step.
