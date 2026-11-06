# USB-C: OTG, USB PD sink at 9 V, USB 3.0

## Status (2026-10-31): works, main DT

Kernel patches 0078-0087 and the main DT. The kernel config needs nothing
new: `CONFIG_TYPEC_S2MM005`, `CONFIG_USB_DWC3_DUAL_ROLE`,
`CONFIG_USB_ROLE_SWITCH` and `CONFIG_PHY_QCOM_QMP_USB` are already set.

| Feature | State |
|---|---|
| Host (OTG): hubs, storage, keyboards, mice, Ethernet, phones | Works, VBUS from the charger's boost, 1.5 A limit |
| Device (gadget) | Works as before; also before `s2mm005` loads |
| Role and power role swaps (docks, PD hubs) | Works: the firmware does them, the driver follows |
| PD sink at 9 V | Works: 9 V / 1.65 A when the source offers it, else 5 V / 3 A |
| USB 3.0 host | Works, 5 Gbit/s, both plug orientations |
| USB 3.0 device | Untested (no known USB 3 C-to-C cable); falls back to high speed |
| Variable or PPS supplies | Not picked; fixed supplies only |

## Patches

- 0078-0079: the SM5705 charger's boost as a regulator, `usb-otg-vbus`
  (operation mode 7, 5.1 V, 1.5 A limit, Samsung's OTG-cable row of the
  mode table). The torch keeps working from the boost; OTGFAIL (over
  current) turns VBUS off and sends `REGULATOR_EVENT_OVER_CURRENT`.
- 0080-0081: the S2MM005 driver takes the power and data roles from the
  connector. On a dual role connector with a `vbus-supply` it switches
  VBUS while the firmware is the source (and through power role swaps),
  and sets the dwc3 role switch from the firmware's DFP flag: host, device
  or none. The VBUS regulator is looked up when VBUS goes on, not at
  probe, because the charger in turn waits for this port's power supply.
  It also sends option command `03 42` at probe and after a firmware reset
  (see below).
- 0082-0083: with `sink-pdos` on the connector, the driver reads the
  source's capabilities once the contract is in place and asks the
  firmware for the fixed supply with the most power at an allowed voltage
  (TCPM's rule: on a tie, the higher voltage), once per capabilities
  message, a second after the first contract.
- 0084: the charger lets a Type-C contract above 5.5 V decide the input
  limit even when the MUIC's BC1.2 limit is higher.
- 0085 (`msm8996.dtsi`): the USB 3.0 controller reset, see below.
- Dropped with 7.3: 0086-0087, upstream's fixes for the NCM gadget's network
  device across role switches (ec35c1969650, e002e92e88e1). The 6.19.5 gadget
  kept a freed dwc3 gadget as the parent of `usb0` and oopsed after several
  hundred host/device switches.
- Board DT: the charger's `usb-otg-vbus` regulator; the connector as dual
  role with `vbus-supply`, `sink-pdos` (5 V / 3 A, 9 V / 1.65 A: the input
  limit Samsung uses for its 15 W chargers, `HV_MAINS` in
  msm8996-sec-gts3llte-common-battery.dtsi) and high speed and SuperSpeed
  ports to dwc3; dwc3 in otg mode with the role switch and both PHYs, the
  QMP PHY on, no UTMI pipe clock and no high-speed limit; PM8994 GPIO 14
  (the USB 3.0 redriver enable) at S4 and high.

## How the S2MM005 firmware needs to be driven

Facts from Samsung's GPL kernel for this tablet (`drivers/ccic/s2mm005_cc.c`,
`s2mm005_pd.c`, `usb_typec_manager_notifier.c`) and, for the state numbers,
the firmware image in the same tree (`BOOT_FLASH_FW_BOOT6.h`, read for
facts only).

- **Source role needs `03 42`.** The firmware toggles and attaches as the
  source by itself, but only negotiates once the AP has started its policy
  manager with option command `03 42`. Samsung's driver sends it (after
  `03 31`, alternate modes) when its USB notifier comes up and after a
  firmware reset; the flag lives in the chip's RAM until the next reset.
  Without it the source path waits for new capabilities instead, and in
  that state VBUS present means a detach: our own boost ended every
  attach about 200 ms in. With it: a PD sink goes through SRC_Ready, a
  sink that never answers (a flash drive on a plain adapter, a phone that
  doesn't speak PD) ends in SRC_Disabled, still attached and powered.
- **9 V:** the firmware always requests the first PDO (5 V). `03 03 N`
  stores the wanted object position and `03 02 11` (PD state 17,
  SNK_Wait_for_Capabilities) makes it fetch the capabilities and request
  again, as Samsung's `s2mm005_select_pdo()` does. A detach puts the
  position back to 1. The 1 s delay matters with docks: the Sabrent dock
  swaps data roles about 200 ms after its first contract and gives up if
  a renegotiation is in the way.
- The vendor driver resets the chip at every shutdown and sends a
  reconnect at probe; this driver does neither and hasn't needed to.

## USB 3.0

Two things kept SuperSpeed down:

- **The controller was still on the UTMI pipe clock.** lk2nd brings USB up
  in its high-speed-only mode (`usb_wrapper_hsonly_mode()`, used when its
  QMP PHY setup fails or with `lk2nd,usb-high-speed-only`), which sets
  PIPE_UTMI_CLK_SEL and PIPE3_PHYSTATUS_SW in QSCRATCH_GENERAL_CFG
  (0x06af8808 read 0x0d). The dwc3-qcom glue only ever sets these bits,
  and `msm8996.dtsi` had no reset for the controller, so they stayed: as a
  host the SuperSpeed port sat in RxDetect, and as a gadget with
  `super-speed` the link stayed in SS.RxDetect and never fell back to high
  speed, so the PC saw nothing. Patch 0085 adds `GCC_USB_30_BCR`, as
  msm8998 and sdm630 have it and as Samsung's dwc3-msm resets the block
  (the BCR of `gcc_usb30_master_clk`); the register reads 0x04 after
  probe. This affects any msm8996 board booted that way.
- **The redriver needs its enable**, PM8994 GPIO 14 (Samsung's
  `qcom,gpios_redriver_en`). With it low the SuperSpeed port stays in
  RxDetect and everything enumerates at 480M. The pin's supply defaults to
  VPH_PWR; Samsung's DT sets S4 (1.8 V), and the board DT does that in the
  same pinctrl state before driving it high (the PMIC GPIO driver writes
  the supply before the mode and level).

The S2MM005 firmware selects the lane pair for the plug orientation by
itself (its SFR 0xf0001080 / 0x1084 bit 10 follows the plug), so there is
no orientation switch to model.

## Tests (SM-T820, 2026-10-30 to 31)

- Sink: PC port (device mode, network gadget), powered PD hub (5 V / 3 A).
- Source / host: USB-C dock with its supply pulled (SRC_Ready in 0.6 s;
  hub, flash drive at 17.6 MB/s, receiver); flash drive on a plain OTG
  adapter (SRC_Disabled, 18.5 MB/s); Android phone, C to C (charges from
  the tablet, enumerates, ten plug cycles clean); Anker USB-C hub with
  RTL8153 (1000/full), two hubs, keyboard, mice.
- Power role swap: Apple USB-C Digital AV Multiport Adapter on its own
  supply: attach as source, the adapter swaps to source after ~10 s, the
  driver drops VBUS, the tablet stays host.
- 9 V: Sabrent dock on a PD 3.0 supply (5/9/15/20 V): 5 V / 2.6 A, then
  9 V / 2.44 A a second later, host role kept. Apple USB-C charger (PD 2.0,
  5/9/15/20 V): 9 V / 3 A contract, input limit 1.65 A, "15 W source,
  charging at up to 2700 mA". Samsung's 25 W PD charger (which detached
  0.4 s after the 9 V Accept in the earlier attempt in
  [battery](battery.md)) has not been tried with these patches.
- USB 3.0: Anker USB-C hub (VL817), a USB 3.1 hub behind it and the
  RTL8153 at 5000M in both plug orientations; gadget on a PC at 480M (the
  link asks for SuperSpeed and falls back, as with a cable without
  SuperSpeed pairs).

## Open

- **Sleep.** The redriver stays enabled all the time; Samsung's kernel
  enables it only while something is attached (`usb_notify.c`). With the
  screen on its idle current was below what the fuel gauge resolves
  (single readings jump by hundreds of mA). Once suspend is reliable:
  check that dwc3 and the QMP PHY suspend cleanly and measure the suspend
  drain with GPIO 14 high and low; if it costs anything, switch it with
  the attach state.
- Seen once: OTGFAIL (1.5 A) when a hub with a keyboard, mice and Ethernet
  behind it was plugged in; a replug was fine.
- Unbinding the charger while the port sources VBUS still pulls the
  regulator from under the S2MM005 driver. The clean fix would be the
  charger as an MFD (charger, regulator and LED cells) that doesn't depend
  on the port.
- Variable and PPS supplies are not picked (fixed only).
- SuperSpeed as a gadget: the USB-C to A cable tried on a PC's USB 3 port
  (2026-10-31) has a white A plug, a USB 2 cable, so its high speed link
  says nothing; a cable known to carry SuperSpeed is still to be tried.
