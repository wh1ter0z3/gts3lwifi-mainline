# LTE modem (SM-T825)

Status: **work in progress, untested.** The SM-T825's device tree enables
the modem DSP (MSS) since 2026-11-06; nobody has booted it on an SM-T825
yet. GNSS on that model goes through the same modem (`docs/gnss.md`).

## What we know (stock firmware T825XXU3CTD1)

- **Modem image** (CP T825XXU3CTA1, `MPSS.TH.2.0.1-00316-M8996FAAAANAZM-1`):
  `modem` partition (FAT), `image/mba.mbn`, `image/modem.mdt` +
  `modem.b00`-`b20`, `image/modem_pr/` (carrier configurations for every
  region), and the ADSP. A full LTE/WCDMA build. Its loadable segments span
  0x88800000-0x8f400000 (0x6c00000 bytes): the same address and span as
  the SM-T820's GNSS-only image, so the main DT's `mpss_mem` fits.
  `gts3-firmware` copies the tablet's own images at boot, as on the
  SM-T820, so the SM-T825 runs its own modem firmware.
- **Stock DTs** (Rev00, 02, 04, 05; msm-id 246, board-id 8/0-5): modem,
  peripheral and MBA memory as on the SM-T820 (`modem_region@88800000`,
  `peripheral_region` at 0x8f400000). `qcom,mss@2080000` matches
  msm8996.dtsi's MSS.
- **Memshare:** stock lends the modem `modem_shared_mem_region`
  (0x93000000, 96 MiB) through `qcom,memshare` (three clients, one of
  3 MiB). Mainline has no memshare driver; our LTE DT keeps the region
  reserved. If the modem asks for memshare memory and doesn't get it, some
  modem features may fail; GNSS on the SM-T820 works without it.
- **Data path:** stock routes mobile data through IPA (`qcom,ipa@680000`,
  `qcom,ipa-hw-ver = <5>`: IPA v2.5) and `rmnet_ipa`. Mainline's IPA driver
  (`drivers/net/ipa`, 7.3) starts at IPA v3.0; IPA v2.x support was posted
  as an RFC in 2021 (Sireesh Kodali, "net: ipa: Add support for IPA v2.x",
  tested on MSM8996 up to getting an address) and never merged.
- **Userspace on stock:** Samsung's RIL (`libsec-ril` over
  `/dev/umts_ipc0`) and `netmgrd`. Mainline talks QMI over QRTR instead
  (libqmi, ModemManager).

## What to expect on mainline

| Piece | Expectation |
|---|---|
| Modem boots (remoteproc `running`) | should work: same path as the SM-T820's GNSS modem |
| GNSS | should work as on the SM-T820 |
| SIM, IMEI, network registration, signal | likely, over QMI (DMS, UIM, NAS) through QRTR |
| SMS | likely, QMI WMS through ModemManager |
| Mobile data | **no**: needs an IPA v2.5 driver (the 2021 RFC ported to 7.3) |
| Voice calls | no: no voice audio path between the modem and the ADSP |

## Test on an SM-T825

With a kernel whose LTE DT enables the MSS and `device-samsung-gts3lwifi-gnss`
installed (rmtfs, tqftpserv). rmtfs runs read-only (`-r`), so the modem
can't write its EFS partitions (`modemst1`/`modemst2`, which hold the
IMEI and calibration): back up `modemst1`, `modemst2`, `fsg`, `fsc` and
`efs` first anyway.

1. Boot, then the modem's state and the firmware it got:
   `cat /sys/class/remoteproc/*/name /sys/class/remoteproc/*/state`,
   `ls /lib/firmware/updates/qcom/msm8996/samsung/gts3/`,
   `dmesg | grep -iE 'remoteproc|mss|q6v5|qrtr'`.
2. Services on the modem's QRTR node: `qrtr-lookup` (expect DMS 2, NAS 3,
   WDS 1, UIM 11, WMS 5, LOC 16 on node 0).
3. QMI by hand (libqmi): `qmicli -d qrtr://0 --dms-get-ids` (IMEI),
   `--uim-get-card-status` (SIM), `--nas-get-serving-system`,
   `--nas-get-signal-strength`.
4. ModemManager: `apk add modemmanager`, start it, then `mmcli -L` and
   `mmcli -m any` (state, operator, signal); unlock a SIM PIN with
   `mmcli -i any --pin=XXXX`. SMS: `mmcli -m any --messaging-list-sms`.
5. GNSS: `docs/gnss.md` step 7, once without and once with ModemManager
   running (both may run location sessions on the modem).
6. A regression pass: audio, sensors, GPU, Venus, Wi-Fi, suspend.

A modem crash shows in `dmesg` as a remoteproc crash and recovery; it
should not reset the tablet. A reset about 9 s into boot is TrustZone
(see the TLMM note in `msm8996-samsung-gts3llte.dts`).

## Next

- Results of the test above.
- Mobile data: port the IPA v2.x series (BAM transport, IPA v2.5 data)
  to 7.3; large, and only worth it once registration works.
- Memshare, if the modem turns out to need it.
