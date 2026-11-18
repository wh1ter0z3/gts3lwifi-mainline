# Dual boot test 1: Nura and Android on the Galaxy Tab S3

Nura (postmarketOS with Plasma) and Android (any GSI on our open vendor)
side by side on internal storage, plus OrangeFox on our kernel. lk2nd v2
picks one at every start: hold Volume Down at power-on for its menu. Switch
without a PC. How to install: [dual-boot-install.md](dual-boot-install.md).

**This is a test build. It erases internal storage (userdata).** Both models
are covered, but it was tested on an SM-T820 board without a screen only.

## Files

| File | What it is |
|---|---|
| `gts3-dualboot-test1.zip` | The installer: for TWRP/OrangeFox (Install), and for a PC (`pc/install-dualboot.sh --zip`). It holds lk2nd v2 for both models, our vendor and kernel, the recovery, and Nura's boot files |
| `nura-root.img.xz`, `.sha256` | Nura's system. It goes next to the zip |
| `lk2nd-gts3lwifi.img`, `lk2nd-gts3llte.img`, `-odin.tar` | lk2nd v2 alone, for a tablet without lk2nd (Download mode). SM-T820: gts3lwifi, SM-T825: gts3llte |
| `lk2nd-...-recovery-odin.tar` | the same lk2nd for the **recovery** partition, which is where it goes for LineageOS mode |
| `OrangeFox-mainline-...-fastboot-boot.img` | our OrangeFox to start from a PC without flashing anything: `fastboot boot <file>` in lk2nd |
| `SHA256SUMS` | Checksums |

The GSI isn't in the release: we test with TrebleDroid's AOSP 16,
`system-td-arm64-vanilla.img.xz`.

Jayden's LineageOS 24 can take our Android's place and keep Nura, installed
from his own zip plus a small add-on:
[lineage-dual-boot-install.md](lineage-dual-boot-install.md). The README
compares the two.

## What's in it

- **Nura:** the postmarketOS preview (kernel 7.3.1, device package r15,
  Plasma 6.8). Same image as the preview, with the dual boot hook, `gts3-boot`
  and the shared clock.
- **Android:** our vendor (AOSP 15 source, Mesa, hardware video, camera
  through libcamera, sensors, GNSS) on our kernel 7.3.1. Nothing from
  Samsung is in it: the tablet's own firmware is read from its partitions
  at every start.
- **OrangeFox** on the same kernel, as lk2nd's `orangefox` entry. Its /data
  is Android's data partition.
- **lk2nd v2:** a menu with the systems, `fastboot oem boot-label`, room for
  our kernels.
- The **microSD card** is the storage both systems share. The clock is one
  for both.

## Tested

On an SM-T820 board without a screen:
- lk2nd v2;
- both systems from the layout;
- switching both ways without a PC, and the shared clock;
- OrangeFox on our kernel: adb, its dual boot mode, the exFAT card;
- the zip installer run inside that OrangeFox.

## Not tested yet

- Anything on a screen: lk2nd's menu, Android, OrangeFox on our kernel.
- **SM-T825:** its lk2nd v2 and Android on it. If it loops at the Samsung
  logo after installing, flash your old lk2nd from Download mode.
- The PC installer flashing a tablet.
- TWRP or OrangeFox builds on Samsung's kernel running the zip.
