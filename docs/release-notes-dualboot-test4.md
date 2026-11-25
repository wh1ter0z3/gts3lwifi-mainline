# Test 4: one setup script, five setups, all of the internal storage

Nura (postmarketOS with Plasma), Android (any GSI on our open vendor) or
LineageOS, alone or next to Nura, with OrangeFox and lk2nd v2. A PC script
(Linux or Windows) asks a few questions and does the rest. How to install:
[install.md](install.md).

**What changed since test 3:**

- **Five setups from one script:** Nura only, Android only, Nura and
  Android, LineageOS only, Nura and LineageOS. The script lists all five and
  says which file each still needs; LineageOS zips (Awesometic's, Jayden's)
  are installed from the zip itself.
- **The whole internal storage.** Samsung's system, cache, hidden and
  userdata partitions hold our layout now (28.6 GiB): Nura only gets about
  28 GiB instead of 23.7. Samsung's own partition table is never changed (the
  bootloader write-protects it), so going back to stock is just Samsung's
  firmware. Details: [partitions.md](partitions.md).
- **Encrypt all of Nura**, like a normal Linux install: LUKS2 (AES-XTS 512,
  4 KiB sectors, Argon2id), a passphrase at every start on postmarketOS's
  unlock screen. Or one encrypted folder, as before. [encryption.md](encryption.md).
- **20 times faster install:** Nura's root takes about a minute over USB
  instead of 17 (the zip unpacks it with its own xz, on all four cores).
- **Nura:** kernel r18 (Bluetooth no longer keeps the tablet awake, plugging
  in wakes it), device package r28: the tablet now also sleeps at the login
  screen (5 minutes on battery, 10 on the charger), and keeps Samsung's
  Wi-Fi board data, which 5 GHz needs (VHT80 2x2, 650 Mbit/s on channel 149).
  Smedio is in the image. A fresh build from postmarketOS edge: against the
  last full image build, 48 packages more (Smedio's), none fewer, 68 small
  version updates.
- **OrangeFox** has its display fix (one plane, zpos) in the boot menu entry
  too, knows every layout (Wipe can't reach Nura), and reports itself as an
  SM-T825 on one, for T825 ROM zips.
- **Android and OrangeFox** stay on kernel r14 (as in test 3); gsi-init finds
  its partitions in the new layout. The OrangeFox the setup script starts from
  RAM (the two `OrangeFox-mainline-*-fastboot-boot.img`) is on kernel r18 since
  the fix below.

**Fixed on 2026-11-25:** OrangeFox's touch didn't match its screen (taps
landed elsewhere, the lock screen couldn't be swiped open) and its battery
showed "!" with no percentage. New `gts3-dualboot-test4.zip` (only its
OrangeFox ramdisk changed) and both `OrangeFox-mainline-*-fastboot-boot.img`;
tested on the SM-T820 with a simulated touchscreen: the lock screen opens,
buttons respond, battery 100% and charging. Downloaded before? Get those three
files again (and `SHA256SUMS`); `nura-root.img.xz` and the rest are unchanged.
An installed tablet keeps its old OrangeFox; to use the fixed one now, start
it from the PC in lk2nd's fastboot:
`fastboot boot OrangeFox-mainline-gts3lwifi-fastboot-boot.img` (`gts3llte`
for the SM-T825).

**Fixed on 2026-11-24:** OrangeFox showed no battery level and no charger
(three modules were missing from its ramdisk: `sm5705_fuel_gauge`,
`i2c-gpio`, `i2c-algo-bit`). New `gts3-dualboot-test4.zip` (only its OrangeFox
ramdisk changed) and both `OrangeFox-mainline-*-fastboot-boot.img`; tested on
the SM-T820, battery 100% in OrangeFox. Downloaded before? Get those three
files again (and `SHA256SUMS`); `nura-root.img.xz` and the rest are unchanged.
A tablet that is already installed only lacks the battery in OrangeFox:
reinstalling isn't needed.

**This is a test build. It erases the tablet's internal storage.** All five
setups were installed and started on an SM-T820 board without a screen.

## Files

| File | What it is |
|---|---|
| `gts3-dualboot-test4.zip` | the installer, for the setup scripts (and for TWRP/OrangeFox) |
| `setup.sh`, `setup.ps1` | the setup scripts, Linux and Windows |
| `nura-root.img.xz`, `.sha256` | Nura's system |
| `OrangeFox-mainline-gts3lwifi-fastboot-boot.img`, `-gts3llte-` | the recovery the script starts from RAM |
| `lk2nd-gts3lwifi.img`, `lk2nd-gts3llte.img`, `-odin.tar` | lk2nd v2, for a tablet without lk2nd (Download mode). Unchanged since test 2 |
| `SHA256SUMS` | checksums |

The GSI and LineageOS aren't in the release: we test with TrebleDroid's AOSP
16 (`system-td-arm64-vanilla.img.xz`) and Jayden's LineageOS 24 for the
SM-T820.

## Tested (SM-T820, no screen)

- All five setups from setup.sh: each installed and started; Nura and
  Android, Nura and LineageOS switched both ways; LineageOS's "reboot
  recovery" reached lk2nd and OrangeFox.
- Encrypted Nura: installed, unlocked with a test key file (no screen for the
  passphrase), root on `/dev/mapper/root`, 81.6 MB/s write, 104.9 MB/s read.
- Wi-Fi on 5 GHz with the kept board data, from Nura's very first start
  (Samsung's board data, regdomain GB, joined a hotspot on channel 149);
  Bluetooth with its address; the login-screen sleep (suspended after the
  limit, woke on the power key).
- The release files themselves, as published: Nura only installed with
  setup.sh from this folder, kernel r18, device r28 and Smedio from the
  image, no failed services.

## Not tested yet

- Any screen: the unlock screen of an encrypted Nura, Plasma, Android's and
  LineageOS's interface.
- setup.ps1 on Windows.
- The SM-T825.
