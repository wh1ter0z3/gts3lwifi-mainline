# Sensors

## Status (2026-10-30): accelerometer, gyroscope, magnetometer and light sensor work; auto-rotate and the light sensor work with iio-sensor-proxy

| Sensor | Chip | State |
|---|---|---|
| Accelerometer | ST LSM6DS3 (K6DS3TR) | works, orientation correct in all four positions |
| Gyroscope | same chip | works |
| Magnetometer | AKM AK09916C | works (about 0.4-0.6 gauss) |
| Ambient light (RGB) | Capella CM3323 | works with patch 0077: `qcom-smgr-light`, `in_intensity_both` in lux (about 20 indoors, up to ~31000 under a phone torch, 0 when covered); iio-sensor-proxy reports it |
| Cover (hall) | PM8994 GPIO 7, low = closed | not in the DT yet: closing the cover would suspend the tablet, and suspend is not reliable yet |

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
accelerometer axes come from the SLPI, and the two models do not report them
in the same frame:

| Model | Raw gravity, portrait with USB at the bottom | Mount matrix |
|---|---|---|
| SM-T820 | +X | the SMGR driver's own, `0, -1, 0; -1, 0, 0; 0, 0, 1` (iio-sensor-proxy reports normal, right-up, bottom-up, left-up in the right positions; checked on a board without a panel) |
| SM-T825 | +Y | `1, 0, 0; 0, -1, 0; 0, 0, 1` (with the driver's matrix the desktop was 90 degrees off in every position; confirmed in all positions with this one) |

The device package's udev rule `61-gts3-accel.rules` sets
`ACCEL_MOUNT_MATRIX` (which iio-sensor-proxy prefers over the driver's
matrix) on `qcom-smgr-accel` only when the device tree is the SM-T825 one
(`samsung,gts3llte`). A per-model `mount-matrix` in the device tree would be
cleaner, but the SMGR sensors have no DT nodes of their own.

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
probably the colour temperature in kelvin; it is not exposed.

## Known issues

- `qcom_smgr` registered `qmi_handle_release` through a function-pointer
  cast; with kCFI every unbind (stopping the SLPI, unloading the module)
  oopsed and left the remoteproc lock held. Patch 0076 fixes it. Before
  0076, do not stop or restart the SLPI.
