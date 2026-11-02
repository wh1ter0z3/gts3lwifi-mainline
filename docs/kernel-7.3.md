# Linux 7.3

This branch moves the kernel package from `v6.19.5-msm8996` to Linux 7.3.1, the
latest stable release on 2026-11-01. msm8996-mainline has no tag past 6.19.5, so
the base is the kernel.org tarball and we carry their patches ourselves.

## Merge order

Merge this after everything else in flight. The branch is stacked on `gps` and
`sensors`, neither merged yet. Whatever lands on main first has to be redone on
top of this: add the patch, then regenerate it with the steps below.

## Patches

| Files | What | Count |
|---|---|---|
| `m001`-`m071` | msm8996-mainline's commits on `v6.19.5`, rebased | 70 |
| `0001`-`0145` | ours, numbers unchanged, with gaps where a patch dropped | 109 |

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

Builds with `pmbootstrap build` (7.3.1-r0), no new warnings. Both device trees
compile. **Not booted yet**: the tablet was asleep when this was pushed. Nobody has
run it on an SM-T825.
