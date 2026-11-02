# Linux 7.3

This branch moves the kernel package from `v6.19.5-msm8996` to Linux 7.3.1, the
latest stable release on 2026-11-01. msm8996-mainline has no tag past 6.19.5, so
the base is the kernel.org tarball and we carry their patches ourselves.

## Merge order

Merge this after everything else in flight. The branch is stacked on `gps` and
`sensors`, neither merged yet. Whatever lands on main first has to be redone on
top of this: add the patch, then regenerate it with the steps below. The exception
is `mdp5-resume`: its patch 0148 is already here.

## Patches

| Files | What | Count |
|---|---|---|
| `m001`-`m071` | msm8996-mainline's commits on `v6.19.5`, rebased | 70 |
| `0001`-`0148` | ours, numbers unchanged, with gaps where a patch dropped | 112 |

To regenerate: `git format-patch` each commit of a branch on `v7.3.1`. The
order is the order in `APKBUILD.pmaports`. To move to a newer stable, rebase
that branch, then update `pkgver`, the checksums and the config.

## Dropped: already in 7.3.1

| Was | Mainline | Subject |
|---|---|---|
| 0056 | 185cd1d8cb | dt-bindings: Input: Add Wacom W9000-series penabled touchscreens |
| 0057 | acaefbacc9 | Input: Add support for Wacom W9000-series penabled touchscreens |
| 0064 | 9a29ee801f | Input: rmi4 - use platform data instead of query, when available |
| 0086 | ec35c19696 | usb: gadget: f_ncm: Fix net_device lifecycle with device_move |
| 0087 | e002e92e88 | usb: gadget: u_ether: Fix NULL pointer deref in eth_get_drvinfo |
| 0105 | 120134fe75 | slimbus: qcom-ngd-ctrl: fix OF node refcount |
| 0106 | 8663e8334d | slimbus: qcom-ngd-ctrl: Fix up platform_driver registration |
| 0107 | 2c22ff152d | slimbus: qcom-ngd-ctrl: Fix probe error path ordering |
| 0108 | 960b53a3f7 | slimbus: qcom-ngd-ctrl: Correct PDR and SSR cleanup ownership |
| 0109 | 2a9d50e9ea | slimbus: qcom-ngd-ctrl: Register callbacks after creating the ngd |
| 0110 | 07c564ea5f | slimbus: qcom-ngd-ctrl: Initialize controller resources in controller |
| 0111 | 6a003446b7 | slimbus: qcom-ngd-ctrl: Balance pm_runtime enablement for NGD |
| 0112 | 55f2ea9ff8 | slimbus: qcom-ngd-ctrl: Avoid ABBA on tx_lock/ctrl->lock |
| 0113 | 8a066a81ee | ASoC: qcom: q6asm: drop DSP responses for closed data streams |
| msm8996 | none | slimbus: qcom-ngd-ctrl: Register SSR notifier after all initialization steps: 7.3.1 registers it at the end of probe |

## Smaller, or changed to fit 7.3

- **0011** only resets `queue_ptr` now, and is renamed to match. Mainline closes
  a stopped stream before opening it again since cee3e63e71.
- **0123**, `drm/msm`: sets `vm_shared` in 7.3's `kernel_managed` fallback. Our
  6.19 backport note is gone from the patch, since 7.3 has 00dfa76bdfc2. 0122 and
  0123 stay: they are not in 7.3.1.
- **Panels** (0002, s6e3fa3, s6e3fa5, sw43101, Sony JDI): `devm_drm_panel_alloc()`,
  because `drm_panel_init()` is static now.
- **0002's follow-ups:** `drm_of_get_panel_orientation()`, and the ELVSS patch
  imports the `IIO_CONSUMER` namespace.
- **`MIPI_DSI_CLOCK_DEFERRED_HS`** moved to bit 14. Bit 13 is mainline's
  `MIPI_DSI_MODE_DSC_ALL_SLICES_IN_PKT`.
- **DSI bridge enable:** `.atomic_enable`, since 7.3 made the bridge ops atomic.
- **`qrtr_device_id`** lives in `include/linux/device-id/qrtr.h`, since
  `mod_devicetable.h` was split up.
- **Venus EOS:** keeps mainline's `IS_IRIS2` check on the quirk.
- **`msm8996-*` boards:** OnePlus and capricorn use the single TE pin state left
  by 778b5aa8a2.
- **msm8996 haptics binding:** now `qcom,pmi8998-haptics.yaml`, because mainline
  took `qcom,spmi-haptics.yaml` for a different block (HAP530).
- **`qcom-smbchg`:** the AICL warning printed an uninitialized variable.
- **`qcom_smgr`, `qcom_sns_reg`:** `QMI_DATA_LEN` fields are `u32` now (mainline
  fe099c387e), so the length fields changed type. Without this, QMI encoding fails
  and the sensors never appear.
- **Board DT:** `cdc_reset_active` drops `bias-pull-down`, because mainline's pin
  state now sets a bias of its own and two are rejected. The pin is driven high.

## New patches

None of these are in 7.4-rc1.

- **0146 `net: qrtr`:** since mainline 544d85de4d, a packet other than HELLO fails
  until our HELLO has gone out. If the remote's HELLO gets to the name server first,
  the service announcement is lost, and the modem crashes about 40 s later.
- **0147 `PCI: qcom`:** since mainline 2cc0e7454c, suspend cuts vddpe-3v3, which on
  this board is the Wi-Fi chip's power. The link never returned, and Wi-Fi stayed
  dead until reboot. This keeps the link up during suspend, as 6.19 did. Why it
  doesn't retrain is unknown: holding BT_EN low doesn't help.
- **0148 `drm/msm/mdp5`:** not caused by 7.3, 6.19 has the same bug. The second
  suspend after a boot failed at `prepare`. See [suspend](suspend.md) and the fork
  branch `mdp5-resume`, which carries it for 6.19.

## Config

`make olddefconfig` on the 6.19.5 config. Worth knowing:

- `CRYPTO_DEV_QCE` is off: 7.3 marks it `BROKEN` on `ARCH_QCOM`. The CPU's crypto
  extensions do the work.
- `MULTIPLEXER`, `CRYPTO_CMAC`, `CRYPTO_LZ4` and `NETFILTER_NETLINK` went from
  module to built-in, selected by something built-in.

## Device trees

The compiled T820 and T825 trees match r10 except for a new disabled SPI node
(`spi@75b9000`, from mainline) and its pin states.

## Test status

SM-T820, 2026-11-02, on a package built with `pmbootstrap build`. Nobody has booted
it on an SM-T825: only its device tree is compile-tested.

| Check | Result |
|---|---|
| Install | `apk add` runs `boot-deploy`, the image boots normally, and `apk add` of r10 goes back |
| Boots with 0146 | 13 of 13 clean: no announce failures or modem crashes. Without it, a crash on 2 of 4 |
| Suspend, 20 s by RTC | 4 of 4 in a row, as RAM boot and as installed. Wi-Fi passes traffic after, Bluetooth stays powered |
| Sensors | accelerometer, gyroscope, magnetometer, light, ADC; orientation reads normal |
| GNSS | session starts and stops; no fix indoors, same as 6.19 |
| Venus decode | H.264 and HEVC both match a software decode |
| Cameras | both capture |
| Audio, by ear | speakers and headphones sound clean. The mic works but is quiet (peaks near 5 %): gain was never tuned, see `docs/audio.md` |
| Panel, by eye | picture correct, brightness works, on a boot with the `pclk` warning below |
| Rotation, touch, pen | all good by hand |
| Power button, by hand | sleeps and wakes twice in a row, picture and touch correct after |
| KWin, modem, ADSP, SLPI, USB gadget | running |

Sensors, GNSS and cameras ran on the build before 0147 and 0148, which don't touch them.

**Not checked by hand:** the keyboard cover, the camera preview by eye, and a GPS fix
at a window.

### Worse than 6.19, or maybe

From the journal, 33 boots of 6.19.5 and 17 of 7.3.1:

| | 6.19.5 | 7.3.1 | Fisher p |
|---|---|---|---|
| `pclk0_clk_src: rcg didn't update its configuration` | 12 | 12 | 0.036 |
| Bluetooth `Opcode 0x0c03 failed: -110` | 2 | 4 | 0.16 |

The clock warning looks like a real increase, cause unknown. It looks harmless: the
DSI clocks end up at the right rates, and the picture was correct on a boot with it.

The Bluetooth timeout comes at the end of the second setup during boot, with the same
timeline on both kernels, so it isn't new. These numbers can't say whether it got more
frequent. That boot has no controller, and rfkill or restarting `bluetooth` doesn't
help. `rmmod hci_uart; modprobe hci_uart` brings it back. 15 of those re-probes in a
row never hit the timeout, so it seems to take the load of a boot to trigger it.

`wcd9335: overflow error on TX port 1` appeared once at the start of a capture, on 2
boots of 7.3.1 and on none of 6.19.5. We don't know how many of the 6.19.5 boots
recorded anything. The recording played back fine.

### Better than 6.19

HEVC decode works. The same 1080p clip hangs on r10 until the test's 90 s timeout
(`docs/venus.md`) and matches a software decode here. We haven't looked for the
commit that fixed it.
