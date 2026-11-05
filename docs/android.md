# Android on the mainline kernel (branch `android`)

The kernel side of running Android (AOSP, LineageOS) on the Galaxy Tab S3
with the mainline kernel. Building the OS itself is not part of this repo.

## What the branch is

The 7.3.1 kernel of the `linux-7.3` branch (PR #10: kernel.org 7.3.1,
the msm8996 base patches `m001`–`m071`, the device patches `0001`–`0148`),
plus:

| Patches | What |
|---|---|
| `0149`–`0153` | TrustZone apps: loading from firmware images, listeners, app sends from the qseecom pool, shutdown, and `/dev/qseecom` with the downstream ioctl ABI and DMA-BUF buffers (`CONFIG_QCOM_QSEECOM_DEV`) |
| `0154`–`0156` | Fingerprint sensor control (`CONFIG_INPUT_EGIS_ET5XX_CTRL`): Samsung's `/dev/esfp0` ABI and `/sys/class/fingerprint/fingerprint`, sensor type reported at probe as the stock kernel does |
| `0157` | TrustZone buffer pool as a 44 MiB CMA pool for the SCM device, exported as the DMA-BUF heap `qseecom@fd400000` |
| `0158` | `kernel/configs/gts3-android.config` |

Details and test results of `0149`–`0156`: `wip/qseecom/` on branch
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

- Built and boot-tested as postmarketOS on an SM-T820: see below.
- No Android build has run on it yet.
