# Input: touchscreen and S-Pen on the Galaxy Tab S3

Both digitizers sit on top of a landscape panel (2048 x 1536) in a
portrait device. DRM expects touch input in the panel's own frame
(drm_connector.c, "panel orientation"): user space that turns the
picture to portrait applies the same turn to the touch input. So the
DT flags below map each digitizer to the landscape panel frame, origin
at the panel's top-left, and the panel's `rotation = <270>`
(docs/display.md, round 20) tells user space about the portrait
mounting.

## Touchscreen (Synaptics S5807, RMI4)

Stock (Samsung GPL kernel, `msm8996-sec-gts3lwifi-r04.dtsi`): the
`synaptics_dsx2` driver reports `max_coords = <1536 2048>` with no
flip properties set, and stock Android rotates the display by 270
degrees (`SF_PRIMARY_DISPLAY_ORIENTATION := 270` in LineageOS's
`BoardConfigCommon.mk`). So the raw coordinates are Android's portrait
frame, and in the panel's frame (x, y) = (y_raw, 1535 - x_raw).

RMI4 applies `touchscreen-inverted-x/y` before `touchscreen-swapped-x-y`
(`rmi_2d_sensor_abs_process()`), so `dts/test/test-touch.dts` sets:

    touchscreen-inverted-x;
    touchscreen-swapped-x-y;

With them the input device reports X 0-2047 and Y 0-1535 (seen with
`evtest`, 2026-10-29). Confirmed with a finger the same day: five taps
near the corners and the centre landed within 19 px of the targets
(mean 13 px).

Test: `evtest` on the touchscreen's event device, then tap near the
corners of the screen. In the panel frame (the boot logo's top is the
panel's left edge, so hold the tablet in landscape with the home button
on the right): top-left is X 0 / Y 0, top-right X 2047 / Y 0,
bottom-left X 0 / Y 1535.

The input device's resolution was 13 and 7 units per mm: `rmi_f12`
overwrote `touchscreen-x-mm`/`-y-mm` with its own estimate, pitch times
electrode count, which pairs the 49 RX electrodes (4 mm pitch) with x
while on this sensor they run along y (it read 196 x 148 mm for a
1535 x 2047 sensor). The upstream fix is 9a29ee801f52 (platform
data first), and `dts/test/test-touch.dts` gives
the panel's active area in the sensor's frame, 147 x 196 mm: 10 units
per mm on both axes (seen 2026-10-29).

## S-Pen (Wacom W9019, Samsung firmware)

Stock DT: `wacom@56` on `i2c_12` (BLSP2 QUP6 at 0x75ba000, GPIO 87/88:
mainline `blsp2_i2c6`), compatible `wacom,w90xx`, `wacom,ic_type =
<9019>`, interrupt GPIO 34, pen detect GPIO 117, firmware write enable
GPIO 24, 3.3 V from a fixed regulator on GPIO 126 (boot-on), boot loader
at 0x09, `max_coords = <19661 14746>`, `max_pressure = <4095>`,
`invert = <1 0 1>` (x flipped, then x and y swapped).

Samsung's firmware does not speak the HID-style protocol of mainline's
`wacom_i2c`. From Samsung's `wacom_tab` driver: command 0x2a returns a
15-byte query (header 0x0f, then maximum x, y, pressure and the firmware
version, big endian), and each interrupt has a 14-byte packet to read:

| byte | meaning |
|---|---|
| 0 | bit 7 in range, bit 6 eraser, bit 5 side button, bit 4 tip |
| 1-2, 3-4 | x, y (big endian) |
| 5-6 | pressure (big endian) |
| 7 | height |
| 8, 9 | tilt x, tilt y (signed) |
| 12 | bit 7: soft key event instead of a pen sample |

Linux 7.3 has a driver for this protocol family, `wacom_w9000`
(W9002, W9007A; Hendrik Noack, Ferass El Hafidi).  It came in as
185cd1d8cb7f (binding) and acaefbacc997 (driver).  Patches 0058-0062 add the W9019 to it: compatible
`wacom,w9019`, which requires `vdd-supply` and an active-high
`flash-mode-gpios` held low; the split query (send 0x2a, wait 100 ms,
read 15 bytes, three tries 100 ms apart, as `wacom_i2c_query()`); the
14-byte report with the soft-key marker; the 10 ms off interval; KUnit
tests (14/14 on this tree).  Patch 0063 sets INPUT_PROP_DIRECT, which
the upstream driver did not.  Enable with `CONFIG_TOUCHSCREEN_WACOM_W9000`.

The driver reports ABS_X/Y, pressure, BTN_TOOL_PEN or BTN_TOOL_RUBBER,
BTN_TOUCH for the tip and BTN_STYLUS for the side button.  Hover shows as
BTN_TOOL_PEN without BTN_TOUCH; the W9019 variant reports no distance or
tilt, since the stock source gives no unit for them.  The digitizer is
powered only while the input device is open.  The axes go through the
common `touchscreen-inverted-x/y` and `touchscreen-swapped-x-y`, and
`touchscreen-x-mm`/`-y-mm` set the resolution (196 x 147 mm, the panel's
active area: 100 units per mm).

Axes: stock maps (x, y) to portrait (y, 19661 - x), and the panel's
frame is portrait turned back by 270 degrees, so in the panel's frame
the pen's x and y are both inverted and not swapped:

    touchscreen-inverted-x;
    touchscreen-inverted-y;

Seen on an SM-T820 (2026-10-29): the driver probes,
`wacom_w9000 3-0056: Wacom W9019 Digitizer size X19661Y14746` (the stock
DT's `max_coords`), firmware 0x0124 in the input id, pressure 0-4095,
resolution 100, INPUT_PROP_DIRECT; the supply is on only while the
device is open, and the interrupt stays quiet with no pen near
(level-low, no storm).  With the pen, the same day: five tip touches
near the corners and the centre landed within 19 px of the targets (mean
10 px) with the flags above; hover, pressure (up to 1721 of 4095 for a
light tap) and the side button were all reported, and the interrupt took
1208 interrupts over the test and went quiet afterwards.

Test: `evtest` on "Wacom W9019 Digitizer"; hover the pen (BTN_TOOL_PEN
1), touch the screen (BTN_TOUCH 1, ABS_PRESSURE rising with force), press
the side button (BTN_STYLUS), and touch near the corners as for the
touchscreen: top-left in the panel frame is X 0 / Y 0, top-right X 19661
/ Y 0.  If nothing arrives, `grep w9019 /proc/interrupts` while the pen
is near and evtest runs: a count stuck at 0 means the interrupt does not
fire (try `IRQ_TYPE_EDGE_FALLING`, the stock trigger).

An earlier version of this branch had a standalone `wacom_w9019` driver
(distance and tilt too); it duplicated `wacom_w9000` and is gone.

Not done: firmware update (the IC keeps its firmware in flash), the pen
detect line (GPIO 117; stock only logs it), the stock "survey" low-power
mode.

## Keys

| Key | Line | Driver, code |
|---|---|---|
| Power | PM8994 PON KPDPWR | `pm8941-pwrkey` (pm8994.dtsi, main DTS), KEY_POWER |
| Volume Down | PM8994 PON RESIN | `pm8941-pwrkey` (`&pm8994_resin`), KEY_VOLUMEDOWN |
| Volume Up | PM8994 GPIO 3, active low, 30 uA pull-up | `gpio-keys`, KEY_VOLUMEUP |
| Home | PM8994 GPIO 2, active low, external pull-up | `gpio-keys`, KEY_HOMEPAGE, wakeup source |

From the stock DT: `gpio_keys` in `msm8996-sec-common.dtsi` (15 ms
debounce), `qcom,pon_2` and the PMIC GPIO nodes `gpio@c100`/`gpio@c200`
in `msm8996-sec-gts3lwifi-r05.dtsi` (digital inputs at 1.8 V).
`dts/test/test-keys.dts` describes the three keys on top of the microSD
test.

Home keeps the stock code, KEY_HOMEPAGE, as the other Samsung DTs upstream
do. Plasma desktop does nothing with KEY_HOMEPAGE, so the device package's
`61-gts3-keys.hwdb` turns Home into Meta, and a lone Meta tap opens the Plasma
launcher. `gpio-keys` uses each button's index as its scan code, so 1 is Home.

Tested on an SM-T820 (2026-10-29), kernel built with `test-keys.dts`:
Volume Up and Volume Down change the volume in Plasma (the owner pressed
them); Home presses arrive as KEY_HOMEPAGE press/release pairs on the
`gpio-keys` device (an event capture), none of the three reads as pressed
at rest (EVIOCGKEY), and with the hwdb entry a Home tap opens the launcher.

Not done:

- the hall sensor of the cover (stock `hall_flip`, PM8994 GPIO 7), which
  would be SW_LID: left out until suspend works, since logind suspends on
  SW_LID;

## Back and Recent keys (TC305K)

Works on an SM-T820, tested 2026-11-02 on Linux 7.3.1. A T825 has the same stock node,
but hasn't run this.

| Check | Result |
|---|---|
| Start-up | firmware 0x18, mode 0x2 (run). First ACK after about 135 ms, ready after about 670 ms |
| Keys, by hand | Back and Recent work. Recent is the left key. The event bytes match the table below |
| Ghost presses | none: 24 min on USB, most of it with Venus decoding, and 5 min on battery |
| Suspend | 3 timed, 1 by the power button: starts again each time, with keys and lights back |
| Waking | a tap doesn't wake it from suspend, Home does. With only the screen off, a tap turns it on, like any input |

A touch can send the same byte twice, 1 ms apart. The input core drops the repeat.

L19 and the LED supply have no other consumers, so they should be off in suspend.
Sleep current with this driver loaded hasn't been measured.

A Coreriver TC305K at 0x20 on a GPIO I2C bus (TLMM 21 SDA, 22 SCL), interrupt TLMM 64,
level low, L19 at 2.2 V for the controller, TLMM 118 for the LED supply (stock
`tc305k@20` in `msm8996-sec-gts3lwifi-r05.dtsi`, `tc3xxk.c`).

`tm2-touchkey` doesn't work on it as it stands. The registers and LED commands match
the TC360, but the event byte doesn't:

| | TC360 in `tm2-touchkey` | TC305K in stock `tc3xxk.c` |
|---|---|---|
| Recent pressed, released | 0x01, 0x09 | 0x01, 0x10 |
| Back pressed, released | 0x02, 0x0a | 0x02, 0x20 |

The driver would call 0x10 and 0x20 invalid, so a key would never come back up. Patch
0160 adds a `coreriver,tc305k-touchkey` variant:

- reads the bits above. A short tap can set a key's press and release bits together;
- turns grip sensing off after each start, as stock does on this tablet
  (`CONFIG_TOUCHKEY_GRIP_SENSOR_OFF`), so grip changes cause no interrupts;
- cuts the power in suspend, where stock keeps it on in a stop mode. Coming back takes
  half a second by stock's delays, so resume does it from a work item;

Firmware is left as it is on the chip: nothing here flashes it. Stock also has modes
for glove, charger noise and a closed flip cover, not used here.

Keys: `KEY_APPSELECT` and `KEY_BACK`, as other Samsung boards upstream. Back reaches
apps as `XF86Back`, which browsers and Dolphin act on. Plasma ignores `KEY_APPSELECT`,
so the device package maps two keys:

- **Home becomes Meta**, which opens the launcher.
- **Recent becomes `KEY_SCALE`, then Present Windows.** KWin binds Present Windows to
  `Launch (C)`, which no key on this keymap produces. `KEY_SCALE` gives `Launch (A)`,
  so the package adds that to the shortcut in `/etc/skel`.

`/etc/skel` only reaches users created after the package is installed. On an existing
install, run this as your own user:

    dbus-send --session --print-reply --dest=org.kde.kglobalaccel /kglobalaccel \
      org.kde.KGlobalAccel.setForeignShortcut \
      array:string:kwin,ExposeAll,KWin,"Toggle Present Windows (All desktops)" \
      array:int32:16777388,16777390,83886137,285212729

A changed hwdb doesn't reach a running KWin, which keeps a key list from when it opened
the device. Reload the driver (`rmmod tm2-touchkey; modprobe tm2-touchkey`) or reboot.

The lights use the kernel's `input-events` trigger: on at any input, off 5 s after the
last, set by a udev rule in the device package. Input includes the touchscreen.

The LED keeps the driver's name, `tm2-touchkey`, on purpose. With `kbd_backlight` in
the name, `systemd-backlight` restores a saved 0 at boot, and writing 0 removes the
trigger, so the lights stay off.

## Keyboard cover (Book Cover Keyboard, STMPE1801)

The Book Cover Keyboard (EJ-FT820) connects through the pogo pins. Stock
DT (`msm8996-pogo-keyboard-gts3llte-common.dtsi`, which the gts3lwifi
DTs include): an ST STMPE1801 key-matrix controller at 0x40 on a GPIO
I2C bus (TLMM 2 SDA, 3 SCL), interrupt TLMM 54, supply PM8994 L6 at
1.8 V, an 8 x 10 matrix, 10 ms keypad debounce, scan count 3. Stock
drives it with Samsung's own `stmpe1801_pogo_keyboard.c`.

Mainline's STMPE MFD core already knows the STMPE1801 and creates its
keypad cell, but the keypad driver only handles the STMPE1601 and
STMPE24xx, and the binding has no `st,stmpe1801`. Patches 0069-0075 add
the STMPE1801 keypad:

| Patch | Change |
|---|---|
| 0069 | binding: `st,stmpe1801`, and row, column and debounce limits per variant |
| 0070 | MFD core: the STMPE1801 interrupt status is read-to-clear; no write-back |
| 0071 | MFD core: one block-read result for I2C and SPI (0 = full buffer), KUnit tests |
| 0072 | keypad: STMPE1801 registers and FIFO event layout, keymap sized from the DT, bounded FIFO drain (ten entries), scan started only after the IRQ and input device are registered |
| 0073 | keypad: KUnit tests for the event parser and the start/stop ordering |
| 0074 | keypad: an OF match table, so a modular build loads by itself |
| 0075 | MFD core: VCC/VIO disabled again when probe fails |

The register facts come from ST's public STMPE1801 datasheet (chip ID
0xc110, read-to-clear interrupt status at 0x08, keypad configuration at
0x30-0x36, a ten-entry FIFO read as five bytes from 0x3a); Samsung's GPL
driver was only used to cross-check the addresses and the I2C transfers.

The main DT describes the cover (since 2026-10-31; before that
`dts/test/test-keyboard.dts`, on top of the keys test), with the stock
key map (stock key 706, Samsung's soft keyboard toggle,
is KEY_ONSCREEN_KEYBOARD). Two things beyond the stock values were
needed: the controller does not answer 10 ms after L6 comes up (the
chip ID read failed with -ENXIO), so L6 has a 100 ms
`regulator-enable-ramp-delay` (stock retries each transfer three times,
10 ms apart); and the node uses plain `interrupts`, which is what
`stmpe_probe()` reads. Kernel config: `CONFIG_MFD_STMPE`,
`CONFIG_STMPE_I2C` and `CONFIG_KEYBOARD_STMPE` (all three =m in the
package config).

Seen on an SM-T820 (2026-10-29): with the cover attached at boot the
STMPE1801 probes, the keypad registers as an input device, and typing
works. Patch 0075 came from a probe that ran too early (before the ramp
delay): the ID read failed and the supply stayed enabled, with a WARN in
`regulator_put()`.

Again with the main DT and patches 0001-0104 (2026-10-31): the
STMPE1801 probes at boot (chip ID 0xc110), "STMPE keypad" registers, and
typing works (keypad interrupts and key events on the event device).

### Hotplug (patches 0114-0120)

The STMPE1801 only answers while the cover is attached, so until
2026-11-01 it probed only when the cover was there at boot. Stock detects
the cover through the SM5705 MUIC: an ID ADC reading of 0x11 (49.9 kOhm,
`ADC_KEYBOARDDOCK` in the Samsung kernel's `include/linux/muic/muic.h`;
`drivers/muic/universal/muic_vps.c`) means attached, and
`drivers/muic/keyboard_notifier.c` then powers L6 and starts the
keyboard. Patch 0104 reports that reading as EXTCON_DOCK
(`extcon-sm5705` reads the ADC register, 0x09, with the other detection
registers). The main DT now puts the keyboard behind an I2C bus that
follows the dock state:

| Patch | Change |
|---|---|
| 0114-0115 | `i2c-hotplug-gpio`: an I2C bus whose devices are added while a GPIO reports the accessory (Michał Mirosław, Svyatoslav Ryhel; posted 2025-10-13, not merged) |
| 0116 | its parent adapter lookup: NULL checks, and `of_get_i2c_adapter_by_node()` to pair with `i2c_put_adapter()` |
| 0117-0118 | the same bus driven by an extcon device's EXTCON_DOCK (`i2c-hotplug-extcon`), and its binding |
| 0119 | `extcon-sm5705`: the cover also while VBUS is present |
| 0120 | MFD core: the IRQ domain removed after the child devices |

When the MUIC reports the cover, the bus registers and the STMPE1801
probes, which enables L6; when the cover goes, the client is removed and
L6 is off again. The keypad's stop fails at removal (-ENXIO: the chip is
already gone); that is expected.

With a USB cable in, the MUIC did not see a cover that came later: the
ADC kept reading open (0x1f), and the charger type was dropped, so
EXTCON_USB went off too. Samsung's driver, with
`CONFIG_MUIC_SUPPORT_KEYBOARDDOCK` (set in the gts3lwifi defconfig), sets
bit 7 of RSVD_ID1 (0x15) at init ("attach RID when VBUS exists") and,
when the cover and VBUS are there without a charger type, reruns charger
detection with BCD_RESCAN (bit 4 of RSVD_ID2, 0x16). The result comes
with the RID charger interrupt (INT2 bit 6), which was masked. Patch 0119
does the same. The missing charger type is also why a USB-A wall charger
next to the cover charged at the Type-C default (500 mA) instead of as a
DCP; with the rerun it should be detected, but that is not tested yet.

Patch 0120: mainline 57bf2a312ab2 ("mfd: stmpe: Remove IRQ domain upon
removal", v6.18) removes the IRQ domain before the keypad, whose
`free_irq()` then walks the freed domain. The second removal, with a key
held, oopsed in `__irq_domain_deactivate_irq()` and killed the hotplug
worker. The patch removes the domain in a devm action, after the
children and the parent interrupt are gone; it is meant for upstream.

The keyboard node is a `wakeup-source`: the I2C core arms its interrupt
for wakeup when the client probes, so a key press wakes the tablet from
s2idle while the cover is attached. Stock arms the same interrupt in
suspend while the cover is connected.

Seen on an SM-T820 (2026-11-01): with a USB host connected, five
detach/attach cycles, the keypad back 110-150 ms after each attach and L6
off after each detach, including detaches with a key held (no oops, no
stuck key reported); USB unplugged and plugged back with the cover on
(the rerun brought EXTCON_USB and SDP back 130 ms after VBUS); the cover
taken off without USB, USB plugged back without it (plain SDP, ADC open)
and the cover attached again; a 20 s s2idle suspend with the cover on;
and a key press waking the tablet. A plain USB cable never read 0x11.

Not done: the wall charger (DCP) case next to the cover; the suspend
drain with the cover attached (L6 and the STMPE1801 stay on, as in
stock).

### Power cycles the MUIC misses (patch 0121)

Patch 0121 is by Nazim Aghabayov, from PR #2 (tested on an SM-T825). A
detach and reattach quick enough for the MUIC to debounce away gives no
dock event, so the STMPE1801 stays bound while it has lost its
configuration. The keypad driver reads its low column register every 2 s
while it is bound. A NAK means the cover is away right now, logged once.
The reset default means the chip power cycled: a new MFD helper,
`stmpe_restore_irq_setup()`, rewrites ICR, autosleep and the cached IER
bits, and the keypad block is set up and started again. With the hotplug
above, a detach the MUIC sees removes the device, which stops the poll;
the next attach probes it fresh.
