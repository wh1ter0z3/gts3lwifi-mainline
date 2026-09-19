# Mainline Linux on the Samsung Galaxy Tab S3 Wi-Fi (SM-T820, gts3lwifi)

Early bring-up of mainline Linux / postmarketOS on the Galaxy Tab S3 Wi-Fi.

- SoC: Qualcomm APQ8096 (Snapdragon 820), PMICs PM8994 + PM8004
- Storage: eMMC (not UFS)
- Bootloader chain: Samsung S-Boot -> lk2nd -> Linux
- Kernel: [msm8996-mainline/linux](https://gitlab.com/msm8996-mainline/linux) `v6.19.5-msm8996`,
  packaged as postmarketOS `linux-postmarketos-qcom-msm8996`

## Status

| Feature | Status |
|---|---|
| Boots, 4 CPU cores | works |
| eMMC (HS400) | works |
| USB peripheral (NCM network, ACM serial, mass storage) | works |
| PM8994 regulators via RPM | works (only the rails needed so far) |
| postmarketOS initramfs + debug shell over USB | works |
| Full rootfs boot | being tested |
| Display (split-DSI AMOLED), touch, S-Pen | not started |
| GPU, video codec, audio, Wi-Fi/BT | not started (need Samsung-signed firmware) |
| Battery / charger (SM5705), Type-C controller (S2MM005) | no mainline drivers |

Known issue: the working test image currently reserves all TLMM GPIOs and
disables the CoreSight nodes (`dts/test/test-A.dts`). Without that the
firmware resets the tablet about 9 s into boot. Narrowing this down to the
real cause is in progress.

## Layout

- `dts/` - board device tree, plus test variants that `#include` it
- `pmaports/` - postmarketOS device package and the kernel patch
- `lk2nd/` - lk2nd device entries for the Wi-Fi and LTE models
- `scripts/` - regenerate the kernel patch, build tester packages
- `docs/` - tester instructions and boot logs

## lk2nd notes

Samsung's bootloader only accepts a boot image packed with the stock load
addresses (base 0x80000000, kernel +0x8000, ramdisk +0x2200000, tags
+0x2000000, page size 4096) and, so far, only with the stock device trees
appended. With stock device trees lk2nd shows "Unknown (FIXME!)" but works.
With such a large lk2nd image, never use `fastboot flash boot` from lk2nd:
it writes at a 512 KiB offset and would overwrite lk2nd. `fastboot boot` is safe.

Keys: Volume Up = PM8994 GPIO 3, Home = PM8994 GPIO 2, Volume Down = PON RESIN.

## Debugging without a display or UART

lk2nd can keep the kernel log in RAM across a reset:

    fastboot --cmdline "console=ttyMSM0,115200 lk2nd.pass-ramoops=zap" boot boot.img
    # after the reset, back in lk2nd:
    fastboot oem ramoops console
    fastboot get_staged console.txt

The AP debug UART is BLSP2 UART2 on TLMM GPIO 4 (TX) / 5 (RX), 1.8 V, 115200 8N1
(`dts/test/test-A-uart.dts`). Pad locations on the board are not known yet.

## Licence

Device tree files: BSD-3-Clause (see SPDX headers). The kernel patch applies to
the GPL-2.0 Linux kernel. Packaging files follow postmarketOS pmaports (MIT).
No firmware blobs, stock images or schematics are included in this repository.
