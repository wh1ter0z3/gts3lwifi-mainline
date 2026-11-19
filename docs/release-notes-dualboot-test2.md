# Dual boot test 2: one kernel for Nura, Android and the recovery

Nura (postmarketOS with Plasma) and Android (any GSI on our open vendor)
side by side on internal storage, plus OrangeFox. lk2nd v2 picks one at
every start: hold Volume Down at power-on for its menu. Switch without a PC.
How to install: [dual-boot-install.md](dual-boot-install.md).

**What changed since test 1:** all three systems — Nura, Android and the
recovery — now run **one kernel**, Linux 7.3.1 built from a single package.
Before, Android and the recovery used a kernel of their own. One kernel
means one set of fixes and one thing to update. The device tree now also
carries the TrustZone buffer pool and the fingerprint sensor's lines, and
the virtual-display driver ships as a module, so the recovery and Android
draw a screen on a board whose panel is disconnected.

**This is a test build. It erases internal storage (userdata).** Both models
are covered, but it was tested on an SM-T820 board without a screen only.

## Files

| File | What it is |
|---|---|
| `gts3-dualboot-test2.zip` | The installer: for TWRP/OrangeFox (Install), and for a PC (`pc/install-dualboot.sh --zip`). It holds lk2nd v2 for both models, our vendor and kernel, the recovery, and Nura's boot files |
| `nura-root.img.xz`, `.sha256` | Nura's system. It goes next to the zip |
| `lk2nd-gts3lwifi.img`, `lk2nd-gts3llte.img`, `-odin.tar` | lk2nd v2 alone, for a tablet without lk2nd (Download mode). SM-T820: gts3lwifi, SM-T825: gts3llte |
| `lk2nd-...-recovery-odin.tar` | the same lk2nd for the **recovery** partition, which is where it goes for LineageOS mode |
| `OrangeFox-mainline-...-fastboot-boot.img` | our OrangeFox to start from a PC without flashing anything: `fastboot boot <file>` in lk2nd |
| `SHA256SUMS` | Checksums |

The GSI isn't in the release: we test with TrebleDroid's AOSP 16,
`system-td-arm64-vanilla.img.xz`.

LineageOS 24 can take our Android's place and keep Nura, installed
from its own zip plus a small add-on:
[lineage-dual-boot-install.md](lineage-dual-boot-install.md). The README
compares the two.

## What's in it

- **Nura:** the postmarketOS preview (kernel 7.3.1 r10, device package r15,
  Plasma 6.8), with the dual boot hook, `gts3-boot` and the shared clock.
- **Android:** our vendor (AOSP 15 source, Mesa, hardware video, camera
  through libcamera, sensors, GNSS) on the same kernel. Nothing from Samsung
  is in it: the tablet's own firmware is read from its partitions at every
  start.
- **OrangeFox** on the same kernel, as lk2nd's `orangefox` entry. Its /data
  is Android's data partition.
- **lk2nd v2:** a menu with the systems, `fastboot oem boot-label`.
- The **microSD card** is the storage both systems share. The clock is one
  for both.

## Tested on the merged kernel (SM-T820, no panel on the board)

- **Nura:** a clean image booted to the Plasma login, no failed services,
  and Wi-Fi came up (`wlan0`, the supplicant running);
- **Android:** reached `boot_completed`, with SurfaceFlinger, zygote and
  netd running, `/dev/qseecom` present and the task-profile cpusets in place;
- **OrangeFox:** its GUI on the virtual display, dual boot mode mapping all
  five partitions, /data on Android's data, the exFAT card, and the
  TrustZone stack alive (`qseecom version 0x1001000`);
- the zip installer run inside that OrangeFox.

## Not tested yet

- Anything on a real screen: lk2nd's menu, and the display in Nura, Android
  and OrangeFox. On a board with no panel the kernel retries the absent
  display; `video=DSI-1:d` (used by the Android and recovery entries) avoids
  it, but whether the panel itself lights up needs a tablet with a screen.
- **SM-T825:** its lk2nd v2 and Android on it. If it loops at the Samsung
  logo after installing, flash your old lk2nd from Download mode.
- The PC installer flashing a tablet.
- TWRP or OrangeFox builds on Samsung's kernel running the zip.
