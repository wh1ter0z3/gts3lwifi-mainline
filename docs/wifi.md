# Wi-Fi: QCA6174 ("Rome") under mainline

## Status (2026-10-28): working

On a Tab S3 with no panel attached, booted from RAM with
`dts/test/test-wifi.dts` (merged into the main DTS on 2026-10-28):

- PCIe0 trains at boot (`PCIe Gen.1 x1 link up`), the chip enumerates as
  `168c:003e` (subsystem `0000:0000`);
- `ath10k_pci` reports **QCA6174 hw3.2** (chip_id 0x00340aff) and runs
  firmware **WLAN.RM.4.4.1-00309** (API 6) from linux-firmware, with
  Samsung's own board data;
- 2x2 (TX/RX antennas 0x3), HT MCS 0-15 on 2.4 GHz, VHT on 5 GHz;
- scans find networks on both bands with sensible signal levels, and
  NetworkManager manages `wlan0`.

Since then: association and throughput (an SM-T825Y reported 866.7 Mbit/s
VHT-MCS 9 80 MHz 2x2 at -42 dBm, and about 214 Mbit/s downloading), and
suspend: the link goes down and comes back on every suspend since kernel r14
(docs/suspend.md).

Logs: `docs/logs/wifi-coldboot.txt` (working boot) and
`docs/logs/wifi-firstboot-nolink.txt` (first attempt, no link).

## Wiring

From the stock DT (`qcom,cnss`, `qcom,wlan-rc-num = <0>`). It is the same as
the Xiaomi MSM8996 boards in mainline (`msm8996-xiaomi-common.dtsi`):

| Signal / rail | Where |
|---|---|
| Rome 3.3 V (stock `rome_vreg`, also `bt_vreg`) | fixed regulator enabled by PM8994 GPIO 9, 4 ms startup |
| WLAN_EN (stock `wlan_en_vreg`) | PM8994 GPIO 8 |
| BT_EN | PM8994 GPIO 19 |
| 32.768 kHz sleep clock | PM8994 GPIO 18, function 2 (Qualcomm MTP: "Rome Sleep Clock") |
| PERST#, WAKE#, CLKREQ# | TLMM 35, 37, 36 (`pcie0_state_on` in msm8996.dtsi) |
| I/O 1.8 V | PM8994 S4 |
| Crystal 1.8 V | PM8994 L30 |
| Core 1.3 V | PM8994 S3 |
| Antenna switch | PM8994 L18. msm8996 lists it at 2.7-2.9 V (`pm8994_l18_pin_ctrl`), but this board raises L18 to 3.3 V for the touch controller (`tsp_avdd`). The test DT used 2.85 V, the main DTS uses 3.3 V; Wi-Fi works with both |
| PCIe PHY | L28 0.925 V, L12 1.8 V (as on every MSM8996 board) |

ath10k does not manage the cnss rails, so the test DTS keeps L30, S3 and L18
always on.

## The catch: BT_EN must be low when the 3.3 V rail comes up

The first boot with the obvious DT (Xiaomi template) gave
`qcom-pcie 600000.pcie: Phy link never came up`. Everything on the SoC side
checked out against stock: rails, pin muxing, PHY PLL locked, PCS ready,
endpoint refclk drive on, `gcc_pcie_clkref_clk` (stock `pcie_0_ldo`) and
LN_BB enabled. Samsung's PCIe driver uses the stock Qualcomm PHY sequence
(their extra `pcie_sec_phy_init` is compiled out for gts3lwifi).

What the hardware said, using the tools in `tools/` (MMIO and PMIC registers
from userspace, link retrained by hand without rebooting):

- The root port's LTSSM sat in **Polling.Compliance** (0x03): a receiver was
  detected, but the chip never sent training sequences.
- With PERST# held low, or WLAN_EN low, or the 3.3 V rail off, no receiver
  was detected at all. So the chip was powered and following PERST#. It just
  never started transmitting.
- The bootloader leaves **BT_EN (PM8994 GPIO 19) high**. Stock configures it
  low until Bluetooth starts.

| Power-up sequence | Result |
|---|---|
| BT_EN high while the 3.3 V rail rises | Polling.Compliance (the failure seen at boot) |
| BT_EN low while the 3.3 V rail rises | link up (L0), chip enumerates, ath10k loads |
| clean power-up, then BT_EN raised | link stays up |
| BT_EN high, only WLAN_EN cycled after a clean power-up | link up |
| PHY values switched to Samsung's (CLK_SELECT 0x35, SSC off, SIGDET 0x1a, ...) | no effect either way |

So BT_EN only has to be low at the moment Rome's 3.3 V rail ramps. Bluetooth
can be enabled any time after that.

The fix in `dts/test/test-wifi.dts`:

- the 3.3 V regulator is **not** always-on. regulator-fixed drives its GPIO
  low at probe, so the rail starts off even if the bootloader left it on;
- `off-on-delay-us = <1000000>` makes the regulator core keep it off for
  1 s after probe (it records `last_off` at registration);
- its pinctrl state also drives BT_EN low and muxes the sleep clock;
- `vreg_wlan_en` has the 3.3 V regulator as `vin-supply`, and the PCIe driver
  enables it as `vddpe-3v3`. The sequence is 3.3 V, 4 ms, WLAN_EN, 70 ms,
  then PERST# release.

## Firmware

- `linux-firmware-ath10k` (gives `ath10k/QCA6174/hw3.0/firmware-6.bin`
  and `board-2.bin`; the device package depends on it).
- `board-2.bin` has no entry for this board (subsystem IDs are 0000:0000), so
  ath10k falls back to `board.bin`. Use Samsung's board data from the tablet
  (read-only, not in this repo): copy `/system/etc/firmware/bdwlan30.bin`
  (8124 bytes) to `/lib/firmware/ath10k/QCA6174/hw3.0/board.bin`.
  Calibration still comes from the chip's OTP (`cal otp` in the log).
- The postmarketOS kernel has `CONFIG_FW_LOADER_USER_HELPER_FALLBACK=y`, so
  each optional file ath10k probes for (`cal-pci-0000:01:00.0.bin`,
  `board-pci-0000:01:00.0.bin`) waits 60 s for a helper that never answers.
  Add `sysctl.kernel.firmware_config.ignore_sysfs_fallback=1` to the kernel
  command line. With it, Wi-Fi is up as soon as udev loads `ath10k_pci`.

## Power save (off by default since device r18)

With power save on (NetworkManager's default), the chip answers an idle ping
only after 100-400 ms: 300 ms reported on an SM-T825Y, 126 ms average (up to
403 ms) measured on an SM-T820. With it off, about 16 ms. The device package
turns it off in `/usr/lib/NetworkManager/conf.d/60-gts3-wifi-powersave.conf`
(`wifi.powersave = 2`). It costs some power while awake and connected; in
suspend the link is down anyway. To turn it back on, put
`[connection]` / `wifi.powersave = 3` in a file in
`/etc/NetworkManager/conf.d/`.

## Regulatory domain

The tablet doesn't set a country, so Wi-Fi starts in the world domain, which
limits the 5 GHz channels (the chip's OTP says GB, EEPROM regdomain 0x833a).
It depends on where the tablet is, so there is no default. Set your country's
two-letter code once, for example Thailand:

    echo "options cfg80211 ieee80211_regdom=TH" | sudo tee /etc/modprobe.d/cfg80211-regdom.conf

and reboot (or `sudo iw reg set TH` for this boot only). `iw reg get` shows
the domain in use.

## Open items

- **MAC address:** ath10k uses the address from the chip's OTP (a Qualcomm
  OUI), not Samsung's per-device address (kept in `/efs`). Pass it through a
  `local-mac-address` on a PCIe child node, or set it in NetworkManager.
- The link trains at Gen1. Check whether Gen2 works.
- Bluetooth (same chip, over UART) works: see docs/bluetooth.md.
- Move the nodes into the main DTS. The cmdline sysctl is in the device
  package's deviceinfo now.
