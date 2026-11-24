# Install guide

How to put **Nura** (a full Linux desktop, postmarketOS with Plasma) and/or
**Android** on a Samsung Galaxy Tab S3, with a Windows or Linux PC. No
command line knowledge needed: one script asks you a few questions and does
the rest. It takes about 15 minutes, plus the downloads.

This is for the setup scripts that come with **dual boot test 4 and later**.
The technical details, and the other ways to install, are in
[dual-boot-install.md](dual-boot-install.md).

**These are test builds.** They work on the developers' tablets, but not
every model and screen has been tried. Read [What works](#what-works) first,
and please [report](#report) how it went.

## What you get

You choose one of five setups:

| Setup | What's on the tablet |
|---|---|
| **1. Nura only** | Linux with the Plasma desktop: a browser, files, settings, Wi-Fi, Bluetooth, sound, cameras, the S-Pen. It gets the whole internal storage (about 28 GB) |
| **2. Android only** | an Android of your choice (a "GSI", see [Downloads](#2-download-the-files)), on our open source drivers |
| **3. Nura and Android** | both, side by side; you pick one at every start, and switch without a PC |
| **4. LineageOS only** | a LineageOS build for the Tab S3 (Awesometic's, Jayden's), installed from its own zip, plus our recovery |
| **5. Nura and LineageOS** | both: a normal start is LineageOS, the recovery keys start Nura |

A recovery (OrangeFox) comes with every setup, for backups and repairs.
Samsung's partition table is never changed (the layouts are inside its
partitions, [details](partitions.md)), so going back to stock is just
flashing Samsung's firmware.

## What works

- **Nura:** screen, touch, S-Pen, keyboard cover, Wi-Fi, Bluetooth, sound,
  cameras, hardware video, rotation, deep sleep (a night on standby costs
  about 1 % of battery), charging.
- **Android:** the same drivers; tested on a board without a screen so far,
  so its display is the least tested part.
- **Encryption:** all of Nura (like a normal Linux install: a passphrase at
  every start), or one encrypted folder.
- **Not yet:** the fingerprint sensor, Netflix-style HD video (Widevine L1),
  mobile data on the SM-T825 (LTE).

## 1. Before you start

- **Your model:** Settings → About tablet, or the label on the back.
  **SM-T820** (Wi-Fi) and **SM-T825** (LTE, it has a SIM slot) both work.
  Other Tab S3 variants (SM-T827) have never been tried.
- **Back up your files.** The install **erases the tablet's internal storage**:
  apps, photos, downloads, everything you saved on the tablet. Copy what you
  want to keep to a PC or a microSD card first. The microSD card itself is
  not touched.
- **Charge the battery** to at least 50 %.
- **A USB cable** that carries data (some charging cables don't), and a PC
  with **Windows 10 or 11**, or **Linux**.
- About **3 GB** free on the PC for the downloads.

## 2. Download the files

From the [latest release](https://github.com/wh1ter0z3/gts3lwifi-mainline/releases),
put these in **one folder** on your PC:

| File | Why |
|---|---|
| `gts3-dualboot-test4.zip` (or newer) | the installer: don't unzip it |
| `OrangeFox-mainline-gts3lwifi-fastboot-boot.img` (SM-T820) or `-gts3llte-` (SM-T825) | the recovery the script starts on the tablet |
| `setup.ps1` (Windows) or `setup.sh` (Linux) | the script you run |
| `nura-root.img.xz` and `nura-root.img.xz.sha256` | **for Nura** |
| `lk2nd-gts3lwifi-odin.tar` (SM-T820) or `lk2nd-gts3llte-odin.tar` (SM-T825) | only if your tablet doesn't have lk2nd yet (step 3) |

**For Android**, also download a GSI into the same folder: an "arm64 A/B"
system image of Android 15, 16 or 17, at most 4 GB unpacked. We test with
TrebleDroid's AOSP 16, `system-td-arm64-vanilla.img.xz`, from
[TrebleDroid's releases](https://github.com/TrebleDroid/treble_experimentations/releases).
Leave the `.xz` packed.

**For LineageOS**, download a LineageOS zip **for your model** into the same
folder (`lineage-*.zip`: for the SM-T820 it says `gts3lwifi`, for the
SM-T825 `gts3llte`). Leave it zipped.

The script lists all five setups and says, for each, which file it still
needs.

**adb and fastboot**, the tools the script uses to talk to the tablet:

- **Windows:** download Google's
  [SDK Platform-Tools for Windows](https://developer.android.com/tools/releases/platform-tools),
  unzip it, and put the `platform-tools` folder **into the same folder** as
  the files above.
- **Linux:** install them from your distribution: `sudo apt install adb
  fastboot` (Debian, Ubuntu, Mint), `sudo pacman -S android-tools` (Arch),
  `sudo dnf install android-tools` (Fedora).

## 3. lk2nd (once)

lk2nd is a small boot menu that starts Nura, Android or the recovery. It
replaces nothing you need: Samsung's own bootloader stays, and so does your
tablet's firmware.

**Skip this step if your tablet already has lk2nd**: you installed
postmarketOS or one of our earlier test builds before. If you're not sure,
try step 4: if you see lk2nd's screen there, you have it.

1. Turn the tablet off. Hold **Power + Volume Down + Home** until a warning
   screen appears, then press **Volume Up**: this is Samsung's *Download mode*.
2. Flash the lk2nd file **for your model**. The other model's file makes the
   tablet loop at the Samsung logo.
   - **Windows:** with Odin (Samsung's flashing tool) and Samsung's USB
     driver ([Samsung Android USB Driver](https://developer.samsung.com/android-usb-driver)).
     Put `lk2nd-...-odin.tar` in the **AP** slot and press Start.
   - **Linux:** `heimdall flash --BOOT lk2nd-gts3lwifi.img` (or
     `lk2nd-gts3llte.img`), with the `.img` from the release.
3. The tablet restarts by itself when it's done.

## 4. Start lk2nd's fastboot mode

1. Turn the tablet off (hold Power, then *Power off*; or hold Power about 10
   seconds).
2. Turn it on while holding **Volume Down only**. Keep holding past the
   Samsung logo until lk2nd's screen appears (both volume keys would start
   Samsung's emergency mode instead).
3. Connect it to the PC by USB. It stays on this screen until the script
   takes over.

## 5. Run the setup script

**Windows:** open the folder, right-click `setup.ps1`, choose **Run with
PowerShell**. If Windows refuses to run it, open PowerShell in that folder
(Windows 11: right-click in the folder → *Open in Terminal*; Windows 10:
Shift + right-click → *Open PowerShell window here*) and run:

```
powershell -ExecutionPolicy Bypass -File .\setup.ps1
```

**Linux:** open a terminal in that folder and run:

```bash
bash setup.sh
```

The script asks:

1. **What to install**: the five setups, each marked *ready* or with the file
   it still needs. Type its number.
2. **For Nura:**
   - your **user name** (lowercase, like `anna`) and **password**: you log in
     with these;
   - the tablet's **name** on your network;
   - **time zone**, **language**, **keyboard layout** and **country** (two
     letters, like `IN`, `US` or `GB`: it sets which Wi-Fi channels are
     allowed): it suggests your PC's own; press Enter to take a suggestion;
   - a **Wi-Fi network** and its password, so Nura is online at its first
     start (press Enter to skip).
   - whether to **encrypt all of Nura** (LUKS2, like a normal Linux
     install): everything stays unreadable without a passphrase you type at
     every start, on the screen or the keyboard cover. **Don't lose it**:
     nobody can recover the data without it. If you say no, it offers an
     **encrypted folder**, `~/Private`, instead: the tablet's storage chip
     encrypts its files in hardware, and `fscrypt unlock ~/Private` opens it
     after logging in ([encryption](encryption.md));
   - whether to turn on **SSH**, to log in to the tablet from your PC (over
     the USB cable at `172.16.42.1`, or over Wi-Fi). It offers your PC's
     SSH key if you have one; with a key, the tablet only accepts that key.
3. Then it finds the tablet, starts the recovery on it (nothing is installed
   yet), and asks you to type **yes**. That is the point of no return: the
   tablet's internal storage is erased after it.

Now leave it alone, with the cable connected. The script shows each step:
the layout, sending Nura (about 1.4 GB, about a minute) and the GSI or the
LineageOS zip, then your settings. Every file is checked on the tablet after
it arrives; a damaged one is refused, and the script tells you to run it
again. At the end it offers to restart the tablet.

With LineageOS, the script also copies the tablet's old recovery (from the
recovery partition, which now gets lk2nd) to your PC first.

## 6. First start

- **Nura** starts by default (Android, if you installed only Android). You
  see the Samsung logo, then postmarketOS, then the login screen: use the
  user name and password you gave the script. If you skipped the script
  (installed another way), the user is `user` and the password `147147`,
  and Plasma asks you to change it.
- **Encrypted Nura:** the unlock screen comes up first: type your passphrase.
- **Android's** first start takes several minutes: the Samsung logo, a black
  screen for a while, then Android's own setup.
- **LineageOS** starts on a normal power-on. For Nura or the recovery, turn the
  tablet on holding **Power + Volume Up + Home**: lk2nd's menu appears.

## Switching between Nura and Android

- **Once:** turn the tablet on holding **Volume Down**. lk2nd's menu lists
  `nura`, `android` and `orangefox`: pick one with the volume keys, confirm
  with Power. The next normal start is the default again.
- **Change the default** from Nura: open *Konsole* and type
  `sudo gts3-boot android reboot` (or `nura`). `sudo gts3-boot` alone lists
  the choices.
- **With LineageOS:** LineageOS's *Restart to recovery* starts lk2nd's menu
  (Nura by default); a normal restart in Nura goes back to LineageOS.

## The recovery (OrangeFox)

Start it from lk2nd's menu (`orangefox`). It knows this setup: *Format
Data* and a factory reset there erase **Android's data only**, never Nura.
Backups can save Nura, Android, or both, to the microSD card.

**Don't use the recovery from Power + Volume Up + Home** if you still have
an old one there: it doesn't know this setup, and *Format Data* in it
erases Nura and Android together.

## If something goes wrong

- **The script stops with an error:** read the last lines, fix what they say
  (a missing file, the cable), and run it again. It starts over from the
  beginning and is safe to repeat.
- **Windows: the script waits for the tablet forever** while lk2nd's screen
  is showing: Windows has no driver for it yet. Open *Device Manager*, find
  the tablet (often under *Other devices*), choose *Update driver*, and
  install Google's USB driver (from the
  [Google USB Driver](https://developer.android.com/studio/run/win-usb) page)
  as *Android Bootloader Interface*. Then run the script again. *(Windows
  hasn't been tested by us yet: please report what you had to do.)*
- **Linux: "no permissions"** from adb or fastboot: install your
  distribution's `android-udev-rules` (or run the script with `sudo`).
- **The tablet doesn't start or hangs:** hold **Power for about 10 seconds**
  to restart it. Hold **Volume Down** while it starts to get lk2nd's menu and
  pick another system or the recovery.
- **SM-T825 loops at the Samsung logo** after the install: lk2nd doesn't
  start on that board. Go to Download mode (step 3) and flash your old lk2nd
  or a stock firmware, and please report it.
- **Back to stock Android (Samsung's):** flash Samsung's firmware for your
  model with Odin or heimdall. That removes lk2nd, Nura and our Android, and
  brings back Samsung's system. With LineageOS, the old recovery is in the
  file the script saved on your PC (`gts3-recovery-before-...img.gz`).

## Report

For questions or a quick chat, there is the [Telegram group](https://t.me/+J_vEuef2Xk1iZGE9).
For a report, open an [issue](https://github.com/wh1ter0z3/gts3lwifi-mainline/issues) and say:

- your model (SM-T820 or SM-T825) and PC (Windows or Linux);
- which setup you chose, and whether the script finished (copy its last
  lines if it didn't);
- what works and what doesn't: the screen and touch in lk2nd's menu, Nura,
  Android and the recovery; Wi-Fi; sound; anything that froze or restarted.
