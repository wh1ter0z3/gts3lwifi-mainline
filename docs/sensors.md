# Sensors

## Status (2026-11-01): accelerometer, gyroscope, magnetometer and light sensor work; auto-rotate and auto-brightness can use them; no compass heading yet

| Sensor | Chip | Models | State |
|---|---|---|---|
| Accelerometer | ST LSM6DS3 (K6DS3TR) | both | works, orientation correct in all four positions |
| Gyroscope | same chip | both | works; rotation matrix from patch 0145 (before it every axis had the wrong sign; Z checked on the SM-T820) |
| Magnetometer | AKM AK09916C | both | raw field works (about 0.4-0.6 gauss), uncalibrated (no hard-iron correction, see [Compass](#compass)); rotation matrix from patch 0145 (X and Y checked on the SM-T820); `tools/compass.sh` is a flat-only test compass; no compass heading in iio-sensor-proxy: iio-sensor-proxy 3.9 cannot make one from a raw magnetometer, see [Compass](#compass) |
| Ambient light (RGB) | Capella CM3323 | both | works with patch 0077: `qcom-smgr-light`, `in_intensity_both` in lux (about 20 indoors, up to ~31000 under a phone torch, 0 when covered); iio-sensor-proxy reports it, KWin's auto-brightness can use it (off by default, see [Auto-brightness](#auto-brightness)) |
| Cover (hall) | PM8994 GPIO 7, low = closed | both | `dts/test/test-hall.dts` (SW_LID), untested; not in the main DT until suspend is reliable |
| Grip (SAR proximity) | Semtech SX9310 | SM-T825 only | `dts/test/test-sar.dts`, untested; mainline driver `sx9310`, nothing on a Linux desktop reads it |
| Proximity | none | - | no proximity sensor on either model |
| Fingerprint | Egis (`etspi`, BLSP1 SPI 6) | both | owned by a TrustZone app on stock; not reachable from Linux |
| Pressure, humidity, heart rate | none | - | |
| Virtual sensors (rotation vector, gravity, step counter, significant motion, gestures) | SLPI algorithms (SAM) | both | the SLPI computes them, but they are separate QMI services that no Linux driver speaks; the desktop does not need them |

The Back/Recent touch keys (Coreriver TC305K) are an input device, not a
sensor; they are not done yet (see the README).

## Inventory from stock

Stock Android's sensor list comes from the SLPI's registry defaults in
`/vendor/etc/sensors/sensor_def_qcomdev.conf` (the same file on both
models) and the HALs in `/vendor/etc/sensors/hals.conf`: `sensors.ssc.so`
(the SLPI sensors), `sensors.bio.so` and `sensors.grip.so` (the SM-T825's
SX9310). The registry configures four SLPI drivers: LSM6DS3 accelerometer
(SMGR sensor ID 0) and gyroscope (10), AK09916C (20) and CM3323 (40,
0x28); three more driver slots are empty (UUID 0). It also enables the
SLPI's algorithms (motion detection, gyroscope calibration, pedometer,
gestures, facing, gravity, orientation, rotation vectors, significant
motion) and carries a default magnetometer factory calibration (a soft-iron
matrix, with the bias marked invalid); each unit's own calibration is in
`sns.reg` on efs (below).

Outside the SLPI, Samsung's kernel (GPL, `msm8996-sec-gts3l*-r05.dtsi`)
has the cover's hall sensor in `gpio_keys` (`hall_flip`, both models) and,
on the SM-T825 only, two SX9310 grip sensors on GPIO I2C buses, of which
the LTE defconfig builds the driver for one (`CONFIG_SENSORS_SX9310`; the
"grip wifi" one, `CONFIG_SENSORS_SX9310_WIFI`, is off).

## How they are connected

There is no Samsung sensor hub and nothing on the application processor's
I2C buses. All of these sensors sit on the buses of the SLPI, Qualcomm's
sensor core (from the stock `sensor_def_qcomdev.conf`):

| Sensor | SLPI bus | Notes |
|---|---|---|
| LSM6DS3 | SSC SPI | interrupt TLMM 122, owned by the SLPI |
| AK09916C | SSC I2C 3, 0x0c | |
| CM3323 | SSC I2C 2, 0x10 | ambient light as the secondary data type |

Linux talks to the SLPI's sensor manager (SMGR) over QRTR. The msm8996
kernel already has the drivers: `qcom_q6v5_pas` boots the SLPI,
`qcom_sns_reg` serves it the sensor registry, and `qcom_smgr` exposes each
sensor as a buffer-only IIO device (`qcom-smgr-accel`, `-gyro`, `-mag`,
`-prox-light`).

## What the SLPI needs

- **Firmware:** Samsung-signed `slpi.mdt` + `slpi.b00`-`b14` from the
  `apnhlos` partition (`image/`). Relocatable, exactly the 10 MiB of
  `slpi_mem` (0x91700000).
- **Sensor registry (`sns.reg`):** Samsung keeps it on the **efs**
  partition, `/efs/FactoryApp/sensors/sns.reg`, not on `/persist` as other
  devices do. It has this unit's factory calibration, so a wiped userdata
  does not lose it. `qcom_sns_reg` loads it as
  `qcom/sensors/sns.reg-samsung,gts3lwifi`.
- **Power:** PM8994 LVS2 (SLPI pad supply and the sensors' supply), kept
  always on as in stock.

The device package's `gts3-firmware` (a boot service) copies the firmware
and the registry from the tablet's own partitions into
`/lib/firmware/updates/` on first boot; nothing proprietary is shipped.

## Auto-rotate

iio-sensor-proxy 3.9 works with these trigger-less buffers as they are. The
accelerometer has to be given in the panel's frame: the compositor turns
the output by the panel's mount (`rotation = <270>` in the DT, a 2048x1536
panel) and the sensor orientation on top. Upright on the keyboard stand
is therefore "normal", not upright portrait.

SMGR reports north-east-down axes (X to the top edge in portrait, Y to
the right, Z into the screen) and +1 g upwards. The device package's udev
rule `61-gts3-accel.rules` sets `ACCEL_MOUNT_MATRIX` (which
iio-sensor-proxy prefers over the driver's matrix) to
`1, 0, 0; 0, -1, 0; 0, 0, -1` on both models. That's the rotation into the
panel's frame:

| Matrix | SM-T820 on the keyboard stand (raw -0.04, 8.15, -5.21 m/s^2) |
|---|---|
| `1, 0, 0; 0, -1, 0; 0, 0, -1` (the rule) | normal, tilted-up: right, what the desktop shows (KWin rotation none) |
| `0, -1, 0; -1, 0, 0; 0, 0, 1` (the SMGR driver's) | right-up: the desktop turns 90 degrees |
| `1, 0, 0; 0, -1, 0; 0, 0, 1` (the rule before, SM-T825 only) | normal, tilted-down: the tilt is upside down |

Results come from iio-sensor-proxy's `orientation_calc()` on that reading.
The first row is also what iio-sensor-proxy reports on the tablet, where a
local rule with this matrix has been in place since 2026-10-30. On an
SM-T825 the X and Y of this matrix were confirmed in all positions, with
Z +1. An earlier version of this page said the SM-T820 needs the driver's
matrix. That came from a check on a board without a panel, which counts
upright portrait as normal.

On 2026-11-01 an SM-T820 was turned by hand through all four positions, with
the local rule (the same matrix), and the desktop came out upright in each.
After a reboot with this rule at its packaged path and no local copy, udev
sets the matrix, iio-sensor-proxy reports normal on the stand and KWin's
rotation is none.

Still to check: the tilt on an SM-T825.

If iio-sensor-proxy was installed after the sensors appeared, run
`udevadm trigger --subsystem-match=iio --action=add` once (or reboot).

### KWin (`pmaports/kwin`)

KWin 6.8.1 rotates the screen from the sensor. Two KWin bugs showed up on
this tablet; `pmaports/kwin` is postmarketOS's systemd KWin package
(extra-repos/systemd/kwin, 99996.8.1-r0) with a fix for each (pkgrel 100,
both fixes are for upstream KWin and not merged yet):

- 0001, snow during the rotation animation: the animation snapshots the old
  frame with `Scene::paint()`, which takes a region in device pixels, but it
  is passed the logical geometry. At 175% only the top left 1171x878 of the
  2048x1536 snapshot was painted; the rest showed whatever the texture
  memory held (noise, pieces of old frames). Fixed by painting the whole
  device rect. Checked with scanout buffer dumps during a rotation.
- 0002, the screen turns on in the wrong orientation: auto-rotate stops
  listening to the sensor while the screen is off. On wake the new sensor
  subscription reported "undefined" before iio-sensor-proxy answered, so
  KWin applied the manual transform (portrait) and rotated back to
  landscape about a second later: two rotations on every wake. KWin now
  keeps the last reading until the first real one arrives, and still
  re-applies that reading once, so a transform changed while the screen
  was off is corrected. Checked with a webcam on the screen: normal wakes
  and lock screen wakes stay in landscape; a wake after a manual portrait
  rotation turns back to landscape right away.

The package also drops the `CAP_SYS_NICE` file capability that KWin's
install step puts on `kwin_wayland` (for real-time scheduling). On musl, a
file capability makes the program AT_SECURE, `issetugid()` returns true,
and libdbus then ignores `DBUS_SESSION_BUS_ADDRESS`: KWin lost the session
bus (no `org.kde.KWin`, global shortcuts and session management broken).
postmarketOS's builds don't ship the capability today only because
fakeroot 1.38.1 loses it. Real-time scheduling without a capability works
with `RLIMIT_RTPRIO` 1 for KWin, e.g. with systemd drop-ins:
`LimitRTPRIO=0:1` for `user@.service` and `LimitRTPRIO=1` for the user
unit `plasma-kwin_wayland.service`. KWin then puts its main, DRM commit and
libinput threads at SCHED_RR priority 1 by itself. That isn't part of the
device package.

## Light sensor

SMGR lists the CM3323 under the proximity/light sensor ID (0x28) with a
single data type of type 1 (secondary): "Capella CM3323 ALS", 5 Hz. Stock
registers it with no primary data type (`data_type1 0`, `data_type2 6` in
`sensor_def_qcomdev.conf`). The driver always streamed the primary data type,
which SMGR rejects with 0x501. Patch 0077 keeps each data type's own type,
streams the secondary one for such a light-only sensor and registers it as
`qcom-smgr-light` with an `in_intensity_both` channel, the layout
iio-sensor-proxy's buffered light driver reads. The value is plain lux
(scale 1), not 16.16 fixed point like the motion sensors. The sample's second
word moves between about 3960 and 5270 (warm room light vs a cool torch),
probably the colour temperature in kelvin; it is not exposed. Nothing would
read it: iio-sensor-proxy's D-Bus API has only `LightLevel` and
`LightLevelUnit`, and neither KWin nor Plasma (night light included) uses a
colour temperature. Before exposing it (as `in_colortemp`, like
`hid-sensor-als`), compare it with a reference under a few known lights.

The sensor streams at 5 Hz by default, the maximum SMGR reports for it. A
write of 10 Hz to `in_intensity_both_sampling_frequency`, which
iio-sensor-proxy does when it opens the sensor, is capped and reads back 5.
Asked for 1 Hz, SMGR still samples at 5 Hz and delivers five reports
together once a second.

It sees some of the panel's own light. On the SM-T820 in room light
(2026-11-01) it read 19 lux with the backlight at 1 and 29 lux at 255,
steady for several samples at each level. The reading follows the panel
(see [Auto-brightness](#auto-brightness)). The share in a dark room is
not measured yet.

## Auto-brightness

KWin 6.8 has ambient-light auto-brightness (`src/utils/lightsensor.cpp`,
`Workspace::applyLightChanges()` in `src/workspace.cpp`). It claims the
light sensor from iio-sensor-proxy, accepts only readings in "lux" (what
iio-sensor-proxy reports for IIO sensors), and sets the output's brightness
from a per-output curve of (lux, brightness) points. It is per output and
off by default:

1. `kscreen-doctor -o` should list the panel with "Automatic brightness:
   supported" (it needs `HasAmbientLight` from iio-sensor-proxy).
2. Turn it on with `kscreen-doctor output.<name>.autoBrightness.enable`, or
   "Automatically adapt to environment" under Brightness in System
   Settings > Display.
3. Train it: the curve starts as a single point (0 lux = 100 %), so until
   then the panel goes to full brightness. Each manual brightness change
   (slider, keys) adds a point at the current light level; keep the light
   steady for a few seconds, then set the brightness, at several levels
   (dark, room, window, outside). KWin interpolates linearly in lux, so a
   curve with only 20 and 30000 lux stays near the 20 lux value indoors.
   The curve is stored as `automaticBrightness` and `autoBrightnessCurve`
   in `~/.config/kwinoutputconfig.json`; edit that file only with Plasma
   stopped.

KWin waits 2 s after a reading changes, ignores changes under 20 %
(`KWIN_AUTO_BRIGHTNESS_DEADZONE`, in percent), and releases the sensor while
the screen is off. Brightness written to sysfs directly is not learned.

Because the light sensor sees the panel (about 10 lux between minimum and
maximum backlight in room light, more than the 20 % dead zone at room
levels), a raised brightness raises the next reading. In a dim room this
may ratchet the brightness up. Train and check the curve in dim light
before relying on it.

## Compass

The magnetometer is a plain 3-axis IIO device (`qcom-smgr-mag`,
`in_magn_{x,y,z}` in gauss).
iio-sensor-proxy 3.9 has no compass driver for that: its only IIO compass
driver reads a tilt-compensated heading channel
(`in_rot_from_north_magnetic_tilt_comp`, which HID sensor hubs provide), so
`HasCompass` stays false. A compass needs either a new iio-sensor-proxy
driver that combines the accelerometer and the magnetometer (upstream
work) or a heading from the SLPI's own algorithms, which no Linux driver
reads. Nothing on the Plasma desktop uses a compass today; GNOME Maps (via
geoclue) and Qt's `QCompass` would.

Two things matter for that:

- **Calibration: none applied.** Laid flat and turned slowly through
  360 degrees (SM-T820, 2026-11-01, `smgr-read.sh qcom-smgr-mag 1200 10`,
  every 30 degree sector covered), X and Y traced a curve centred at
  X +0.107, Y -0.005 gauss, with the X radius about 0.7 times the Y radius
  (part of that may be tilt or the desk). |B| changed with the heading.
  The SMGR stream is the raw field: no hard-iron (bias) correction, and
  probably no soft-iron one. A compass driver needs its own calibration.
- **Axes: patch 0145.** On the SM-T820, SMGR reports all 3-axis sensors in
  north-east-down coordinates (X to the top edge in portrait, Y to the
  right, Z into the screen). The driver gave all three the accelerometer's
  matrix, `0, -1, 0; -1, 0, 0; 0, 0, 1`. That is the negated rotation, whose
  X and Y signs suit iio-sensor-proxy for the accelerometer: SMGR reports
  +1 g upwards, and iio-sensor-proxy takes a negative Y as upright. For a
  field or an angular velocity it inverts the reading. On the SM-T820 lying
  flat, a heading from X and Y read 180 when a phone compass said north and
  270 at east, and counter-clockwise turns read negative on the gyroscope's
  Z. Patch 0145 gives the gyroscope and the magnetometer the rotation
  `0, 1, 0; 1, 0, 0; 0, 0, -1`. The right result follows from those
  readings; they were not repeated with the patch, and the magnetometer's Z
  and the gyroscope's X and Y were not checked. The SM-T825 uses the same
  accelerometer matrix (above), so its magnetometer and gyroscope probably
  match too. Not checked.

`tools/compass.sh` is a test compass. It calibrates while the tablet
turns flat through a circle, then prints the heading of the top edge
(opposite the USB port) from the magnetometer alone. There's no tilt
compensation, because KWin holds the accelerometer buffer. It applies
`in_mount_matrix`, so without 0145 pass an offset of 180.

## Cover (hall)

`dts/test/test-hall.dts` adds the cover's hall sensor as its own
`gpio-keys` device: SW_LID, set when the cover is closed (PM8994 GPIO 7,
active low, no pull, 15 ms debounce, as in stock). Opening the cover wakes
the tablet; closing it does not (`wakeup-event-action`). It stays out of
the main DT until suspend works: with SW_LID, logind (`HandleLidSwitch`, at
the login screen) and PowerDevil (in a Plasma session, "When laptop lid
closed") suspend the tablet when the cover closes. To turn the switch off
without a reboot:
`echo 0 > /sys/devices/platform/gpio-hall-sensor/disabled_switches`.

To check: `evtest` shows SW_LID 1 with the cover closed and 0 open, also
with the Book Cover Keyboard folded behind the tablet; the switch wakes the
tablet from suspend when the cover opens and not when it closes; the
interrupt does not fire while the cover stays still.

## Grip (SM-T825)

`dts/test/test-sar.dts` adds the SM-T825's SX9310 next to the LTE antenna
(GPIO I2C on TLMM 81/82, interrupt TLMM 132) with the mainline `sx9310`
driver (`CONFIG_SX9310`, not in the package config). It uses the driver's
default register setup, not Samsung's; the readings will tell whether the
`semtech,*` tuning properties are needed. Android lowers the modem's
transmit power when it reports near; nothing on a Linux desktop reads it,
so it only matters once the modem works.


## Tests on the tablet

Done on the SM-T820 (2026-11-01, kernel r8 with patches 0140-0145 as a
module, iio-sensor-proxy 3.9 with the local restart patch from
[Known issues](#known-issues), KWin 6.8):

1. **Kernel patches.** `monitor-sensor` shows the orientation (normal,
   tilted-up) and the light level. `smgr-read.sh qcom-smgr-accel 5` gives
   |g| = 9.7 m/s^2. Raw scans (all channels, timestamp included): the 4
   bytes before the timestamp held a kernel address before 0140 and are 0
   after it; the timestamp follows CLOCK_REALTIME (2.5 ms apart at 400 Hz);
   `in_timestamp_type` is `le:s64/64>>0` after 0144. Rates, counted over six
   seconds (0143):

   | Sensor | Written (Hz) | Streamed (Hz) |
   |---|---|---|
   | accel | 12 / 50 / 100 / 200 / 416 | 12.2 / 49.3 / 98.6 / 197 / 394 |
   | gyro | 10 / 100 / 416 | 12.2 / 99 / 396 |
   | mag | 10 / 50 | 10.1 / 50.0 |
   | light | 5 / 10 | 4.8 / 4.8 (reads back 5) |

   SMGR accepts rates between the native ones and streams at a native one:
   the accelerometer and gyroscope do not go below about 12.5 Hz, while the
   attribute still reads what was written. The accelerometer's reports give
   52, 104, 208 and 416 Hz for those rates, and it runs about 5 % under them.

   Reports with two samples (0142), counted in the handler: one each on the
   accelerometer at 50 Hz and up, the gyroscope at 100 and 416 Hz and the
   magnetometer at 10 and 50 Hz, none on the rest (at 416 Hz, 2372 reports
   and 2373 samples). No timestamp step is zero or negative.

   Scans with fewer axes (0141): every subset of the axes, with and without
   the timestamp, on the accelerometer, gyroscope and magnetometer, and the
   light sensor with and without it. Records have the size the channels
   imply, the padding is 0 and timestamps rise at the set rate. Each axis
   matches a full scan on the accelerometer and magnetometer (the gyroscope's
   offset moves between buffer enables, so its values were not compared).
2. **Light vs panel.** 19 lux at backlight 1, 29 lux at 255 in room light
   (see [Light sensor](#light-sensor)). Still to do: a dark room, and
   training KWin's auto-brightness.
3. **Compass groundwork.** Calibration done, none applied. Axes checked
   at north and east with `tools/compass.sh` and a phone compass, and
   the gyroscope's Z with turns; both were inverted before 0145 (see
   [Compass](#compass)). Still to do: the SM-T825.

Still to do:

4. Hall: boot `dts/test/test-hall.dts` (kernel unchanged), `evtest` on
   "GPIO Hall Effect Sensor" while closing and opening the cover, with and
   without the Book Cover Keyboard. Set PowerDevil's lid action to "Do
   nothing" (and `HandleLidSwitch=ignore` for logind) before the test while
   suspend is unreliable.
5. Grip (SM-T825, kernel with `CONFIG_SX9310=m`): boot
   `dts/test/test-sar.dts`, read `in_proximity*_raw` with a hand on and off
   the antenna area.

`smgr-read.sh` needs iio-sensor-proxy stopped. With KWin running, starting
it again leaves auto-rotate dead (see [Known issues](#known-issues)), so
reboot after such a test.

## Known issues

- The driver pushed each sample from an uninitialised stack buffer and
  never set its timestamp, so with the timestamp enabled (iio-sensor-proxy
  enables all channels) every motion sample carried 8 bytes of kernel stack
  to userspace: a kernel address, so a KASLR leak. Patch 0140 clears the
  sample and timestamps it (tested).
- Every timestamp channel was declared unsigned with 32 real bits, so a
  reader that honours realbits (`tools/iio/iio_generic_buffer`) cuts the
  64-bit nanosecond timestamp to 32 bits (it wraps every 4.3 s). Patch 0144
  uses `IIO_CHAN_SOFT_TIMESTAMP` (tested).
- The gyroscope and the magnetometer had every axis inverted (the
  accelerometer's sign-flipping matrix). Patch 0145 gives them a plain
  rotation (see [Compass](#compass) for what was checked).
- The first sample after a buffer enable is stale or unsettled: the
  gyroscope's first sample read -1.4, 0.3, -1.7 rad/s at rest, and the
  accelerometer's first few read low (6.3 then 7.8 m/s^2 before about
  9.7). Readers that care should drop the first sample.
- SMGR puts two samples in one report of most streams, and the driver
  pushed only the first. Patch 0142 pushes them all, spaced back from the
  report's arrival by the sampling period (tested).
- With fewer than three axes enabled, a 3-axis buffer got the wrong axes;
  patch 0141 pushes only the enabled axes (tested). It does not use the IIO
  core's demux (`available_scan_masks`): with the last channels of a scan
  off, the core takes the timestamp from the wrong offset. Tried on the
  SM-T820, `x` with the timestamp reads the Z sample as the timestamp. That
  bug is in `iio_buffer_update_demux()`, in mainline as well.
- Writes to `in_*_sampling_frequency` were ignored: every buffer enable
  went back to the fastest rate, one QMI message per sample. Patch 0143
  keeps the written rate, capped at the maximum SMGR reports (tested, rates
  above). Nothing writes one yet, so
  the accelerometer runs at its fastest rate, 416 Hz. 12 Hz (it streams at
  12.4 Hz) would lose nothing: iio-sensor-proxy reads it every 700 ms and
  uses only the newest scan, and at 416 Hz the buffer filled in 0.3 s, so
  the scan it used was stale anyway. Tried with
  `ATTR{in_accel_sampling_frequency}="12"` in `61-gts3-accel.rules`: it
  takes effect from boot, and turning an SM-T820 by hand through all four
  positions felt the same as at 416 Hz (not timed). Measured on the
  SM-T820 with the screen on and the desktop idle (10 s windows, SLPI
  `smd-edge` IRQs, CPU busy over 4 cores, residency in the deepest idle
  state):

  | Accelerometer | IRQ/s | CPU busy | Deepest idle |
  |---|---|---|---|
  | off (screen off) | 0 | 1 % | 98-99 % |
  | 12 Hz | 12 | 2 % | 97 % |
  | 416 Hz | 397 | 4 % | 93-95 % |

  The SLPI channel batches: the gyroscope at 416 Hz on top of the
  accelerometer added only about 20 IRQ/s. The rate can only change
  while the buffer is off, i.e. while the screen is off (KWin drops its
  claim) or before iio-sensor-proxy opens the device.
- There is no `*_sampling_frequency_available` attribute. The driver has a
  `read_avail` callback, but no channel sets `info_mask_*_available`, so the
  core never calls it. Nothing needs it yet.
- Restarting iio-sensor-proxy while KWin runs leaves the orientation at
  "undefined" for every client. iio-sensor-proxy 3.9 takes its D-Bus
  name before it has probed the IIO devices, so KWin's immediate
  `ClaimAccelerometer` fails (`driver_set_polling: assertion
  'sensor_device' failed`); the claim is counted, but polling never
  starts. At boot iio-sensor-proxy starts before KWin, so this does not
  happen. A reboot recovers. A local patch to iio-sensor-proxy fixes it
  (10 of 10 restarts on the SM-T820); it is not packaged.

- `qcom_smgr` registered `qmi_handle_release` through a function-pointer
  cast; with kCFI every unbind (stopping the SLPI, unloading the module)
  oopsed and left the remoteproc lock held. Patch 0076 fixes it. Before
  0076, do not stop or restart the SLPI.
