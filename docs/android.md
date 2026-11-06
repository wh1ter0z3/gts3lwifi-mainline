# Android on the mainline kernel (branch `android`)

The kernel side of running Android (AOSP, LineageOS) on the Galaxy Tab S3
with the mainline kernel. Building the OS itself is not part of this repo.

## What the branch is

The 7.3.1 kernel of `main` (kernel.org 7.3.1, the msm8996 base patches
`m001`–`m071`, the device patches `0001`–`0170`, including the touch keys,
headphone jack, GPU memory clock and GPS),
plus:

| Patches | What |
|---|---|
| `0171`–`0175` | TrustZone apps: loading from firmware images, listeners, app sends from the qseecom pool, shutdown, and `/dev/qseecom` with the downstream ioctl ABI and DMA-BUF buffers (`CONFIG_QCOM_QSEECOM_DEV`) |
| `0176`–`0178` | Fingerprint sensor control (`CONFIG_INPUT_EGIS_ET5XX_CTRL`): Samsung's `/dev/esfp0` ABI and `/sys/class/fingerprint/fingerprint`, sensor type reported at probe as the stock kernel does |
| `0179` | TrustZone buffer pool as a 44 MiB CMA pool for the SCM device, exported as the DMA-BUF heap `qseecom@fd400000` |
| `0180` | `kernel/configs/gts3-android.config` |
| `0181` | Device tree node for the fingerprint control driver: reset and LDO enable on PM8994 MPP 2/8, data-ready on TLMM 131, the QUP6 SPI and AHB clocks |

Details and test results of `0171`–`0178`: `wip/qseecom/` on branch
`qseecom-wip` (the same work on 6.19.5).

## The config fragment

`kernel/configs/gts3-android.config` goes on top of the postmarketOS config:

    cp config-postmarketos-qcom-msm8996.aarch64 $O/.config
    scripts/kconfig/merge_config.sh -m -O $O $O/.config kernel/configs/gts3-android.config
    make O=$O olddefconfig

It adds binderfs, SELinux in the LSM list, file-based encryption,
fs-verity, dm-verity, userfaultfd, sw_sync, uhid, netd's BPF and netfilter
pieces, `/proc/config.gz`, and the TrustZone stack the vendor HALs use.
It is additive: postmarketOS still boots with it (SELinux stays without a
policy). Not in it, on purpose:

- `CONFIG_STATIC_USERMODEHELPER`: Android's base config wants it, but it
  turns off the kernel's module loader helper that postmarketOS uses. Add
  it in an Android-only build.
- `dm-default-key` (metadata encryption): only in Android's own kernel
  tree.

## What runs on top, and what it needs

- Keymaster, gatekeeper and the fingerprint HAL: Samsung's vendor HALs
  from the stock firmware (T820XXU3CTD5, the build the tablet's TrustZone
  apps come from) against `/dev/qseecom`. The vendor `libQSEEComAPI.so`
  allocates through ION, which mainline does not have; use the open one in
  `tools/libQSEEComAPI` (`qseecom-wip`), which has a bionic build mode.
- The TrustZone apps are loaded by name from
  `/lib/firmware/qcom/msm8996/samsung/gts3/tz/` (from the `apnhlos`
  partition; never in this repo).
- Graphics (freedreno, Mesa, drm_hwcomposer, minigbm), camera (libcamera's
  Android HAL), audio (tinyalsa), Wi-Fi, Bluetooth and sensors (IIO) use
  standard Linux interfaces this kernel already provides.

## Status

Built with the fragment and RAM-booted as postmarketOS on an SM-T820
(2026-11-05): boots to a running system with no failed units; Wi-Fi up.
On the running kernel:

| Check | Result |
|---|---|
| SELinux | in the active LSMs (`capability,selinux`), no policy loaded |
| binderfs | mounts, creates `binder`, `hwbinder`, `vndbinder` |
| ext4 | `encryption` and `verity` features |
| userfaultfd, sw_sync, `/proc/config.gz` | present |
| TrustZone pool | 44 MiB CMA, SCM's memory-region, heap `qseecom@fd400000` |
| `/dev/qseecom` | loads cmnlib and keymaster (app 6); commands answered, through the open libQSEEComAPI |
| Fingerprint control | driver loads by itself from the DT node; `/dev/esfp0` and `/sys/class/fingerprint/fingerprint` (`ET510`, type 3); power, clock and reset sequence runs |

No Android build has run on it yet.

The kernel package on this branch (7.3.1-r4, one above `main`'s r3) merges the fragment in
`prepare()`, so a postmarketOS build from the branch is the Android
kernel. To check one, on the tablet: `sudo sh tools/android-kernel-check.sh`
(features, TrustZone pool, keymaster through `/dev/qseecom`, fingerprint
control; it needs static builds of `qseecom-km` and `esfp0-test` next to
it for the last two, see the script's header).

Notes:

- Test boots: the APSS watchdog's maximum timeout is about 30 s, so use
  `systemd.watchdog_sec=30` (60 is rejected).
- Building on a host without `lld`: `HOSTLDFLAGS=-fuse-ld=bfd` (7.3 links
  host tools with lld when `LLVM=1`).
