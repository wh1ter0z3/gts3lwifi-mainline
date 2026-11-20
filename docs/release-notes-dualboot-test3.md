# Dual boot test 3: deep sleep in Nura

Nura (postmarketOS with Plasma) and Android (any GSI on our open vendor)
side by side on internal storage, plus OrangeFox. lk2nd v2 picks one at
every start: hold Volume Down at power-on for its menu. Switch without a PC.
How to install: [dual-boot-install.md](dual-boot-install.md).

**What changed since test 2:**

- **Nura sleeps deeply.** Kernel r14 lets the SoC reach the RPM's vmin
  (XO off, rails lowered) on every suspend, **with Wi-Fi connected**. On an
  SM-T820, three 30 s suspends spent about 31 s each in vmin, the Wi-Fi link
  came back each time and reconnected within seconds. The power key and the
  RTC alarm wake the tablet. Details: [suspend.md](suspend.md).
  - an I2C driver fix: only the first suspend after boot reached vmin before
    (patch 0221);
  - the MPM and the two deepest idle states in the device tree (0222, 0223);
  - the Wi-Fi PCIe link now goes down in suspend: the controller is reset
    when it is set up again (0224, 0225), which replaces keeping the link up
    (0147, dropped).
- **Nura's command line** no longer has `clk_ignore_unused` and
  `pd_ignore_unused` (device package r17). They kept every clock the
  bootloader left on, which also kept the tablet out of deep sleep.
- **OrangeFox:** adb over USB works. The recovery now loads the SM5705
  charger driver, which provides the USB-C chip's VBUS supply; without it
  USB stayed in host mode ([#22](https://github.com/wh1ter0z3/gts3lwifi-mainline/issues/22)).
- **Android and OrangeFox** run the same r14 kernel. Their command lines are
  unchanged, so Android doesn't reach deep sleep yet.
- **How Nura was built:** test 2's Nura image with only the kernel and the
  device package upgraded. postmarketOS edge is in the middle of moving
  everything from `/usr/sbin` to `/usr/bin`, and a fresh build from it would
  differ from test 2 in hundreds of packages. Everything else in Nura is
  what test 2 had.

**This is a test build. It erases internal storage (userdata).** Both models
are covered, but it was tested on an SM-T820 board without a screen only.

## Files

| File | What it is |
|---|---|
| `gts3-dualboot-test3.zip` | The installer: for TWRP/OrangeFox (Install), and for a PC (`pc/install-dualboot.sh --zip`). It holds lk2nd v2 for both models, our vendor and kernel, the recovery, and Nura's boot files |
| `nura-root.img.xz`, `.sha256` | Nura's system. It goes next to the zip |
| `lk2nd-gts3lwifi.img`, `lk2nd-gts3llte.img`, `-odin.tar` | lk2nd v2 alone, for a tablet without lk2nd (Download mode). SM-T820: gts3lwifi, SM-T825: gts3llte. Unchanged since test 2 |
| `lk2nd-...-recovery-odin.tar` | the same lk2nd for the **recovery** partition, which is where it goes for LineageOS mode |
| `OrangeFox-mainline-...-fastboot-boot.img` | our OrangeFox to start from a PC without flashing anything: `fastboot boot <file>` in lk2nd |
| `SHA256SUMS` | Checksums |

The GSI isn't in the release: we test with TrebleDroid's AOSP 16,
`system-td-arm64-vanilla.img.xz`.

## Tested

- **Kernel r14 on an SM-T820 board (no panel):** boots without the two
  options, Wi-Fi connects, three 30 s suspends with Wi-Fi up reached vmin
  (30.9, 31.0 and 30.9 s), the link and Wi-Fi came back after each.
- **OrangeFox's USB fix:** adb over USB with the charger driver loaded, on an
  SM-T820 (on the test 2 kernel; this release has the same fix on r14).
- **The installers, offline:** the PC installer built the userdata image for
  an SM-T825 (Nura only) and for an SM-T820 with Android (a stand-in system
  image). Checked in both: the boot entries and their device trees, Nura's
  command line without the two options, the r14 kernel in every entry, the
  recovery's ramdisk, Nura's root grown and clean, and r14's modules in the
  vendor partition. The zip's other files are byte for byte test 2's.

## Not tested yet

- This release installed on a tablet: the checks above ran offline.
- The display on a real panel without `clk_ignore_unused` and
  `pd_ignore_unused`, and after a resume. Test 2 (with them) showed Plasma
  on an SM-T825Y's panel.
- **SM-T825:** Wi-Fi after resume; the running modem keeps it out of vmin.
- Bluetooth after a resume, and how much battery a night of deep sleep saves.
