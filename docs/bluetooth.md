# Bluetooth: QCA6174 over UART

## Status (2026-10-28): working

With `dts/test/test-bt.dts` (merged into the main DTS on 2026-10-28) (built on the Wi-Fi test DT):

- `hci_qca` finds the ROME controller (product 0x8, SoC 0x44, ROM 0x302,
  version 0x00440302) on BLSP1 UART2 at 3 Mbaud with RTS/CTS;
- it loads `qca/rampatch_00440302.bin` and `qca/nvm_00440302.bin` from
  linux-firmware, and setup completes in about 1.3 s;
- after the address fix below, BlueZ reports the controller powered, and a
  scan finds the devices around.

Not checked yet: pairing, audio (A2DP/SCO), coexistence under Wi-Fi load.

## Wiring

From the stock DT. It is the same as apq8096-db820c and the OnePlus 3:

| | |
|---|---|
| UART | BLSP1 UART2 at 0x7570000 (mainline `blsp1_uart2`), TLMM 41-44 |
| BT_EN | PM8994 GPIO 19 (stock `qca,bt-reset-gpio`) |
| Sleep clock | PM8994 GPIO 18 function 2, 32.768 kHz (shared with Wi-Fi) |
| 3.3 V | the Rome regulator on PM8994 GPIO 9 (stock `bt_vreg` = `rome_vreg`) |

The 3.3 V rail and the sleep clock come from the Wi-Fi setup. BT_EN must stay
low until the 3.3 V rail is up (see docs/wifi.md). `hci_qca` drives it low
at probe and only raises it when hci0 is powered, long after the PCIe probe
has powered the chip.

```dts
&blsp1_uart2 {
	label = "BT-UART";
	uart-has-rtscts;
	status = "okay";

	bluetooth {
		compatible = "qcom,qca6174-bt";
		enable-gpios = <&pm8994_gpios 19 GPIO_ACTIVE_HIGH>;
		clocks = <&rome_sleep_clk>;	/* fixed-clock, 32768 Hz */
	};
};
```

## Bluetooth address

The NVM file only holds a placeholder address (00:00:00:00:5A:AD), so
the kernel leaves hci0 unconfigured (`DOWN RAW`, "No default controller
available" in bluetoothctl). Samsung stores the per-device address as text in
`bluetooth/bt_addr` on the `efs` partition.

The device package sets it at boot (`pmaports/device-samsung-gts3lwifi`):

- `90-gts3-bdaddr.rules` starts `gts3-bdaddr.service` when hci0 appears.
  It is a service rather than a udev `RUN`, because systemd-udevd's sandbox
  does not allow Bluetooth sockets;
- `gts3-bdaddr` mounts efs read-only with `noload`, reads the address and
  runs `btmgmt public-addr`. It retries for up to 30 s, because hci0 appears
  before `hci_qca` has finished loading the firmware.

## Firmware

linux-firmware's ROME 3.2 files work. Samsung's own pair is in
`/system/etc/firmware` (`rampatch_tlv_3.2.tlv`, 13 KB, and `nvm_tlv_3.2.bin`,
1960 bytes). They differ from linux-firmware's (68 KB patch, 2041-byte NVM).
Samsung's NVM may carry board tuning (TX power, PCM/I2S for voice). Try it
if audio or range turns out poor.

`Frame reassembly failed (-84)` shows once per power-up, right after the
patch download, when the UART changes speed. Setup completes regardless.

## Power and suspend (kernel r18)

Bluetooth holds the QCA6174's 3.3 V rail (`vddpa-supply`, stock's `qca,bt-vdd-pa-supply`): the PCIe
host powers Wi-Fi off in suspend, and with it the rail Bluetooth needs. The efs address goes into the
NVM during the firmware download instead of a command afterwards, which the controller sometimes
dropped the next command after. Details and test results in [suspend](suspend.md).

