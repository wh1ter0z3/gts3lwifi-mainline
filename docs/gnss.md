# GNSS (GPS)

## Status (2026-11-01): GNSS works on the SM-T820 on mainline (test DT); geoclue bridge not written yet

| Model | Where the GNSS engine is | Stock | Mainline |
|---|---|---|---|
| SM-T820 (Wi-Fi, APQ8096) | modem DSP (MSS), which runs a GNSS-only modem image ("modemlite") | works: satellites and position reports at 1 Hz in LineageOS | **works** with `dts/test/test-gnss.dts`: modem up, QMI LOC fix in about 6 min from a cold start (see "Results") |
| SM-T825 (LTE, MSM8996) | modem DSP, in the full LTE modem image | should work the same way (same location HAL) | `dts/test/test-gnss-lte.dts`, untested; we have no dump of its modem partition |

"APQ8096 has no modem" is only true of the cellular part. The modem
subsystem (MSS, a Hexagon QDSP6 v5.5) is there on the SM-T820 too, and
Samsung loads a modem image into it that has only GNSS and the services
that GNSS needs. There is no separate GNSS chip on either model.

On mainline the chain is: `qcom_q6v5_mss` boots the modem DSP, `rmtfs`
serves it its file system (the `modemst1`, `modemst2`, `fsg` and `fsc`
partitions), `tqftpserv` serves its configuration files, and the location
service (QMI LOC, QRTR service 16) is reached over QRTR. Every part of
this chain is upstream and in the kernel package's config; the first test
needs no kernel patch and no config change. What the test DT adds is the
memory layout that the modem image needs (below).

## Results (SM-T820, 2026-11-01)

Kernel 6.19.5 from the installed pmOS package, RAM-booted with
`dts/test/test-gnss.dts` from lk2nd's fastboot. Nothing in the kernel was
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
  out" for every follow and stop call), so `gnss-test.sh fix` doesn't work.
  A single process that keeps one QRTR socket does. The test used a small
  Python client: REG_EVENTS (0x21), SET_OPERATION_MODE (0x4a, standalone),
  START (0x22, periodic 1 s, intermediate reports on) and INJECT_UTC_TIME
  (0x38) from the NTP-synced clock, all on one socket.
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

Next: a geoclue source (QMI LOC to NMEA, e.g. feeding geoclue's
`nmea-socket`), injecting time at session start; then idle/sleep checks
(the modem stays up while rmtfs runs); then MSS into the main DT with the
moved regions, after a normal-boot regression pass.

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
| `rmtfs`, `tqftpserv`, `qrtr`, `qmi-utils` | Alpine/pmOS packages | not pulled in by the device package |
| MSS node | msm8996.dtsi, disabled | enabled in the test DTs only |
| Memory layout | msm8996.dtsi `mpss_mem` is 0x6200000 | too small for the modem image, the test DT moves the regions |
| Memshare (QMI service 0x34) | no; Nikita Travkin's series of 2021-03-19 was not merged | maybe needed, see open questions |
| QMI LOC in the kernel (GNSS subsystem) | no driver | not needed to test |
| geoclue | `modem-gps` (through ModemManager) and `nmea-socket` sources | SM-T820 needs a bridge (below) |

No kernel config change is needed (`CONFIG_GNSS` stays off; nothing uses
it). `qcom_q6v5_mss` relocates the image into `mpss_mem` and refuses a
segment that ends past it ("segment outside memory range").

## Memory layout (`dts/test/gnss.dtsi`)

The SM-T820 modem image spans 0x6c00000 bytes from its lowest segment to
the end of its last (25 program headers, relocatable; segments 9 and 24
are large zero-filled ones), which is exactly the stock `modem_mem`. The
test DT grows `mpss_mem` to that and moves the regions after it to where
the stock kernel puts them (all of those images are relocatable):

| Region | Main DT | Test DT (= stock SM-T820) |
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

## Userspace

Packages: `apk add rmtfs qrtr qmi-utils tqftpserv`. postmarketOS's
systemd preset enables `rmtfs`, `tqftpserv`, `qrtr-ns` and ModemManager
when they are installed; the in-kernel QRTR name service makes `qrtr-ns`
unnecessary. They are not device package dependencies yet: with the main
DT there is no modem for rmtfs to start.

- **rmtfs**: `-P` finds the partitions by label, `-s` starts the modem
  once its service is up, `-r` keeps the modem's writes in RAM (the test
  uses it, the partitions stay untouched), `-v` logs every request.
- **qmicli** (libqmi 1.38): `--loc-start`, `--loc-follow-position-report`,
  `--loc-follow-gnss-sv-info`, `--loc-follow-nmea`,
  `--loc-get/set-engine-lock`, one action per call. Its QRTR client IDs
  belong to the process, so `tools/gnss-test.sh` goes through `qmi-proxy`
  (`-p`) and keeps the client between calls.
- **geoclue**: on the SM-T825 ModemManager should pick the modem up over
  QRTR (it needs WDS, NAS and DMS on the modem's node) and provide
  location to geoclue's `modem-gps` source. The SM-T820's GNSS-only image
  very likely has no WDS/NAS, so ModemManager will not take it; geoclue
  then needs a small bridge from QMI LOC to its `nmea-socket` source (the
  way `gnss-share` serves NMEA to geoclue for other devices; gnss-share
  0.8 has no QMI LOC driver). Not written yet: first see what the modem
  offers (test step 3).

## Hardware test plan

Helper: `tools/gnss-test.sh` (copy it to the tablet, run it as root). Every
step is reversible; nothing writes to a partition until a later,
separate decision to let rmtfs write.

**0. Pre-checks (current install, no reboot).**
`apk add rmtfs qrtr qmi-utils tqftpserv`, then `systemctl disable --now
rmtfs` (the preset enables it; the test starts it by hand). Install the
device package from this branch (or copy `gts3-firmware`) and run
`systemctl start gts3-firmware`. Pass: `ls
/lib/firmware/updates/qcom/msm8996/samsung/gts3/` shows `mba.mbn`,
`modem.mdt`, `modem.b*` and `modem_pr/`; `/dev/qcom_rmtfs_mem1` exists;
`gnss-test.sh status` shows the MSS node `disabled` (main DT).

**1. Back up the modem's file system.** `gnss-test.sh backup` (reads
`modemst1`, `modemst2`, `fsg`, `fsc` with `dd`, writes `SHA256SUMS`).
Copy the directory off the tablet. Pass: four images, 2 MiB, 2 MiB,
2 MiB and 4 KiB. These hold the modem's NV items (calibration among them);
`fsg` is the factory copy. Do not go on without this backup.

**2. RAM-boot the test DT.** On the host: `BOOT=<copy of the tablet's
/boot/boot.img> MKBOOTIMG=/usr/bin/mkbootimg scripts/make-test-image.sh
dts/test/test-gnss.dts gnss.img`, get to lk2nd's fastboot
(docs/lk2nd.md), `fastboot boot gnss.img`. Nothing is written; the next
normal boot is the installed system. Pass:
- `gnss-test.sh status`: MSS node okay, `remoteproc` of `qcom-q6v5-mss`
  `offline`, rmtfs not running.
- No regressions from the moved regions: sound plays (ADSP), the sensors
  report (SLPI; auto-rotate), the GPU renders (zap shader), a Venus decode
  works (`docs/venus.md`). `dmesg | grep -i -E 'remoteproc|fail'` shows
  ADSP and SLPI up.

**3. Start the modem.** `gnss-test.sh up` (turns the MSS's recovery and
core dump off, starts tqftpserv and `rmtfs -r -P -s -v`). Pass:
- dmesg: `remote processor 2080000.remoteproc is now up` (MBA loaded,
  image authenticated);
- `/var/log/gnss-rmtfs.log` opens `modem_fs1`, `modem_fs2`, `modem_fsg`
  and `modem_fsc`;
- `qrtr-lookup` lists service 16 (location) on the modem's node; note
  every other service it lists (WDS 1, DMS 2, NAS 3 decide whether
  ModemManager can take this modem).

Failures and what they mean: "segment outside memory range" (span; check
`modem.mdt` against `mpss_mem`); "MBA boot timed out", "MBA returned
unexpected status", "MPSS (header) authentication failed" (the MBA or
TrustZone rejects the image or the layout); "assigning Q6 access to mpss
memory failed" (SCM); `fatal error received: <reason>` (from the modem;
note the reason, then `gnss-test.sh down`); up but no service 16 within a minute (tqftpserv did not find
`modem_pr`, or pd-mapper: check its log and `qcom_pd_mapper` is loaded).

**4. A GNSS session, outdoors or at a window.**
`qmicli -p -d qrtr://<node> --loc-get-engine-lock`; if it is not `none`,
`--loc-set-engine-lock=none` (with rmtfs `-r` this does not persist).
Then `gnss-test.sh fix 900`. Pass: SV info with C/N0 values within a few
minutes (the antenna works and the engine runs), then position reports
with a plausible position and accuracy. Then `gnss-test.sh nmea 120`.
If the session starts but no satellite ever shows up, or the modem logs a
memshare error or crashes when the session starts, memshare is the first
suspect (open questions).

**5. Idle and sleep.** `gnss-test.sh down`, then compare the screen-off
idle drain with the modem up and idle against the modem off (fuel gauge,
unplugged); once suspend works, check that the modem and rmtfs do not wake
it (`suspend_stats`, wakeup sources). See the TODO in the pmOS notes.

**6. Later.** Let rmtfs write (drop `-r`) only once a session works,
with the backup from step 1 at hand. Then the device package can depend
on the userspace and the MSS can go into the main DT.

SM-T825: the same steps with `test-gnss-lte.dts`, after a backup of its
partitions and a dump of its `modem` partition (to check the image span
first).

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
  write) at every probe and at every shutdown, in the main DT as well as
  in these test DTs. Never set `qcom_scm.download_mode`.
- **Moved firmware regions** (ADSP, SLPI, zap, Venus): only in the
  RAM-booted test DT; checked in step 2 before the modem starts.
- **A modem crash** only stops the remoteproc (recovery is off during the
  test); the rest of the system keeps running. A hang that needs a reset:
  Power + Volume Down.
- **Memory protection.** The modem's memory is given to it through SCM
  (as the working rmtfs memory already is); a wrong layout fails at load
  time, not later.

## Open questions

- **Memshare.** Stock reserves a 96 MiB pool for it on both models and the
  modem image has a memshare client. On msm8916, GNSS gets no fix without
  the 2 MB memshare gives it; on this image we do not know whether it asks
  at all. If step 4 points there, carry the memshare driver (Nikita
  Travkin's 2021 series, carried by msm8916-mainline) as kernel patches
  0124 and up: it logs a request it does not know with the client id and
  size, which tells us what to describe in the DT.
- **Which QMI services the modem offers** (step 3): decides whether
  ModemManager can serve geoclue or a QMI LOC bridge is needed.
- **The antenna path** on the SM-T820 (no RF data, only stock logs that
  show reports at 1 Hz but not the fix quality).
- **SM-T825**: modem image span and file names unknown; ModemManager
  support over QRTR untested; Samsung's RIL is a different matter.
- tqftpserv 1.3 (Alpine) against the `updates/` lookup of current git; if
  the modem does not get `modem_pr`, link it into
  `/lib/firmware/qcom/msm8996/samsung/gts3/`.
