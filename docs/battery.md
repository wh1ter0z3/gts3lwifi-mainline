# Battery, charger, USB charger detection and USB-C: SM5705, S2MM005

## Status (2026-10-28): fuel gauge, charger, MUIC and USB-C/PD work

With `dts/test/test-charger.dts` (merged into the main DTS on 2026-10-28) (which includes `test-battery.dts`) and
kernel patches 0012-0017 there are two power supplies and an extcon device:

- `sm5705-battery`, the fuel gauge (`CONFIG_FUEL_GAUGE_SM5705=m`): state of
  charge, voltage, open-circuit voltage, current, temperature (from the
  battery's thermistor since patch 0173, see
  [below](#battery-temperature-from-the-thermistors-0172-0174)), and the
  battery's design data;
- `sm5705-charger`, the charger (`CONFIG_CHARGER_SM5705=m`, on an
  `i2c-gpio` bus, so also `CONFIG_I2C_GPIO=m`): USB power present, USB
  charger type, charging state, health, and the charge limits;
- the MUIC (`CONFIG_EXTCON_SM5705=m`, extcon `1-0025`): the BC1.2 charger
  type. The charger sets its input current limit from it.

```
POWER_SUPPLY_NAME=sm5705-charger
POWER_SUPPLY_TYPE=USB
POWER_SUPPLY_STATUS=Full
POWER_SUPPLY_HEALTH=Good
POWER_SUPPLY_ONLINE=1
POWER_SUPPLY_USB_TYPE=SDP
POWER_SUPPLY_CONSTANT_CHARGE_CURRENT=2000000
POWER_SUPPLY_CONSTANT_CHARGE_VOLTAGE=4300000
POWER_SUPPLY_INPUT_CURRENT_LIMIT=500000
POWER_SUPPLY_CHARGE_TERM_CURRENT=300000

POWER_SUPPLY_NAME=sm5705-battery
POWER_SUPPLY_TYPE=Battery
POWER_SUPPLY_STATUS=Full
POWER_SUPPLY_TECHNOLOGY=Li-ion
POWER_SUPPLY_VOLTAGE_MAX_DESIGN=4300000
POWER_SUPPLY_VOLTAGE_MIN_DESIGN=3400000
POWER_SUPPLY_VOLTAGE_NOW=4296875
POWER_SUPPLY_VOLTAGE_OCV=4377441
POWER_SUPPLY_CURRENT_NOW=-625000
POWER_SUPPLY_CHARGE_FULL_DESIGN=6000000
POWER_SUPPLY_ENERGY_FULL_DESIGN=23100000
POWER_SUPPLY_CAPACITY=100
POWER_SUPPLY_TEMP=375
POWER_SUPPLY_CHARGE_TERM_CURRENT=300000

/sys/class/extcon/extcon0 (1-0025): USB=1 SDP=1 DCP=0 CDP=0
```

UPower shows the battery (percentage, state, icon) and the charger as line
power (`on-battery: no` while USB is plugged in).

**The charge voltage is now Samsung's 4.30 V.** Without a charger driver the
SM5705 kept the 4.38 V it boots with, and the battery sat at 4.38 V
whenever the tablet was plugged in. The charger driver applies the limits
from the battery node at probe. After that the battery first runs down to
4.30 V on USB power (the current above, -625 mA, is that), then the charger
holds it there.

While charging is on, the charger holds the battery node at BATREG with the
battery switch fully on (STATUS2 0x38: CHGON, Q4FULLON, TOPOFF), so a
battery resting above 4.30 V carries the system load until it is down to
4.30 V. Switching the charger off for a moment (CNTL 0x05 -> 0x04) shows it:
the battery current went from -335 mA to -11 mA, USB carried the whole load,
and the battery rested at 4.333 V. The gauge's OCV estimate (4.293 V) lags
behind. The idle load is about 1.45 W, well within a 500 mA USB port.

**The input current limit follows the charger type** (charger driver,
from the MUIC):

| Type | Input limit | Why |
|---|---|---|
| SDP (PC port, or data contact timeout) | 500 mA | USB 2.0; stock uses 460 mA |
| CDP | 1500 mA | BC1.2; stock uses 1000 mA |
| DCP (wall charger, also Samsung AFC, QC 2.0, U200) | 1800 mA | stock's setting for Samsung wall chargers |
| AFC / QC 2.0 at 9 V (opt-in, below) | 1650 mA | stock's 9 V setting |
| USB-C source, Type-C current or PD contract (S2MM005, below) | up to 3000 mA at 5 V, 1650 mA above 5.5 V | used when higher than the BC1.2 limit; a PD contract above 5.5 V always (patch 0084) |
| nothing detected | unchanged | |

**Charge current:** 2.7 A with a source of about 15 W (9 V AFC, or 3 A at
5 V over USB-C), like Samsung's firmware for its 15 W chargers; 2.0 A (the
bootloader's setting) otherwise. The temperature guard's limits still
apply. Patch 0028.

The bootloader had left 1500 mA for the PC port. `input_current_limit` is
writable, until the next cable event. AFC (9 V) is not negotiated, so
Samsung fast chargers deliver 5 V.

Cable changes, logged every 2 s (PC port, then a wall charger, then the PC
again):

```
12:50:34 USB=0 SDP=0 DCP=0 type=Unknown lim=500000  online=0 chg=Discharging muic-irq:1
12:50:56 USB=0 SDP=0 DCP=1 type=DCP     lim=1800000 online=1 chg=Full        muic-irq:3
12:51:37 USB=0 SDP=0 DCP=0 type=Unknown lim=1800000 online=0 chg=Discharging muic-irq:4
12:51:58 USB=1 SDP=1 DCP=0 type=SDP     lim=500000  online=1 chg=Charging    muic-irq:6
```

Every change raised the MUIC interrupt (attach/detach) and the charger
interrupt, and the input limit followed the type. With nothing plugged in
the limit stays at the last value, which the next cable replaces.

Pulling the USB cable and plugging it back in (logged every 2 s):

```
12:27:06 online=1 chg=Full        bat=Full        i=-498535 irq=5
12:30:40 online=0 chg=Discharging bat=Discharging i=-229492 irq=5
12:30:42 online=1 chg=Charging    bat=Charging    i=-199707 irq=8
12:30:44 online=1 chg=Full        bat=Full        i=-420410 irq=9
```

Each change raised the charger interrupt and a `change` uevent on both
supplies; the battery follows through `external_power_changed`. UPower
recorded the battery as discharging and set line power back online at
replug. USB networking came back by itself.

Not done yet:

- the flash LED, which shares the charger's I2C device;
- the fuel gauge's alert interrupt and battery model init.

## Hardware

| | |
|---|---|
| Fuel gauge | SM5705 fuel gauge, I2C 0x71, device ID 0x000b |
| Fuel gauge bus | TLMM 6 (SDA) / 7 (SCL). Stock drives them as an `i2c-gpio` bus, but they are the BLSP2 QUP2 I2C pins, so the test DT uses the hardware controller `&blsp2_i2c2` (0x75b6000) at 100 kHz |
| Fuel gauge alert | PM8994 GPIO 1 (stock `fuelgauge,fuel_int`), not used: the driver polls every 30 s |
| Charger | SM5705 charger and flash LED, I2C 0x49, device ID 0xf9, interrupt TLMM 38 (active low, pull-up) |
| MUIC | SM5705 MUIC (cable detection, AFC and QC 2.0), I2C 0x25, device ID 0x09 (bit 3: QC 2.0 support), interrupt PM8994 GPIO 4: push-pull 1.8 V, active low, so the pin needs `power-source = <PM8994_GPIO_S4>` (with the default VPH reference it reads low) |
| Charger bus | TLMM 33 (SDA) / 32 (SCL). These are HDMI DDC pins, not a BLSP pair, so like stock it is an `i2c-gpio` bus |
| Charge enable | PM8994 GPIO 13, active low. The bootloader leaves it low (charging on); Linux does not touch it |
| Battery | EB-BT825ABE, 6000 mAh, 3.85 V, 23.1 Wh |
| Stock limits | charges to 4.30 V (`battery,chg_float_voltage`), top-off 300 mA, full condition 4.20 V and 93 %, recharges below 4.23 V, powers off around 3.4 V |

The battery is a `simple-battery` node (`monitored-battery`). The power
supply core exposes its design capacity, energy, voltages and chemistry on
the battery. The charger driver applies `constant-charge-voltage-max-microvolt`,
`constant-charge-current-max-microamp` (not set, the charger keeps 2 A) and
`charge-term-current-microamp`, and refuses writes above the battery's
maximums. The fuel gauge names the charger in `power-supplies` and takes
its status from it.

## Battery temperature guard

Kernel patch 0021. The SM5705 charger only protects its own die
(THEMREG/THEMSHDN); the battery's temperature comes from the fuel gauge,
which reads the battery's thermistor since patch 0173 (next section).
The charger driver reads the temperature of the battery it supplies every
30 seconds and applies Samsung's limits for this battery
(`battery,temp_*_normal` and `battery,swelling_*` in the stock DT):

| Battery temperature | Charging | Back to the next zone |
|---|---|---|
| at or above 50 C | off (charger mode CHG_OFF, the tablet stays on USB power) | below 45 C |
| 41-50 C | 4.10 V, at most 1.35 A | below 39 C |
| 15-41 C | normal: 4.30 V, 2 A | |
| 0-15 C | at most 0.9 A | above 20 C |
| below 0 C | off | above 5 C |

The off limits come from `alert-celsius = <0 50>` in the battery node.
While charging is off the charger and the battery report "Not charging",
and the charger's health reads Overheat / Cold / Warm / Cool. Limits written
to sysfs stay the upper bound.

Tested with a module whose upper limit was moved to 30 C: with the battery
at 37 C it switched charging off within the first check (CNTL 0x05 ->
0x04, health Overheat, status Not charging); the real module switched it
back on at load.

## Battery temperature from the thermistors (0172-0174)

**Why.** Until patch 0173 the gauge reported its temperature register (0x09).
That register reads the SM5705 itself, which also holds the charger, not the
battery. On a 9 V charger it moved 6-8 C within seconds each time charging
started or stopped, and went from 24.9 to 41.3 C within 12 seconds of the
charger starting, with the other thermal zones at 24-28 C. The guard above
took that for a warm battery: on a 9 V PD charger the register read 49.5 C
at +2.47 A, the charger went to health Warm and cut to 4.10 V and 1.35 A,
and minutes later it read 42.2 C at +1.38 A. That is the current, not heat.

**What stock does.** Samsung's `sec_battery.c` reads the battery from a
thermistor on PM8994 VADC channel LR_MUX5_PU1_AMUX_THM2 (0x74) and turns the
raw code into a temperature with `battery,temp_table_adc` and `_data`
(msm8996-sec-gts3llte-common-battery.dtsi); it uses the gauge's register only
inside the gauge's own compensation. A second thermistor next to the charger,
on LR_MUX8_PU1_AMUX_THM4 (0x77, `battery,chg_temp_table_adc` and `_data`),
makes stock lower the input voltage above 50 C (`battery,chg_high_temp`),
until it is back under 47 C (`_recovery`): 9 V AFC to 5 V, or the next lower
PD profile, with at most 10 W on the lowest.

**The patches.**

- 0172: bindings, `io-channels` and a `temperature-lookup-table` for both
  SM5705 nodes;
- 0173: the fuel gauge reports the thermistor when the board has one, and
  falls back to the register when it doesn't;
- 0174: the charger can't lower its input voltage, so above 50 C on the
  charger-area thermistor it limits the charge current to 1.4 A, until it
  is back under 47 C. 1.4 A is Samsung's `battery,chg_charging_limit_current`, which
  stock reads but doesn't use for wired charging, so the current cap is our
  choice, and the kernel log says so (`charger area at ... C: at most 1.4 A`).

The board DT adds both VADC channels (ratiometric, 200 us settle) and stock's
two tables. The SM-T825 DT includes the SM-T820 one, so it gets them too.

**Stock's numbers on this tablet** (TWRP, stock kernel 3.18, on USB, cool):

| | Battery (0x74) | Charger area (0x77) | Gauge register |
|---|---|---|---|
| 7 readings, idle and with the GPU loaded, -389 to +466 mA | 19.9-23.5 C at raw 34285-35134 | 24.8-27.9 C at raw 33448-34112 | 26.7-29.3 C |

The two tables turn stock's raw codes into stock's temperatures, to 0.1 C.

**Tested on an SM-T820** (7.3.1 with 0172-0174 booted from RAM, 2026-11-14):

| | Battery (0x74) | Charger area (0x77) | Health |
|---|---|---|---|
| At rest on the PC's USB-C, 100 % | 24.7 C at raw 33951; the table gives the same to 0.1 C | 34.6 C | Good |
| Input limit 3 A → 100 mA → back, a 1.3-1.45 A swing | moved 0.2 C per amp (the register moved 6-8) | | Good |
| 9 V / 3 A PD charger from 80 %, 13 minutes at about +1.5 A | 24.4-24.6 C | 40.9 C at most | Good |

The same PD charger, before the patches, read 42.2 C at +1.38 A and had sent
the charger to Warm. Stock's kernel in TWRP read 24.8 C (`batt_temp`) two
minutes before this kernel read 25.1 C.

On that charger the battery took about 1.5 A, not the 2.7 A charge current:
with a 9 V contract the input limit is 1.65 A (Samsung's 9 V setting), and
1.5 A into the battery is about what 1.65 A gives at 5 V, not 9 V. These
patches don't touch either limit; why, isn't known yet.

Not tested: a warm battery, the 1.4 A limit firing (charger area over 50 C),
the SM-T825, a built package.

## Fast charging: AFC and QC 2.0 at 9 V (opt-in)

Kernel patches 0019-0020. The MUIC driver negotiates 9 V with Samsung AFC
chargers, and falls back to Quick Charge 2.0 when AFC fails, like the
vendor driver: request 9 V (`AFC_TXD` 0x46 = 9 V / 1.65 A, `ENAFC`), measure
VBUS once the charger accepts (`VBUS_READ`), and take a `VBUS_STATUS`
within one step of the request as success; five failures leave it at 5 V.
A 9 V charger shows up as `FAST-CHARGER` in the extcon state and the charger
driver sets a 1.65 A input limit (Samsung's 9 V setting) and charges at
2.7 A, like stock (patch 0028).

**Off by default.** Turn it on with:

    echo "options extcon_sm5705 afc=1" > /etc/modprobe.d/afc.conf

Samsung's firmware only starts AFC after the S2MM005 Type-C controller has
seen the 56 kOhm Rp of a legacy USB-A to C cable ("to prevent damage by RP0
cable"). Here the charger driver keeps VBUS at 5 V through the MUIC
(`sm5705_muic_hold_5v()`, patches 0035-0036) while the S2MM005 reports a
USB-C source: a PD contract or more than default USB current. With
Samsung's 25 W PD charger the 5 V hold comes before the MUIC has finished
its detection, so AFC is never tried. A cable that shows no Type-C attach
at all still gets AFC (unlike stock, which needs the 56 kOhm).

The Xiaomi's USB-A to C cable has a non-standard pull-up: at 5 V the
S2MM005 reads it as a 3 A USB-C source, at 9 V as nothing. Depending on
which comes first after plugging in, the charger ends at 9 V / 1.65 A or
5 V / 3 A; both are 15 W and charge at 2.7 A.

Tested with a Xiaomi 18 W charger (QC 3.0, also answers AFC) and a USB-A to
C cable:

```
17:35:27 DCP=1 FAST-CHARGER=0 lim=1800000 | DEVICE_TYPE3=0x40 CHG_TYPE=0x10 VBUS_STATUS=0x00
17:35:30 DCP=1 FAST-CHARGER=1 lim=1650000 | DEVICE_TYPE3=0xc0 CHG_TYPE=0x11 VBUS_STATUS=0x04
...      9 V held for 2 minutes, charger online, no over-voltage
17:37:38 back on the PC: SDP, 500 mA
sm5705-muic 1-0025: AFC: charger at 9 V
```

The charger's OVPSEL bit (CHGCNTL7 bit 2, set by the bootloader and by
stock) selects the high VBUS over-voltage threshold that 9 V needs. The
QC 2.0 path is written but has not run yet (the Xiaomi answered AFC).

USB-C PD chargers: see the next section, and [usb](usb.md) for 9 V.

## USB-C and Power Delivery: S2MM005

Kernel patches 0029-0032, main DTS. The S2MM005 ("CCIC" in Samsung's
kernels) is a Type-C port controller whose firmware also runs the PD
policy engine: as a sink it takes the source's capabilities and requests
one by itself. The new driver reports what it negotiated:

- `/sys/class/typec/port0` (sink, device) with a partner while a USB-C
  source is attached, `power_operation_mode` `usb_power_delivery`,
  `3.0A`, `1.5A` or `default`;
- a power supply `s2mm005-source-2-0033` with `online`, `usb_type`
  (C or PD), `voltage_now` and `current_max`, which supplies the charger
  (`power-supplies` in the charger node).

The charger uses the higher of the MUIC's BC1.2 limit and what the USB-C
source allows (at most 3 A at 5 V, Samsung's 1.65 A above 5.5 V).

Tested with Samsung's 25 W PD charger (USB-C to C cable):

```
s2mm005 2-0033: Type-C source: 5000 mV, 3000 mA
sm5705-charger 3-0049: 15 W source, charging at up to 2700 mA
s2mm005 2-0033: PD contract: 5000 mV, 3000 mA
```

input limit 3 A, charge current 2.7 A, PD revision 3.0. It offers 5 V
3 A, 9 V 2.77 A, 12 V 2.1 A and PPS 3.3-5.9 V 3 A / 3.3-11 V; the firmware
requests the first. Unplugged and on the PC port (this cable shows no
Type-C pull-up, so there is no attach): 500 mA input, 2.0 A charge current.

**9 V over PD** works with patches 0082-0084 and the board DT's
`sink-pdos` (2026-10-31, [usb](usb.md)): the driver asks the firmware
for the best fixed supply a second after the first contract, and the
charger takes 1.65 A at 9 V. Tested with an Apple USB-C charger (9 V / 3 A
contract) and a Sabrent dock on a PD 3.0 supply (9 V / 2.44 A).

The first attempt, by hand with Samsung's 25 W PD charger, failed: asking
for the 9 V capability (the vendor's `select_pdo`: command `03 03 02`, then
`03 02 11` to request again) got an Accept, the charger started ramping
(the MUIC measured about 7 V), then the chip saw CC1 open and detached
about 0.4 s after the Accept, and everything restarted at 5 V. VBUS never
dropped (no hard reset), the tablet's load did not matter (same at 500 mA
input), and the MUIC stayed quiet. That charger has not been tried with
the patches yet. With 3 A at 5 V it already gives the same 15 W as 9 V
AFC.

| | |
|---|---|
| Bus | BLSP1 QUP5 I2C (TLMM 83 SDA, 84 SCL), 100 kHz, address 0x33; msm8996.dtsi has no node for this QUP, the board DTS adds it |
| Interrupt | TLMM 73, open drain, low while an event is pending; the board pulls it up, so the pin needs `bias-disable` (with the default pull-down it always reads low) |
| Firmware | main version 0xd5, boot 6, hardware boot 4 (no ftrim fix-up needed, that is for boot 2 and older); never flashed by this driver |
| Registers | 16-bit addresses, big endian; 0x10 command, 0x20 state, 0x30 message flags, 0x60 low power state, 0x0220 own sink capabilities (5 V 0.5 A, variable 5-12 V 0.5 A), 0x0240 last request, 0x0260 source capabilities |
| State word 0x20 | byte 0 PD state (21 = sink ready, 17 = waiting for capabilities, 20 = transition), bit 24 attached, 25 source, 26 DFP, 27-28 Rp level (1 default, 2 1.5 A, 3 3 A), 31 firmware reset |
| Commands | `01` clear interrupt; `aa` maps the firmware version to 0x08/0x09; `0f 06` automatic low power mode; `03 03 n` select capability n; `03 02 s` go to PD state s |

## Flash LED (torch)

Kernel patches 0033-0034, main DTS. The rear camera flash (under the
camera) is on the SM5705's first flash LED output. The charger driver
registers it as `/sys/class/leds/white:torch`: brightness n is n x 10 mA,
0-18 (180 mA, stock's camera torch current; the output goes to 320 mA).

```
echo 9 | sudo tee /sys/class/leds/white:torch/brightness   # 90 mA
echo 0 | sudo tee /sys/class/leds/white:torch/brightness
```

It runs in "movie mode" over I2C (FLED1CNTL1 0x17 bits 1:0 = 1, current in
FLED1CNTL4 0x1a: 10 mA + 10 mA steps). Stock uses the external enable
pins instead (TLMM 11 torch, 12 flash), which stay low. The LED needs the
charger's operation mode, as in Samsung's table: with USB power the
charger stays in charging mode (CNTL 0x05), on battery it switches to the
flash boost (0x06, 4.5 V from FLEDCNTL6) while the torch is on, and back
when USB power appears or the torch goes off.

Like Samsung's firmware, switching the torch on brings a 9 V AFC charger
back to 5 V first (the MUIC's D+ reset, VBUS confirmed at 5 V in about
0.15 s), and switching it off lets the charger return to 9 V (patch 0036).

Tested: 20, 90 and 180 mA on USB power and on battery (battery current up
by about 235 mA at 180 mA), and USB plugged in while lit (mode 0x06 ->
0x05, LED stays on). The 1.2 A camera flash strobe is not used; mainline
has no camera driver for this tablet.

## Switched on by the charger: the charging screen (like stock)

Stock, read from the firmware (S-Boot in the aboot partition, Samsung's
`/system/bin/lpm`):

- S-Boot's `check_low_battery`, at every start: at 3449 mV or less with no
  cable (and no reboot or factory jig) it powers off; with a cable it stays
  in the bootloader and charges, a second at a time, until the battery is
  above 3499 mV, and powers off if the cable is pulled meanwhile. This runs
  before lk2nd, so it already applies to us.
- When plugging in switched the tablet on, S-Boot starts the boot image with
  `androidboot.mode=charger` (and `charging_mode=0x3030`); Android then runs
  only `lpm`: a battery on the screen, the screen dark after a while, a
  short press of the power key lights it, holding the key boots Android
  (`is_bootable()` checks the battery health, not its level), unplugging
  powers off.

We do the same. lk2nd passes the original cmdline to Android boot images
only; its patch 0007 ([lk2nd/v2](../lk2nd/v2)) passes on
`androidboot.mode=charger` alone to the extlinux labels. Then
`gts3-charger-generator` (device package) boots `gts3-charger.target`, the
basic system without the desktop, network or UPower, and `gts3-charger`
shows "Charging NN% - hold the power key to start" on the boot splash. The
screen goes dark after 20 s and a press lights it; holding the power key
1.5 s continues to the full system (`graphical.target`, about 4 s);
unplugging switches the tablet off after 3 s.

Why: on a 500 mA USB port the battery drains for about 20 s while the
desktop starts (-1.2 A at worst, measured), and UPower takes a negative
current as discharging, so with an empty battery it powered the tablet off
on every boot, and the charger switched it on again (four boots in three
minutes). Kernel patch 0239 helps too: the gauge tells user space when the
current crosses zero (every 2 s on a charger), so UPower sees charging again
within a second or two instead of at its own 30 s poll.

Tested on the SM-T820 (2026-11-28, lk2nd 0007 from RAM): S-Boot passed the
mode on each cable start, the charging screen charged 79 -> 83% in 32 min on
the laptop port, holding the key started the desktop, and unplugging
switched the tablet off. The board has no panel, and there plymouth's
display stalls ("pp done time out"): the screen and splash are written in
the background so the power key is never missed (it was, in the first
test). Not tested with a screen yet.

## Fuel gauge registers

All registers are 16 bit, read with an SMBus word read (low byte first).
Layout and formulas from Samsung's `sm5705_fuelgauge.c`:

| Reg | Name | Decoding | Example |
|---|---|---|---|
| 0x00 | DEVICE_ID | | 0x000b |
| 0x05 | SOC | bits 15:8 percent, 7:0 1/256 % | 0x6424 = 100.1 % |
| 0x06 | OCV | bits 13:11 volts, 10:0 1/2048 V | 0x2305 = 4.377 V |
| 0x07 | VOLTAGE | as OCV | 0x2307 = 4.378 V |
| 0x08 | CURRENT | bit 15 sign (set = discharging), 12:11 amps, 10:0 1/2048 A | 0x800a = -4.9 mA |
| 0x09 | TEMPERATURE | bit 15 sign, 14:8 degrees, 7:4 1/16 degree; the SM5705's own temperature, not the battery's (used only without a thermistor, 0173) | 0x2590 = 37.5 °C |
| 0x10 | FG_OP_STATUS | bit 4 set: battery model loaded | 0x0010 |

The current sign was checked on the tablet. Right after a reboot the charger
topped the full battery up at +78 mA, CPU load on all cores lowered that to
+58 mA, and once charging ended the current settled at -3 to -5 mA.
Positive means charging, as the power supply ABI expects.

Stock shows the raw SOC: `fuelgauge,capacity_max` is 1000 (100.0 %) and
`capacity_min` 0. The gauge reports slightly over 100 % when the battery is
above 4.30 V, and the driver clamps that to 100.

## Charger registers

8-bit registers, layout from Samsung's `sm5705_charger.c`. Values as the
bootloader left them, before the driver:

| Reg | Name | Content | Boot value |
|---|---|---|---|
| 0x00-0x03 | INT1-4 | interrupt flags, cleared by reading | |
| 0x04-0x07 | INTMSK1-4 | 1 = masked | 0xff 0x9b 0xff 0xff |
| 0x08 | STATUS1 | 0 VBUSPOK, 1 VBUSUVLO, 2 VBUSOVP, 3 VBUSLIMIT, 4-7 same for wireless | 0x21 |
| 0x09 | STATUS2 | 0 AICL, 1 BATOVP, 2 NOBAT, 3 CHGON, 4 Q4FULLON, 5 TOPOFF, 6 DONE, 7 WDTMROFF | 0x39 |
| 0x0a | STATUS3 | 0 THEMREG, 1 THEMSHDN, 2 OTGFAIL, 3 DISLIMIT, 4 PRETMROFF, 5 FASTTMROFF, 6 LOWBATT | 0x00 |
| 0x0c | CNTL | bits 2:0 operation mode (5 = charging on) | 0x05 |
| 0x0d | VBUSCNTL | bits 6:0 input limit, 100 mA + 25 mA steps | 0x38 = 1500 mA |
| 0x10 | CHGCNTL2 | bits 5:0 charge current, 100 mA + 50 mA steps | 0x26 = 2000 mA |
| 0x12 | CHGCNTL4 | bits 5:0 BATREG (0: 3.8 V, 1: 4.0 V, then 4.01 V + 10 mV steps), bit 6 AUTOSTOP | 0x67 = 4.38 V |
| 0x13 | CHGCNTL5 | bits 3:0 top-off current, 100 mA + 25 mA steps; 5:4 switching frequency | 0x04 = 200 mA |
| 0x15 | CHGCNTL7 | 7:6 AICL threshold (0 = 4.5 V), 5 AICL enable, 2 OVPSEL, 1 AUTOSET | 0x2e |
| 0x16 | CHGCNTL8 | 6:5 watchdog timer, 4:3 top-off timer (3 = 45 min) | 0x18 |
| 0x37 | DEVICEID | | 0xf9 |

After the driver: BATREG 0x1f (4.30 V), top-off 0x08 (300 mA), interrupt
masks 0xf8 0x95 0xcd 0xff (VBUS good/UVLO/OVP, battery OVP, charging,
top-off, done, thermal shutdown, safety timers).

Stock's init also sets a 45 min top-off timer (CHGCNTL8 0x16 bits 4:3),
AICL on at 4.5 V, OVPSEL on and AUTOSET off. The boot values match except
AUTOSET, which is on; the driver leaves all of them alone.

`STATUS` of the charger, and so of the battery: USB power absent ->
discharging; TOPOFF or DONE -> full; CHGON -> charging; otherwise not
charging. The input current limit is not changed: on the PC's USB port the
charger's AICL holds the input current below what the port can give
(STATUS2 bit 0 set).

## MUIC registers

The layout differs from the SM5502/SM5504/SM5703 that mainline's
`extcon-sm5502` handles: a third interrupt register at 0x05 moves the masks,
the ADC and the device type registers, so the SM5705 has its own small
driver (`extcon-sm5705`). From Samsung's `muic_regmap_sm5705.c`:

| Reg | Name | Content | Boot value |
|---|---|---|---|
| 0x01 | DEVICE_ID | | 0x09 |
| 0x02 | CONTROL | 4 switch open, 3 raw data, 2 manual switch, 1 wait, 0 interrupt mask | 0x1e |
| 0x03-0x05 | INT1-3 | 1: attach/detach; 2: VBUS on, RID charger, MHL, ADC change, VBUS off; 3: AFC | |
| 0x06-0x08 | INTMASK1-3 | 1 = masked | 0xff 0xff 0xff |
| 0x09 | ADC | ID resistor, 0x1f = open | 0x1f |
| 0x0a-0x0c | DEVICE_TYPE1-3 | 1: 7 OTG, 6 dedicated charger, 5 USB charger, 2 USB; 3: 7 AFC TA, 2 DCD timeout SDP, 1 QC 2.0 | 0x04 0x00 0x00 |
| 0x13 | MANUAL_SW1 | D-/D+ switch | 0x27 (USB) |
| 0x15 | RSVD_ID1 | bit 0 VBUS valid | 0x81 |
| 0x17 | CHG_TYPE | 0x01 DCP, 0x02 CDP, 0x04 SDP, 0x08 DCD timeout, 0x10 U200, 0x11 AFC, 0x13 QC 2.0 | 0x04 |

The driver unmasks attach/detach and VBUS on/off/ADC change (masks
0xfc 0x7a 0xff), reads CHG_TYPE while VBUS is valid, and reports USB plus
SDP/CDP, or DCP. The USB switch stays in automatic mode.

## Battery model

Samsung's driver loads a battery model at boot (`battery_params`: OCV
tables, RCE, RS, current and temperature calibration) when FG_OP_STATUS bit
4 is clear. The gauge runs from the battery and keeps that model across
reboots and OS changes, so on this tablet it still reports bit 4 set and its
SOC matches the voltage. If the battery is ever disconnected, the gauge
loses the model; the driver then warns at probe
(`gauge lost its battery model`) and the percentage may be wrong until
Android has booted once, or until the init sequence is ported.

## Next

- 9 V over PD with Samsung's 25 W PD charger (see above); variable and PPS
  supplies ([usb](usb.md)).
- CPU idle: the kernel config has `CONFIG_ARM_PSCI_CPUIDLE` off, so there
  are no CPU idle states. Worth trying for idle power, carefully, since
  Samsung's TrustZone already mishandles PSCI SYSTEM_RESET.
- Fuel gauge alert interrupt (PM8994 GPIO 1) instead of only polling.
- Port the gauge model init (`battery_params`) in case the gauge resets.
