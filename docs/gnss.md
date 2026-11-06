# GNSS (GPS)

## Status (2026-11-02): GPS is on for the SM-T820: the modem DSP is in the main DT and `device-samsung-gts3lwifi-gnss` comes with the device package

| Model | Where the GNSS engine is | Stock | Mainline |
|---|---|---|---|
| SM-T820 (Wi-Fi, APQ8096) | modem DSP (MSS), which runs a GNSS-only modem image ("modemlite") | works: satellites and position reports at 1 Hz in LineageOS | **works**: the modem boots at startup as on stock, first fix 2.5-8 min after it starts, and geoclue gets it through gnss-share with our `qmi_loc` driver (`pmaports/gnss-share`, `device-samsung-gts3lwifi-gnss`; see "Results"). The regression checklist (step 10) is still open |
| SM-T825 (LTE, MSM8996) | modem DSP, in the full LTE modem image | should work the same way (same location HAL) | off: its DT disables the modem DSP until someone tests it on one; we have no dump of its modem partition |

"APQ8096 has no modem" is only true of the cellular part. The modem
subsystem (MSS, a Hexagon QDSP6 v5.5) is there on the SM-T820 too, and
Samsung loads a modem image into it that has only GNSS and the services
that GNSS needs. There is no separate GNSS chip on either model.

On mainline the chain is: `qcom_q6v5_mss` boots the modem DSP, `rmtfs`
serves it its file system (the `modemst1`, `modemst2`, `fsg` and `fsc`
partitions), `tqftpserv` serves its configuration files, and the location
service (QMI LOC, QRTR service 16) is reached over QRTR. Every part of
this chain is upstream and in the kernel package's config; the first test
needs no kernel patch and no config change. What the DT adds is the MSS
node and the memory layout that the modem image needs (below).

## Results (SM-T820, 2026-11-01)

Kernel 6.19.5 from the installed pmOS package, RAM-booted with
`dts/test/test-gnss.dts` (since moved into the main DT unchanged) from
lk2nd's fastboot. Nothing in the kernel was
changed.

- **Modem:** `MBA booted without debug policy, loading mpss`, then
  `remote processor 2080000.remoteproc is now up`. ADSP and SLPI came up as
  usual with the moved regions.
- **Services on the modem's QRTR node (0):** 16 (location, QMI LOC v2),
  21 (modem EFS), 22 (time), 23/24 (thermal), 43, 55 (QMI-SLIM), 66, 227
  and 232. There's no WDS, DMS or NAS, so ModemManager can't take this modem
  and geoclue needs a QMI LOC bridge.
- **rmtfs starts by itself:** the `rmtfs` package's udev rule starts
  `rmtfs -r -P -s` as soon as `/dev/qcom_rmtfs_mem1` appears, even with the
  service disabled. `-s` then boots the modem. Mask the service (`systemctl
  mask rmtfs`) to keep it off. tqftpserv must be started by hand: the
  modem only asked it for `/readwrite/server_check.txt`.
- **qmicli can't hold a session:** `--client-no-release-cid` plus
  `--client-cid` through `qmi-proxy` times out on QRTR ("Transaction timed
  out" for every follow and stop call), so the first `gnss-test.sh fix`,
  built on qmicli, didn't work. A single process that keeps one QRTR
  socket does. The test used a small Python client: REG_EVENTS (0x21,
  mask 0x3ff7), SET_OPERATION_MODE (0x4a, standalone), START (0x22,
  periodic 1 s, intermediate reports on) and INJECT_UTC_TIME (0x38) from
  the NTP-synced clock, all on one socket. `tools/qmi-loc.py` and
  gnss-share's `qmi_loc` driver now do the same.
- **Engine:** indoors at a desk, the session runs and NMEA flows
  (about 6 sentences/s), but no satellite is seen and there is no fix. The
  engine asks for time (0x28), predicted orbits (0x29) and a position
  (0x2a); after the time injection it no longer asks for time. Mode `msb`
  is the default; standalone (4) is accepted.
- **At a window/outdoors (cold start, time injected, no orbit data):** first
  satellite after about 340 s, first fix after about 380 s. Then 13 GPS and
  about 10 GLONASS satellites in view, 6-8 used, C/N0 up to 37-38 dB-Hz,
  HDOP 0.8-1.0, and a horizontal uncertainty of 10 m most of the time
  (4-11 m). 229 successful position reports in 15 minutes. QMI SV info and
  position report indications arrive once satellites are tracked.
- **Memshare** was not needed for a fix.

### Through gnss-share and geoclue (test steps 4-8)

Same layout as the main DT now has (`test-gnss.dts` at the time), at a window. All of it passed.

| Step | Test | Result |
|---|---|---|
| 4 | gnss-share by hand, its event mask 0x194 | no engine without a client; first client starts the session, time injected, NMEA flows, first fix after 141 s; engine off when the client leaves |
| 5 | two clients | one `Starting device`; the session survives the shorter client and stops with the longer; a new session fixes in 6-7 s |
| 5 | `pkill -9 gnss-share` | engine off within 8 s |
| 6 | modem down and up during a session | the driver sets the session up again by itself; new fix after 500 s (`rmtfs -r` keeps nothing) |
| 7 | packages installed, RAM boot | rmtfs, tqftpserv and gnss-share start by themselves, modem up, socket there; engine stays off for 150 s idle |
| 7 | `where-am-i -a 8`, GPS-only drop-in | first location after 171 s, 61 updates; session stops with where-am-i, engine off |
| 7 | `where-am-i -a 4` | gnss-share gets no client |
| 8 | normal boot, packages installed | rmtfs, tqftpserv and gnss-share inactive (`start condition unmet`), no rmtfs process, no modem in dmesg; ADSP and SLPI running, orientation reported, sound cards and sinks present, Wi-Fi up, no failed units, stock geoclue starts |

Not checked: whether where-am-i's permission went through the on-screen
prompt or was allowed without one; sound in step 8 by ear; a request with
geoclue's stock configuration and a real fix (it would go online, below).

- **geoclue's accuracy is not a measurement.** For NMEA it comes from a
  table on the HDOP (`get_accuracy_from_hdop`: up to 1 gives 0 m, up to 2
  gives 1 m, up to 5 gives 3 m, up to 10 gives 50 m). `where-am-i` printing
  0 or 3 m is that table. The modem's own figure is about 10 m (above).
- **Stock geoclue also goes online.** Its configuration keeps `[wifi]` and
  `[ip]` enabled, and a request starts every source at or below the
  requested level (`gclue_locator_start`). So an exact request uses GPS and
  also sends nearby Wi-Fi networks and the IP address to
  `api.beacondb.net`; a coarse one uses only those two. This comes from
  reading the code; our tests ran GPS-only. gnss-share's packaging changes
  none of it.
- **GPS-only test configuration.** A drop-in in `/etc/geoclue/conf.d/` with
  `enable=false` in `[wifi]`, `[3g]`, `[cdma]`, `[modem-gps]` and `[ip]`.
  Disable all of them: with `[wifi]` off and the modem sources left on,
  geoclue hangs at startup and never finishes activating. Stock starts
  fine, and so does turning off any other single source. Not chased
  further.

### On a package install with the main DT (step 10, 2026-11-01)

Kernel r10 and device r7, built from this branch plus the sensors series,
installed with `apk add -u` from a local repository, then booted normally
(no RAM boot):

- **Packages:** asking for the device package alone pulled in the `-gnss`
  subpackage, gnss-share, `gnss-share-geoclue` and `gnss-share-systemd`.
- **Modem:** up about 25 s into each of 6 boots. With its firmware removed
  from the rootfs (a first boot), `gts3-firmware` installed 105 files and the
  modem came up on that same boot, 3 out of 3 times.
- **ADSP, SLPI:** running; the IIO sensors report, orientation correct.
- **GPU:** KWin composites with OpenGL ES.
- **Venus:** H.264 720p through `v4l2h264dec` is byte-identical to a software
  decode. HEVC fails to negotiate on this kernel; it works on 7.3.1 (`docs/venus.md`).
- **Idle:** the modem adds no interrupts we can see (under 1/s); the 396/s on
  `smd-edge` is the accelerometer.
- **GNSS session at a desk:** starts with the client and stops with it. No
  satellites indoors, as before.

Audio by ear and a fix at a window ran on Linux 7.3.1, below. Open: any boot
on an SM-T825.

**Bluetooth timed out once in 6 boots:** `hci0: Opcode 0x0c03 failed: -110`
after its second setup, on the first boot of this install, and the adapter
was missing until a reboot. The other 5 boots were fine, with the same
timing, and it was not seen in earlier boots without the modem. Cause
unknown.

### Audio, a fix at a window, and sleep (steps 9 and 10, 2026-11-02)

On Linux 7.3.1 (fork branch `linux-7.3`), which carries this branch, installed:

- **Audio, by ear:** speakers and headphones sound clean. The mic works but is
  quiet: its gain was never tuned.
- **Fix at a window:** first fix after 80 s, probably with orbit data kept from
  earlier fixes. 12 GPS and 10 GLONASS in view, HDOP about 0.8.
- **Idle modem across s2idle:** 30 s by RTC, twice. Both slept the full time, so
  the modem and gnss-share didn't wake it.
- **Session across s2idle:** 30 s in the middle of a 120 s session. It carried on:
  123 GGA in 123 s, 117 with a fix, modem running, nothing in dmesg, and
  gnss-share stopped the session at the end. No sentence seems to be lost, so a
  client left connected probably keeps the engine on through a suspend.

Not measured: what the modem costs in battery, idle or in suspend.

## Evidence

SM-T820:

- **Modem partition** (`state/firmware/vendor_firmware-modem.tar`, the
  `modem` partition as Android mounts it, `/vendor/firmware-modem`).
  `verinfo/ver_info.txt`: modem `MPSS.TH.2.0.1-00316-M8996FAAAANAZM-1`,
  meta `MSM8996.LA.2.0-01350-STD.PROD-1`, `"Product_Flavor": "modemlite"`.
  Strings in the image: `oem_test_gpsonly.c`, `gnss_apq_pga_cal`,
  `loc_qmi_shim.c`, `gps_server`, GLONASS/BeiDou/Galileo/QZSS; also a
  memshare client (`mem_share_qmi_client.c`, `mem_share_request_pool`),
  `rfs_tftp.c`, the service registry and SSCTL. Its segments with data add
  up to about 7 MB.
- **Stock kernel** (LineageOS 21 on Samsung's 3.18,
  `diagnostics/20261006T163931Z-android14-after-backup-6d5814a1/kernel-log.txt`,
  identity SM-T820): `pil-q6v5-mss 2080000.qcom,mss: modem: loading from
  0x0000000088800000 to 0x000000008f400000`, `MBA boot done`, `modem:
  Brought out of reset`, sysmon over SSCTL, and `rmt_storage` serving
  `/boot/modem_fs1`, `modem_fs2`, `modem_fsg` and `modem_fsc`. The same log
  places SLPI at 0x8f400000, ADSP at 0x8fe00000 and the A530 zap shader at
  0x91f00000.
- **Stock logcat** (`diagnostics/wifi-pno-review-20261008/logcat.bin`):
  LocSvc `QMI_LOC_EVENT_GNSS_SV_INFO_IND_V02` and
  `QMI_LOC_EVENT_POSITION_REPORT_IND_V02` at 1 Hz, 122 of them on
  2026-10-08 at 07:56; `QMI_LOC_START_REQ`/`STOP` in the kernel-smoke log
  of 2026-10-06. The fix quality is not in the log.
- **When the modem runs on stock:** from boot to shutdown, whether or not
  anything asks for a location. `init.target.rc` starts `per_proxy`
  (`pm-proxy`) as soon as `per_mgr` runs and stops it only on
  `sys.shutdown.requested`. The kernel log above shows `__subsystem_get:
  modem` at 11.6 s from `per_mgr`'s binder thread and `Brought out of reset`
  at 14.3 s; `previous-kernel-log.txt` of
  `20261006T150951Z-baseline-twrp-4a799ad3` shows `subsystem_put: modem` as
  the system goes down.
- **No other GNSS hardware.** The QCA1530 start in the device's
  `init.target.rc` is commented out; Samsung's gts3lwifi DTs have no GNSS
  pins or nodes; the HAL is Qualcomm's location HAL over QMI
  (`proprietary-files.txt` lines 540-628, `gnss@1.0`/`1.1` in `gts3l.mk`).
- **Downstream DT** (Samsung GPL 3.18). `apq8096-v3.dtsi` disables only
  IPA and rmnet, the modem PIL stays (`qcom,pil-q6v55-mss`, PLL supply
  PM8994 L12 at 1.8 V). `msm8996-sec-common.dtsi`, which both models
  include: `modem_mem` 0x88800000 + 0x6c00000, `peripheral_mem` 0x8f400000
  + 0x3100000, and a 96 MiB memshare pool `modem_shared_mem` at 0x93000000
  (memshare client "modem", id 1, size 0: allocated on request). The
  gts3lwifi defconfig has `MSM_PIL_MSS_QDSP6V5=y`,
  `IPC_ROUTER_GLINK_XPRT=y` and `MEM_SHARE_QMI_SERVICE=y`.
- **Partitions** (`state/h0/by-name.txt`): `modem` p39 (85 MiB, the
  images), `modemst1` p23 and `modemst2` p24 (2 MiB each), `fsg` p14
  (2 MiB), `fsc` p33 (4 KiB).

SM-T825: MSM8996 (msm-id 246) with the full LTE modem image and the same
location HAL, so GNSS goes through the same QMI LOC service. Stock talks
to the modem with Samsung's RIL (`libsec-ril` over `/dev/umts_ipc0`) and
`netmgrd` for data. We have no copy of its modem partition: the image's
size, memory span and file names are not known.

The SM-T825 DT reserves `modem_shared_mem` with the comment "LTE model
only"; downstream has it for both models. It is the memshare pool, and
nothing on mainline uses it (no memshare driver), so the SM-T820 DT does
not need it.

## Mainline status

| Piece | Upstream | Here |
|---|---|---|
| `qcom_q6v5_mss`, `qcom,msm8996-mss-pil` | yes; apq8096-db820c enables it | `=m` in the kernel package |
| sysmon, PDR helpers, pd-mapper (msm8996 and apq8096 tables) | yes | `=m` |
| QRTR over SMD/GLINK | yes | `=y` |
| rmtfs memory (client 1, given to the modem through SCM) | yes, in msm8996.dtsi | probes on the tablet today |
| `rmtfs`, `tqftpserv`, `qrtr` | Alpine/pmOS packages | rmtfs and tqftpserv pulled in by the optional `device-samsung-gts3lwifi-gnss` subpackage |
| MSS node | msm8996.dtsi, disabled | enabled in the main DT (both models) |
| Memory layout | msm8996.dtsi `mpss_mem` is 0x6200000 | too small for the modem image; the main DT uses the stock layout |
| Memshare (QMI service 0x34) | no; Nikita Travkin's series of 2021-03-19 was not merged | maybe needed, see open questions |
| QMI LOC in the kernel (GNSS subsystem) | no driver | not needed to test |
| geoclue | `modem-gps` (through ModemManager) and `nmea-socket` sources | `nmea-socket` fed by gnss-share's `qmi_loc` driver (ours, not upstream; below) |

No kernel config change is needed (`CONFIG_GNSS` stays off; nothing uses
it). `qcom_q6v5_mss` relocates the image into `mpss_mem` and refuses a
segment that ends past it ("segment outside memory range").

## Memory layout (main DT)

The SM-T820 modem image spans 0x6c00000 bytes from its lowest segment to
the end of its last (25 program headers, relocatable; segments 9 and 24
are large zero-filled ones), which is exactly the stock `modem_mem`. The
main DT grows `mpss_mem` to that and moves the regions after it to where
the stock kernel puts them (all of those images are relocatable):

| Region | Before (msm8996.dtsi with the old main DT overrides) | Main DT (= stock SM-T820) |
|---|---|---|
| mpss | 0x88800000 + 0x6200000 | 0x88800000 + 0x6c00000 |
| slpi | 0x91700000 + 0xa00000 | 0x8f400000 + 0xa00000 |
| adsp | 0x8ea00000 + 0x2500000 | 0x8fe00000 + 0x2100000 (the image's span) |
| gpu (zap) | 0x90f00000 + 0x100000 | 0x91f00000 + 0x100000 |
| venus | 0x91000000 + 0x500000 | 0x92000000 + 0x500000 |
| mba | 0x91500000 + 0x200000 | 0x92500000 + 0x200000 (stock allocates it anywhere; mba.mbn is 209 KiB) |
| reserved tail at 0x92100000 | + 0x400000 | removed, now inside venus |

0x8f400000-0x92500000 is the stock `peripheral_mem`. The MSS also gets
`pll-supply = <&vreg_l12a_1p8>` (downstream `vdd_pll`) and Samsung's
firmware paths. Checked offline: `make CHECK_DTBS=y` for both boards,
`dt-validate` of both test DTs with no finding the board DTs do not have
already, no overlapping regions, and every user of a moved region points
at the new node.

This layout was tested on 2026-11-01 as `dts/test/gnss.dtsi` (with
`test-gnss.dts` and `test-gnss-lte.dts`) and then moved into the main DT
unchanged: the compiled trees of both boards are identical to those of the
test DTs, which are gone, apart from the SM-T825's MSS, which is now off.
The regression checklist of test step 10 is still to be run on the main DT
build.

## Firmware

All from the tablet's own `modem` partition (`image/`), copied by the
device package's `gts3-firmware` into
`/lib/firmware/updates/qcom/msm8996/samsung/gts3/` (nothing is shipped):

- `mba.mbn` (the modem boot authenticator)
- `modem.mdt`, `modem.b00`-`b04`, `b06`-`b08`, `b10`-`b14` (`b05` and
  `b09` do not exist: those segments have no file data)
- `modem_pr/` (239 entries, the MCFG tree: `mcfg/configs/mcfg_hw/...`),
  which the modem reads over QRTR TFTP as `/readonly/firmware/modem_pr/`;
  tqftpserv looks for it next to the modem's firmware, `updates/` first

`gts3-firmware` copies these on every model (21 MB) but does not start the
modem: rmtfs does, once it serves the file system. Without an MSS node in
the DT (the main DT today) the files are just there.

## Userspace: geoclue through gnss-share

geoclue has two ways to get satellite positions: the `modem-gps` source,
through ModemManager, and the `nmea-socket` option of its NMEA source,
which reads NMEA from a Unix socket. ModemManager can't take the SM-T820's
modem: it offers QMI LOC but no WDS, DMS or NAS (see "Results"). So
something has to run the QMI LOC session and serve its NMEA to geoclue.

**Choice: gnss-share with a new `qmi_loc` driver** (`pmaports/gnss-share`,
patches 0002-0004). gnss-share is the postmarketOS daemon that already does
this for other phones: it owns the GNSS device, serves NMEA on
`/run/gnss-share.sock` to any number of clients, starts the device for the
first client and stops it after the last. Alpine packages it with a
`gnss-share-geoclue` subpackage that points geoclue's NMEA source at that
socket, and postmarketOS's systemd preset enables its service. A driver for
it is about 600 lines of Go plus small QRTR and QMI packages; the socket,
its permissions, the multi-client handling, the geoclue configuration and
the service come as they are.

Rejected:

- **A standalone daemon** (QMI LOC to NMEA on its own socket): it would
  repeat all of the above, and clash with gnss-share over
  `/run/gnss-share.sock` and geoclue's `nmea-socket` setting as soon as
  gnss-share is installed.
- **A QMI LOC source inside geoclue**: C in geoclue's core for one family
  of devices; geoclue already has the generic NMEA path.
- **A kernel GNSS driver** (QMI LOC client in the kernel, `/dev/gnss0`): a
  much larger upstream job, and userspace would still need something to
  feed geoclue.
- **gpsd**: its QRTR patch (2020) never landed, and geoclue does not read
  gpsd.

**Not upstream.** The patches are written to be upstreamable (against
gnss-share 0.8.2, with tests), but postmarketOS does not accept
contributions made with generative AI tools, so they are carried here.
`pmaports/gnss-share` is Alpine's APKBUILD with version `99990.8.2`, so
that it wins over Alpine's `0.8.2`. Patch 0001 fixes an upstream bug the
tests found: two clients that connected at the same time both saw two
subscribers, and neither started the device.

### What the `qmi_loc` driver does

- **One QRTR socket per session.** The first client opens it, the last one
  closing ends it. The location service (16, version 2) is found through
  the kernel's QRTR name service, not at a fixed port. The lookup stays
  open, so when the modem restarts (the service disappears and comes back)
  the driver sets the session up again by itself.
- **Session setup** (as ModemManager's `mm-shared-qmi.c`): REG_EVENTS with
  NMEA, time requests, engine state and fix session state (mask 0x194),
  SET_OPERATION_MODE standalone (there is no A-GPS server), START (session
  1, periodic, intermediate reports, every 1000 ms). A request that fails
  or gets no answer in 5 s is retried after 30 s.
- **Time injection** (INJECT_UTC_TIME) right after START and whenever the
  engine asks for the time, but only while the kernel considers the clock
  synchronized: maximum error estimate below 16 s, the same test as
  systemd's `NTPSynchronized`. The uncertainty sent is that estimate, at
  least 500 ms. Without a synchronized clock the engine works out the time
  from the satellites, which is slower.
- **Stopping:** STOP when the last client leaves, then the socket closes.
  If gnss-share dies, closing its socket makes the kernel send DEL_CLIENT to
  the modem, which ends the session there too (step 5: engine off within
  8 s).
- NMEA is never logged; `-d` only adds debug messages about the session.

Message IDs, TLVs and enum values come from libqmi's
`data/qmi-service-loc.json`; the QRTR control packets from Linux's
`include/uapi/linux/qrtr.h` and `net/qrtr/ns.c`.

### Packaging: what runs when

The subpackage `device-samsung-gts3lwifi-gnss` comes with the device
package, and depends on geoclue, gnss-share (ours), rmtfs and tqftpserv.
It installs
`/etc/gnss-share-qmi-loc.conf` (driver `qmi_loc`, socket
`/run/gnss-share.sock`, group `geoclue`), systemd drop-ins for rmtfs,
tqftpserv and gnss-share, and `90-gts3-gnss.rules`.

- **A DT without the MSS** (the SM-T825's, and kernel packages from before
  the MSS moved into the main DT): there is no `2080000.remoteproc` platform
  device, so rmtfs, tqftpserv and gnss-share all skip on their `Condition*`
  lines, and `gts3-firmware` doesn't copy the modem's images. Nothing runs
  and nothing needs masking. (The rmtfs package's own udev rule still tries
  to start rmtfs when `/dev/qcom_rmtfs_mem1` appears; the condition stops
  it.)
- **The main DT (MSS enabled):** when `qcom_q6v5_mss` registers the
  modem's remoteproc, `90-gts3-gnss.rules` starts rmtfs, which pulls in
  tqftpserv and, with `-s`, boots the modem. The package's unit runs
  `rmtfs -r -P -s`: the modem's writes stay in RAM. The modem then stays
  up, idle, for as long as rmtfs runs. gnss-share starts at boot and waits.
  Stock does the same: the modem boots at startup and stops at shutdown
  (see "Evidence"). Its firmware takes 9 MB of an SM-T820's rootfs, and
  comparing it adds 1.8 s to `gts3-firmware` at each boot.
- **GNSS sessions run only on demand:** geoclue connects to the socket
  only while an app asks for exact accuracy (level 8, e.g. a map's "my
  location"). Requests for a coarser accuracy don't start the GNSS
  engine. When the last such app stops, geoclue disconnects and gnss-share
  stops the session. geoclue's Wi-Fi and IP sources are a separate matter
  and do go online (see "Results").
- **OpenRC** is not wired up: the subpackage only carries systemd units
  and drop-ins.

geoclue asks an agent before it gives an app a location. Plasma has no
agent of its own; geoclue's demo agent starts with the desktop session
(`/etc/xdg/autostart/geoclue-demo-agent.desktop`). Test as the desktop
user (from the session, or over SSH as that user); as root, `where-am-i`
waits for an agent that never answers.

### Test tools

- `tools/gnss-test.sh`: `share` runs gnss-share with the `qmi_loc` driver
  in the foreground; `fix` and `nmea` read its socket (`RAW=1`: run the
  session with `qmi-loc.py` instead); `watch` reports the engine turning on
  and off. `fix` prints satellites, signal and fix state but never the
  position; `nmea` masks latitude, longitude, altitude and satellite
  positions and drops proprietary sentences.
- `tools/qmi-loc.py`: a QMI LOC client in Python (one socket, name
  service lookup), `watch` and `nmea`.
- `tests/gnss/run.sh`: runs both tools and gnss-share on a PC against
  `tests/gnss/fake-loc.py`, a fake location service on the PC's own QRTR
  node (`modprobe qrtr`; it refuses to run where a real one exists). It
  sends synthetic NMEA at 0 N 0 E. Covers sessions, the summary and
  masking, the engine watcher, modem restarts, two gnss-share clients, and
  the session stopping with the last client.

## Hardware test plan

Helpers: `tools/gnss-test.sh` and `tools/qmi-loc.py`, copied to one
directory on the tablet (say `/root/gnss/`), run as root. Every step is
reversible; nothing writes to a partition until a later, separate decision
to let rmtfs write. `fix` never prints the position and `nmea` masks it:
don't paste unmasked NMEA or `where-am-i` output anywhere.

Steps 0-8 were done on 2026-11-01 (see "Results"; 0-3 with the first
version of the tools); repeat them as needed. Steps 9 and 10 are open.

**0. Pre-checks (current install, no reboot).**
`apk add rmtfs qrtr tqftpserv socat python3`, then `systemctl mask rmtfs`:
its udev rule would start it, and boot the modem, as soon as a DT with the
MSS is booted; until step 7 the tests start it by hand. Install the device
package from this branch (or copy `gts3-firmware`) and run `systemctl start
gts3-firmware`; don't install the `-gnss` subpackage yet. Pass: `ls
/lib/firmware/updates/qcom/msm8996/samsung/gts3/` shows `mba.mbn`,
`modem.mdt`, `modem.b*` and `modem_pr/`; `/dev/qcom_rmtfs_mem1` exists;
`gnss-test.sh status` shows the MSS node `disabled` (a kernel package
built before the MSS moved into the main DT).

**1. Back up the modem's file system.** `gnss-test.sh backup` (reads
`modemst1`, `modemst2`, `fsg`, `fsc` with `dd`, writes `SHA256SUMS`).
Copy the directory off the tablet. Pass: four images, 2 MiB, 2 MiB,
2 MiB and 4 KiB. These hold the modem's NV items (calibration among them);
`fsg` is the factory copy. Do not go on without this backup.

**2. RAM-boot the main DT with the MSS.** On the host: `BOOT=<copy of the
tablet's /boot/boot.img> MKBOOTIMG=/usr/bin/mkbootimg
scripts/make-test-image.sh dts/apq8096-samsung-gts3lwifi.dts gnss.img`, get to lk2nd's fastboot
(docs/lk2nd.md), `fastboot boot gnss.img`. Nothing is written; the next
normal boot is the installed system. Pass:
- `gnss-test.sh status`: MSS node okay, `remoteproc` of `qcom-q6v5-mss`
  `offline`, rmtfs not running.
- No regressions from the moved regions (the checklist of step 10).

**3. Start the modem.** `gnss-test.sh up` (turns the MSS's recovery and
core dump off, starts tqftpserv and `rmtfs -r -P -s -v`). Pass:
- dmesg: `remote processor 2080000.remoteproc is now up` (MBA loaded,
  image authenticated);
- `/var/log/gnss-rmtfs.log` opens `modem_fs1`, `modem_fs2`, `modem_fsg`
  and `modem_fsc`;
- `qrtr-lookup` lists service 16 (location) on the modem's node.

Failures and what they mean: "segment outside memory range" (span; check
`modem.mdt` against `mpss_mem`); "MBA boot timed out", "MBA returned
unexpected status", "MPSS (header) authentication failed" (the MBA or
TrustZone rejects the image or the layout); "assigning Q6 access to mpss
memory failed" (SCM); `fatal error received: <reason>` (from the modem;
note the reason, then `gnss-test.sh down`); up but no service 16 within a
minute (tqftpserv did not find `modem_pr`, or pd-mapper: check its log and
that `qcom_pd_mapper` is loaded).

**4. The bridge by hand, at a window or outdoors.** Build gnss-share for
the tablet on a PC: gnss-share 0.8.2 with `pmaports/gnss-share/*.patch`
applied (`git am` or `patch -p1`), then `CGO_ENABLED=0 GOOS=linux
GOARCH=arm64 go build -trimpath -o gnss-share ./cmd/gnss-share`; copy it
to `/root/gnss/`. Three root shells, all in `/root/gnss/`:

- A: `./gnss-test.sh watch 3600` (prints `engine on` / `engine off`)
- B: `GNSS_SHARE=/root/gnss/gnss-share ./gnss-test.sh share` (gnss-share
  in the foreground, with debug messages)
- C, once A and B run: `./gnss-test.sh fix 900`

Pass:
- Before C: no `engine on` in A; B prints only `Starting GNSS server`.
  No client, no session.
- When C starts: B prints `Client connected`, `Starting device`,
  `qmi_loc: location service on QRTR node 0 port N, starting the
  session`, `qmi_loc: session started`, then `qmi_loc: injected the system
  time, uncertainty N ms` if the clock is synchronized (`gnss-test.sh
  status`, line `clock`; otherwise `system clock not synchronized`), and
  `qmi_loc: engine on`. A prints `engine on`.
- C prints a summary every 15 s: satellites and C/N0 within a few minutes,
  then `first fix` (2.5-8 min after the modem starts; seconds while it
  still has recent orbit data).
- C ends: B prints `Client disconnected`, `Stopping device`,
  `qmi_loc: session stopped`; A prints `engine off` within a few seconds.
- `./gnss-test.sh nmea 30` shows sentences with the position fields
  masked (`x`).

If the engine runs but no NMEA comes, compare `RAW=1 ./gnss-test.sh fix
300` (`qmi-loc.py` with gnss-share's event mask, 0x194) with `RAW=1
LOC_ARGS="--events 0x3ff7" ./gnss-test.sh fix 300` (the mask of the first
fix). If only the second works, the modem wants another event registered
before it sends NMEA: find which by bisecting the mask, and add it to the
driver's `qmiLocEvents`.

**5. The session ends with its client.** With A and B as in step 4:
- Two clients: `./gnss-test.sh fix 120` in C and `./gnss-test.sh nmea 60`
  in a fourth shell. Pass: one `Starting device`; the session keeps
  running when the shorter one ends and stops when the longer one ends.
- gnss-share killed: start `./gnss-test.sh fix 600` in C, wait for
  `engine on`, then `pkill -9 -x gnss-share`. Pass: A prints `engine off`
  within a few seconds (the kernel told the modem that the client is gone).
  If the engine stays on, the modem keeps sessions of dead clients alive:
  note how long, then `gnss-test.sh down`. Restart B afterwards.

**6. The modem restarts during a session.** With A, B and `./gnss-test.sh
fix 900` in C running and a fix: `./gnss-test.sh down`, wait 10 s,
`./gnss-test.sh up`. Pass: B prints `qmi_loc: location service gone
(modem stopped?), waiting for it to come back`, then `location service on
QRTR node 0 port N, starting the session` and `session started` again
without being touched; C's summary shows satellites and a fix again (A's
watcher stops with the modem: restart it). rmtfs `-r` keeps nothing
across a modem restart, so the second fix may take as long as the first.

**7. Packages, then geoclue.** Build the packages from this branch
(`pmaports/gnss-share` and `pmaports/device-samsung-gts3lwifi` in
pmbootstrap's pmaports; `pmbootstrap build gnss-share
device-samsung-gts3lwifi`) and install `gnss-share`,
`gnss-share-geoclue`, `gnss-share-systemd`, `device-samsung-gts3lwifi`
and `device-samsung-gts3lwifi-gnss` (`pmbootstrap sideload`, or copy the
`.apk` files and `apk add --allow-untrusted`). `systemctl unmask rmtfs`,
`gnss-test.sh down`, then RAM-boot step 2's image again. Pass:
- Without anyone starting anything: `systemctl status rmtfs tqftpserv
  gnss-share` all active; dmesg shows the modem up; `gnss-test.sh status`
  shows service 16 and the socket `/run/gnss-share.sock`.
- No session: `./gnss-test.sh watch 120` prints no `engine on`.
- geoclue, as the desktop user (not root): `/usr/libexec/geoclue-2.0/demos/where-am-i
  -a 8 -t 900 | grep -E 'Accuracy|Timestamp'`. The demo agent asks for
  permission on screen; allow it. Pass: `journalctl -u gnss-share` shows
  `Client connected` and `session started`; after a fix, where-am-i
  reports a location. To be sure it came from GPS, and to keep the test
  offline, use the GPS-only drop-in (see "Results"). Where-am-i ending (or
  Ctrl+C) ends the
  session: `Client disconnected`, `session stopped`, and a watcher sees
  `engine off`.
- A coarse request starts no session: `where-am-i -a 4 -t 60` (city
  level) leaves gnss-share without a client.

**8. A boot without the MSS, with the packages installed.** Boot the
installed system while its kernel package still has the old DT (no MSS),
or step 2's image built from the DT with the `&mss_pil` status set to
`disabled`. Pass: `systemctl status rmtfs tqftpserv
gnss-share` show all three inactive (`start condition unmet` where systemd
tried them), no rmtfs process, no modem in dmesg, and nothing else changed
(sound, sensors, Wi-Fi).

**9. Idle and sleep** (the "Don't break sleep" rule; TODO in the pmOS
notes). The modem boots at startup and stays up, because stock does (see
"Evidence"), so there is no on-demand start to decide on. Once suspend
works, check that the idle modem and gnss-share don't wake it
(`/sys/power/suspend_stats`, wakeup sources) and that a session running at
suspend comes back after resume.

**10. Regression checklist for the moved regions**, on step 2's image
before the modem starts, and again with the packages (the modem up):
- ADSP: `remoteproc` of the ADSP `running`; headphones and speakers play,
  the microphone records (`docs/audio.md`).
- SLPI: `remoteproc` of the SLPI `running`; the IIO sensors report and
  auto-rotate works (`docs/sensors.md`).
- GPU: the zap shader loads (no `zap` or `a5xx` error in dmesg), Plasma
  renders with the GPU (`docs/gpu.md`).
- Venus: a decode works (`tests/venus-dectest.sh`, `docs/venus.md`).
- Modem: steps 3 and 7; with the packages installed it starts by itself.
- First boot: with the modem's files moved out of
  `/lib/firmware/updates/qcom/msm8996/samsung/gts3/`, `gts3-firmware` copies
  them and the modem comes up on that same boot.
- SM-T825: its DT includes the main DT, so it gets the same regions (the
  stock layout of both models), with the MSS disabled. Check that it boots
  and that audio, sensors, GPU and Venus still work.

**11. Later.** Let rmtfs write (drop `-r` in a drop-in) only once
sessions work, with the backup from step 1 at hand; the modem would then
keep its orbit data and settings across restarts.

SM-T825: steps 0-7 with `msm8996-samsung-gts3llte.dts`, after a backup of its
partitions and a dump of its `modem` partition (to check the image span
first). On that model ModemManager may take the modem (it offers WDS, DMS
and NAS there); then ModemManager and gnss-share would both run location
sessions. Sort that out before removing the `&mss_pil` override from its
DT; with the override the subpackage does nothing there.

## Risks and safety

- **The modem's NV partitions.** rmtfs `-r` never writes them; the backup
  in step 1 is the way back if a later writable run corrupts them. `fsg`
  is the factory copy; the modem normally falls back to it when both
  `modemst` copies are bad.
- **TrustZone register 0x007b3000** (TCSR download mode, never to be
  read): the MSS does not touch it (its halt registers are in TCSR 1 at
  0x763000-0x765000). But msm8996.dtsi gives `scm` `qcom,dload-mode =
  <&tcsr_2 0x13000>`, and `qcom_scm` in this kernel reads and writes that
  register through TrustZone (`qcom_scm_io_rmw()`, an SCM IO read then
  write) at every probe and at every shutdown, with or without the MSS. Never set `qcom_scm.download_mode`.
- **Moved firmware regions** (ADSP, SLPI, zap, Venus): in the main DT
  now; the same layout worked RAM-booted as a test DT on 2026-11-01, and
  step 10's checklist is to be run on a main DT build.
- **A modem crash** only stops the remoteproc (recovery is off during the
  test); the rest of the system keeps running. A hang that needs a reset:
  Power + Volume Down.
- **Memory protection.** The modem's memory is given to it through SCM
  (as the working rmtfs memory already is); a wrong layout fails at load
  time, not later.

## Open items

- **Idle cost of the modem.** It boots at startup and stays up idle, as on
  stock. It doesn't wake a suspend, but its battery cost on mainline is
  unmeasured (step 9).
- **Time injection** depends on systemd-timesyncd having synchronized the
  clock (network at boot). Without it no time is injected and the first
  fix takes longer; a wrong RTC with no network is not injected either,
  which is the point of the check.
- **No assistance data.** Predicted orbits (XTRA) and a coarse position
  are not injected; the engine asks for both (0x29, 0x2a). The first fix
  after a modem start took 141 s to about 500 s. With rmtfs `-r` nothing
  the modem learns
  survives a modem restart or reboot (step 11).
- **Memshare.** Stock reserves a 96 MiB pool for it on both models and the
  modem image has a memshare client. A fix came without it; if sessions
  later fail or the modem crashes when a session starts, carry the
  memshare driver (Nikita Travkin's 2021 series, carried by
  msm8916-mainline) as kernel patches: it logs a request it does not know
  with the client id and size, which tells us what to describe in the DT.
- **Engine lock.** Never needed so far; if a session starts but the
  engine stays off, check it with `qmicli -d qrtr://0
  --loc-get-engine-lock` (libqmi) and set it to `none`.
- **OpenRC** is not wired up for GNSS (systemd only).
- **gnss-share upstream.** Patches 0001-0004 are not sent: postmarketOS
  does not accept contributions made with generative AI tools. 0001 is an
  upstream bug fix that someone could redo independently.
- **SM-T825**: modem image span and file names unknown; it probably has
  WDS/DMS/NAS, so ModemManager may take the modem and run its own location
  session alongside gnss-share; Samsung's RIL is a different matter.
- tqftpserv 1.3 (Alpine) against the `updates/` lookup of current git; if
  the modem does not get `modem_pr`, link it into
  `/lib/firmware/qcom/msm8996/samsung/gts3/` (on 2026-11-01 the modem
  asked tqftpserv only for `/readwrite/server_check.txt`).
