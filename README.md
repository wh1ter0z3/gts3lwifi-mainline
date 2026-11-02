# Mainline Linux on the Samsung Galaxy Tab S3

Mainline Linux / postmarketOS on the Galaxy Tab S3. One build runs on both models, Wi-Fi and LTE.

- SoC: Qualcomm APQ8096 (SM-T820) / MSM8996 (SM-T825), Snapdragon 820; PMICs PM8994 + PM8004
- Storage: eMMC (not UFS)
- Boot chain: Samsung S-Boot -> lk2nd -> Linux
- Kernel: Linux 7.3.1 with the [msm8996-mainline](https://gitlab.com/msm8996-mainline/linux) patches rebased
  (see [docs/kernel-7.3.md](docs/kernel-7.3.md)), packaged as postmarketOS `linux-postmarketos-qcom-msm8996`
  with our patches (`pmaports/`)

## Models

One kernel and one device package boot both models: the package carries both
device trees and lk2nd picks the one whose msm-id (291 SM-T820, 246 SM-T825)
and board-id (board revisions 00/02/04/05, all listed) match the tablet. The SM-T825
device tree (`dts/msm8996-samsung-gts3llte.dts`) includes the SM-T820 one and
only changes the SoC ID, the modem memory and the pins TrustZone may own.

| Model | State |
|---|---|
| SM-T820 (Wi-Fi, `gts3lwifi`) | main target, everything below is tested on it |
| SM-T825 (LTE, `gts3llte`) | boots to KDE (first report 2026-10-31); modem (LTE, SIM) and GPS not supported yet; top speakers left out until checked |

## Status

Works = tested on hardware. Details and test notes are in `docs/`.

| Area | Status | Notes |
|---|---|---|
| **System** | | |
| Boot, CPU (4 cores), cpufreq | Works | |
| CPU idle | Works | cluster and system retention; deeper states refused by the firmware ([cpuidle](docs/cpuidle.md)) |
| Thermal | Works | [thermal](docs/thermal.md) |
| eMMC | Works | HS400 |
| microSD | Works | UHS-I (SDR104); hot-plug not tested |
| Reboot, power-off | Works | through PS_HOLD, PSCI hangs in Samsung's TrustZone ([reboot](docs/reboot.md)) |
| Suspend | Partial | s2idle; the Wi-Fi fix (patch 0037) is untested; no deep sleep yet ([suspend](docs/suspend.md)) |
| **Display and input** | | |
| Display | Works | colours, brightness, blank/unblank; HBM (outdoor brightness) through the panel's `hbm` attribute ([display](docs/display.md)) |
| GPU (Adreno 530) | Works | freedreno, OpenGL ES 3.1 ([gpu](docs/gpu.md)) |
| Touchscreen | Works | [input](docs/input.md) |
| S-Pen | Works | pressure, hover, side button ([input](docs/input.md)) |
| Power, Volume, Home keys | Works | |
| Book Cover Keyboard | Works | hotplug through the MUIC; a key press wakes the tablet ([input](docs/input.md)) |
| Back/Recent touch keys | Works | Recent opens Present Windows; lights time out ([input](docs/input.md)) |
| **Connectivity** | | |
| Wi-Fi | Works | 2.4 and 5 GHz ([wifi](docs/wifi.md)) |
| Bluetooth | Works | [bluetooth](docs/bluetooth.md) |
| USB device mode | Works | network, serial, storage |
| USB host / OTG, USB 3 | Works | 5 Gbit/s as a host ([usb](docs/usb.md)) |
| USB-C power (PD) | Works | 5 V / 3 A or 9 V / 1.65 A sink, 5 V / 1.5 A source ([usb](docs/usb.md)) |
| GPS / GNSS | Works on the SM-T820 | through geoclue (modem DSP in the main DT, gnss-share `qmi_loc` driver), first fix 2.5-8 min after boot. Off on the SM-T825 until tested ([gnss](docs/gnss.md)) |
| LTE modem, SIM (SM-T825) | Not yet | planned |
| **Power** | | |
| Battery gauge | Works | [battery](docs/battery.md) |
| Charging | Works | Samsung's 4.30 V and temperature limits, up to 2.7 A; opt-in 9 V AFC / QC 2.0 |
| Flash LED | Works | as a torch |
| **Multimedia** | | |
| Speakers (4), headphones, microphones | Works | headphone jack detection and headset mic not yet ([audio](docs/audio.md)) |
| Video decode / encode (Venus) | Partial | H.264, VP8, VP9 decode; H.264 and HEVC encode, 4K30 HEVC from the camera; HEVC decode fails ([venus](docs/venus.md)) |
| Cameras (front, rear) | Works | rear 13 MP with autofocus, front 5 MP, through libcamera (`pmaports/libcamera` for AF) ([camera](docs/camera.md)) |
| **Sensors** | | |
| Accelerometer, gyroscope, magnetometer | Works | auto-rotate with iio-sensor-proxy; KWin rotation fixes in `pmaports/kwin`; no compass heading yet ([sensors](docs/sensors.md)) |
| Light sensor | Works | lux through iio-sensor-proxy; KWin auto-brightness is off by default and needs training (patch 0077, [sensors](docs/sensors.md)) |
| Cover (hall) sensor | Not yet | test DT `dts/test/test-hall.dts`; waits for reliable suspend |
| Grip sensor (SM-T825) | Not yet | test DT `dts/test/test-sar.dts`; nothing reads it without the modem |
| Fingerprint (Home key) | Blocked | its pins are owned by Samsung's TrustZone |
| **Software** | | |
| postmarketOS with Plasma (Wayland) | Works | installed on the eMMC behind lk2nd ([pmos-install](docs/pmos-install.md)) |
| Widevine DRM | Partial | L3 plays in Waydroid and Flatpak Firefox; L1 not on mainline ([widevine](docs/widevine.md)) |
| Waydroid (Android 13) | Works | Android apps on Linux, GPU-accelerated; an extra, not a stock Android replacement; optional `-waydroid` subpackage ([waydroid](docs/waydroid.md)) |

## Layout

- `dts/` - device trees for both models (the SM-T825 one includes the SM-T820 one), plus test variants; the panel temperature channel is still test-only
- `pmaports/` - postmarketOS device package (with the optional Waydroid subpackage and GNSS), the kernel patches, libcamera with autofocus, KWin with rotation fixes, iio-sensor-proxy with a restart fix, and gnss-share with a QMI LOC driver
- `lk2nd/` - lk2nd device entries for the Wi-Fi and LTE models
- `scripts/` - regenerate the kernel patch, build tester packages
- `tools/` - on-device debugging aids: MMIO and PMIC (SPMI) register access, manual PCIe link retrain, TLMM pin sampling, RT5508 amplifier probe, GNSS test helper and QMI LOC client
- `kernel/` - out-of-tree kernel additions (panel, fuel gauge, charger and MUIC drivers, bindings, Kconfig snippet)
- `docs/` - notes per subsystem, the postmarketOS install, tester instructions and boot logs

## lk2nd

Samsung's bootloader only accepts lk2nd when it is packed like a stock boot
image ([docs/lk2nd.md](docs/lk2nd.md), packing script in `scripts/`). There
is one image per model for now: `lk2nd-gts3lwifi.img` (SM-T820, in the GitHub
releases) and `lk2nd-gts3llte.img` (SM-T825).

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
