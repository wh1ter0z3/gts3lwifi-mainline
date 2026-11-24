# Kernel patches: what could go upstream (2026-11-24)

The patches of `pmaports/linux-postmarketos-qcom-msm8996`, sorted. `m001`-`m071` come from the MSM8996 community's tree (postmarketOS's msm8996 kernel; other boards, SMBCHG charger, haptics, sensor manager) and are listed only at the end. `0001`-`0236` are ours. A sort by subject and files, not a review: each patch still needs checking against current mainline before it is sent.

## A. Generic MSM8996 / Qualcomm fixes in upstream drivers (79)

Bugs and missing features in drivers every MSM8996 (and often MSM8998/SDM660) device uses. The first candidates for linux-arm-msm and the subsystem lists.

### Display (MDP5, DPU, DSI, MSM GEM, DRM core) (13)

- `0003` drm/msm/mdp5: add split display support for bonded
- `0004` drm/msm/dpu: msm8996: mark CTL_0 and CTL_2 as split display
- `0041` drm/msm/dsi: clear a stale clock lane HS request when
- `0050` drm/mipi-dsi: add a flag for a clock lane started after the
- `0051` drm/msm/dsi: support MIPI_DSI_CLOCK_DEFERRED_HS
- `0053` drm/msm/mdp5: don't wait for PP_DONE on a disabled
- `0068` drm/msm/mdp5: no async plane updates on command-mode CRTCs
- `0122` drm/msm: factor out a locking put_iova_spaces() wrapper
- `0123` drm/msm: don't tear down shared VM mappings on handle close
- `0148` drm/msm/mdp5: Assign pipes again to a plane state restored on
- `0186` drm/msm/mdp5: Drop the bandwidth vote while runtime suspended
- `0187` drm/msm: Enable THP for GEM buffers
- `0204` drm: Accept detached ops in the crtc fence's name callbacks

### Video (Venus) (21)

- `0008` media: venus: hfi_cmds: accept the H.264 8x8 transform
- `0009` media: venus: signal EOS on a real buffer with HFI v3
- `0099` media: venus: venc: Refuse new capture buffers while the
- `0100` media: venus: venc: Give the firmware timestamps at the frame
- `0101` media: venus: venc: Add RGBA input on HFI v3
- `0102` media: venus: venc: Use CAVLC with the Baseline profiles
- `0182` media: venus: vdec: Crop to the stream size when the firmware
- `0206` media: venus: venc: Take RGBA frames under the BGR32
- `0207` media: venus: venc: Bitstream buffers of the driver's
- `0208` media: venus: venc: Sizes that fit Android's frames and
- `0209` media: venus: Free bounce bitstream buffers only once the
- `0210` media: venus: venc: Wait for a drain to finish before a START
- `0211` media: venus: vdec: Bitstream dma-bufs as large as the client
- `0212` media: venus: vdec: Smallest placeholder for an
- `0213` media: venus: vdec: Parameter sets in front of the
- `0214` media: venus: vdec: Keep the session across dma-buf
- `0215` media: venus: vdec: Keep the visible rectangle across
- `0216` media: venus: vdec: Visible size from the H.264 SPS
- `0217` media: venus: Drop the delayed capture buffers with the
- `0218` media: venus: vdec: Take the capture S_FMT a resolution
- `0231` media: v4l2-common: A negative tv_usec counts as negative

### Cameras (CAMSS, CCI) (4)

- `0096` media: qcom: camss: ispif: Power all VFEs for the ISPIF reset
- `0097` media: qcom: camss: Stop the started subdevs when stream
- `0098` i2c: qcom-cci: Retry a message that timed out
- `0103` media: qcom: camss: vfe: Toggle the frame drop period once

### Audio (QDSP6, apq8096 machine, WCD9335 codec) (14)

- `0010` ASoC: qcom: apq8096: pin the back end format to S16_LE
- `0011` ASoC: qcom: q6asm-dai: reset queue_ptr when preparing a
- `0022` ASoC: qcom: q6afe: pass the TDM frame control settings to the
- `0023` ASoC: qcom: q6afe: send a TDM slot mapping only for a set
- `0026` ASoC: qcom: q6routing: let one stream feed several ports
- `0027` ASoC: qcom: apq8096: support primary and quaternary TDM back
- `0162` ASoC: codecs: wcd-mbhc-v2: Add plug type detection without an
- `0163` ASoC: codecs: wcd9335: Serialise micbias and MCLK requests
- `0164` ASoC: codecs: wcd9335: Keep the interrupt off across system
- `0165` ASoC: codecs: wcd9335: Add the jack interrupts to the
- `0166` ASoC: dt-bindings: qcom,wcd9335: Add the jack properties
- `0167` ASoC: codecs: wcd9335: Add headset detection
- `0168` ASoC: codecs: wcd9335: Record at the board's MIC BIAS2
- `0169` ASoC: qcom: apq8096: Add a headset jack

### Power, clocks, sleep, PCIe, I2C (9)

- `0018` cpuidle: psci-domain: accept OSI-only firmware on MSM8996
- `0037` clk: qcom: gcc-msm8996: keep PCIe GDSCs in retention during
- `0170` arm64: dts: qcom: msm8996: Add DDR bandwidth to the GPU OPPs
- `0183` irqchip/qcom-mpm: Program the vMPM wake-up timer
- `0184` clk: qcom: gcc-msm8996: No parent for the modem's GPLL0 vote
- `0185` clk: qcom: smd-rpm: Release unused handoff votes in the sleep
- `0220` clk: qcom: gcc-msm8996: 12.5 MHz for the fingerprint SPI
- `0221` i2c: qup: Suspend through runtime PM in the late phase
- `0224` PCI: qcom: Reset the MSM8996 controller each time it

### Storage encryption (ICE v2 on eMMC) (5)

- `0226` firmware: qcom: scm: The older ICE key call (ICE v2,
- `0227` soc: qcom: ice: ICE v2.1 with the older TrustZone key
- `0228` mmc: sdhci-msm: Inline encryption with ICE v2
- `0229` dt-bindings: crypto: qcom,inline-crypto-engine: Add
- `0230` arm64: dts: qcom: msm8996: The eMMC inline crypto

### Bluetooth (QCA ROME), QRTR (4)

- `0232` Bluetooth: hci_qca: Hold an optional vddpa supply for ROME
- `0233` Bluetooth: hci_qca: Start with both sides asleep after a
- `0234` Bluetooth: qca: Put the public address into ROME's NVM
- `0146` net: qrtr: Send a pending HELLO before other packets

### USB 3.0 reset (SoC device tree) (1)

- `0085` arm64: dts: qcom: msm8996: add the USB 3.0 controller reset

### Sensor manager (on top of the community's SMGR driver, m069) (8)

- `0076` iio: qcom_smgr: release the QMI handle through a void *
- `0077` iio: qcom_smgr: stream ambient light from a light-only
- `0140` iio: qcom_smgr: clear samples and give them a timestamp
- `0141` iio: qcom_smgr: push only the enabled axes
- `0142` iio: qcom_smgr: push every sample of a report
- `0143` iio: qcom_smgr: stream at the sampling frequency userspace
- `0144` iio: qcom_smgr: use the standard soft timestamp channel
- `0145` iio: qcom_smgr: give the gyroscope and magnetometer a

## B. Drivers for chips on the Tab S3 (76)

New drivers or new chip variants. Upstreamable as drivers with bindings; several chips are in other Samsung devices too (SM5705: Galaxy J/A series).

### Samsung AMS968HH01 panel (14)

- `0002` drm/panel: Add Samsung AMS968HH01 (ANA38401) dual-DSI panel
- `0005` drm/panel: samsung-ams968hh01: program the mDNIe block to
- `0038` drm/panel: samsung-ams968hh01: send the gamma table in
- `0039` drm/panel: samsung-ams968hh01: apply the panel's MTP gamma
- `0040` drm/panel: samsung-ams968hh01: use the 360 cd/m2 ELVSS value
- `0042` drm/panel: samsung-ams968hh01: add brightness control
- `0043` drm/panel: samsung-ams968hh01: turn on ACL
- `0052` drm/panel: samsung-ams968hh01: start the link clock after the
- `0054` drm/panel: samsung-ams968hh01: add the high brightness mode
- `0055` drm/panel: samsung-ams968hh01: report the panel orientation
- `0065` dt-bindings: display: panel: add Samsung AMS968HH01
- `0066` dt-bindings: display: panel: samsung,ams968hh01: add a
- `0067` drm/panel: samsung-ams968hh01: compensate ELVSS for the
- `0088` drm/panel: samsung-ams968hh01: keep HBM out of the backlight

### Silicon Mitus SM5705 (fuel gauge, charger, MUIC) (24)

- `0012` dt-bindings: power: supply: add Silicon Mitus SM5705 fuel
- `0013` power: supply: add Silicon Mitus SM5705 fuel gauge driver
- `0014` dt-bindings: power: supply: add Silicon Mitus SM5705 charger
- `0015` power: supply: add Silicon Mitus SM5705 charger driver
- `0016` dt-bindings: extcon: add Silicon Mitus SM5705 MUIC
- `0017` extcon: add Silicon Mitus SM5705 MUIC driver
- `0019` extcon: sm5705: opt-in 9 V negotiation with AFC and QC 2.0
- `0020` power: supply: sm5705_charger: input limit for 9 V fast
- `0021` power: supply: sm5705_charger: battery temperature guard
- `0028` power: supply: sm5705_charger: charge at 2.7 A from 15 W
- `0031` dt-bindings: power: supply: sm5705-charger: allow a Type-C
- `0032` power: supply: sm5705_charger: input limit from a USB-C
- `0033` dt-bindings: power: supply: sm5705-charger: add the torch LED
- `0034` power: supply: sm5705_charger: drive the flash LED as a torch
- `0035` extcon: sm5705: let the charger hold VBUS at 5 V
- `0036` power: supply: sm5705_charger: 5 V for USB-C sources and the
- `0078` dt-bindings: power: supply: sm5705-charger: add the USB OTG
- `0079` power: supply: sm5705_charger: provide the USB OTG VBUS
- `0084` power: supply: sm5705_charger: follow a USB PD contract above
- `0104` extcon: sm5705: report Samsung keyboard covers as a dock
- `0119` extcon: sm5705: see the keyboard cover while VBUS is present
- `0172` dt-bindings: sm5705: Add the battery and charger thermistors
- `0173` power: supply: sm5705_fuel_gauge: Read the battery's
- `0174` power: supply: sm5705_charger: Limit the current when the

### Samsung S2MM005 USB-C / PD (6)

- `0029` dt-bindings: usb: add Samsung S2MM005 Type-C and PD
- `0030` usb: typec: add Samsung S2MM005 Type-C and PD controller
- `0080` dt-bindings: usb: samsung,s2mm005: describe the source and
- `0081` usb: typec: s2mm005: support the source and host roles
- `0082` dt-bindings: usb: samsung,s2mm005: describe sink-pdos
- `0083` usb: typec: s2mm005: request the best fixed supply in

### Richtek RT5508 speaker amplifier (2)

- `0024` ASoC: dt-bindings: add Richtek RT5508 speaker amplifier
- `0025` ASoC: codecs: add Richtek RT5508 speaker amplifier driver

### Wacom W9019 (S-Pen digitizer) (6)

- `0058` dt-bindings: input: wacom: add W9019 variant
- `0059` Input: wacom_w9000: add W9019 support
- `0060` Input: wacom_w9000: add protocol KUnit tests
- `0061` Input: wacom_w9000: harden W9019 state transitions
- `0062` dt-bindings: input: wacom: document flash-mode polarity
- `0063` Input: wacom_w9000: report a direct input device

### STMPE1801 keyboard cover, I2C hot-plug (15)

- `0069` dt-bindings: mfd: stmpe: add STMPE1801 keypad limits
- `0070` mfd: stmpe: handle STMPE1801 read-to-clear interrupts
- `0071` mfd: stmpe: normalize block read results
- `0072` Input: stmpe-keypad: add STMPE1801 support
- `0073` Input: stmpe-keypad: add event parser KUnit tests
- `0074` Input: stmpe-keypad - add an OF match table
- `0075` mfd: stmpe: disable the supplies when probe fails
- `0114` dt-bindings: i2c: Document GPIO detected hot-plugged I2C bus
- `0115` i2c: muxes: Add GPIO-detected hotplug I2C
- `0116` i2c: muxes: i2c-hotplug-gpio: Fix the parent adapter lookup
- `0117` dt-bindings: i2c: Document extcon detected hot-plugged I2C
- `0118` i2c: muxes: i2c-hotplug-gpio: Support presence reported by
- `0120` mfd: stmpe: Remove the IRQ domain after the child devices
- `0121` input: stmpe-keypad: recover from an accessory power cycle
- `0124` input: stmpe-keypad: re-initialise the chip on resume

### Cameras: IMX258, DW9807, Samsung S5K5E3 (7)

- `0089` media: i2c: imx258: Add wait after enabling
- `0090` media: i2c: imx258: Add reset-gpio support
- `0091` media: i2c: imx258: add 16:9 modes
- `0092` dt-bindings: media: i2c: dongwoon,dw9807-vcm: add the
- `0093` media: i2c: dw9807-vcm: power the device from its supplies
- `0094` dt-bindings: media: i2c: Add Samsung S5K5E3 image sensor
- `0095` media: i2c: Add Samsung S5K5E3 image sensor driver

### Coreriver TC305K touch keys (2)

- `0159` dt-bindings: input: tm2-touchkey: Add Coreriver TC305K
- `0160` Input: tm2-touchkey - add Coreriver TC305K

## C. Tab S3 device tree (7)

The board itself. Goes upstream as one DTS once the bindings from B are in.

### SM-T820 / SM-T825 board (7)

- `0001` arm64: dts: qcom: Add Samsung Galaxy Tab S3 (gts3lwifi,
- `0196` arm64: dts: qcom: gts3lwifi: TrustZone buffer pool as CMA for
- `0197` arm64: dts: qcom: gts3lwifi: Egis ET510 fingerprint sensor
- `0223` arm64: dts: qcom: gts3lwifi: Deep sleep (MPM, L2 and system
- `0225` arm64: dts: qcom: gts3lwifi: Reset for the PCIe
- `0235` arm64: dts: qcom: gts3lwifi: Bluetooth holds the Rome 3.3 V
- `0236` arm64: dts: qcom: gts3lwifi: Wake-up sources in deep sleep

## D. Local only: debug aids, downstream ABIs, Android (19)

Not for mainline as they are: bring-up aids, the downstream QSEECOM ABI (upstream's qseecom framework differs), Android-only kernel features and our Android kernel config, the fingerprint control driver for the downstream HAL.

### Display bring-up aids (2)

- `0006` drm/msm/dsi: bring-up aids: rgb_swap and defer_hs_clk
- `0007` drm/msm: add a debugfs trigger for the DSI test pattern

### QSEECOM with the downstream ABI, fingerprint control (8)

- `0188` firmware: qcom: scm: QSEE app loading from firmware
- `0189` firmware: qcom: scm: QSEE listeners and raw app sends
- `0190` firmware: qcom: scm: QSEE app sends from the qseecom
- `0191` firmware: qcom: scm: QSEE app shutdown; Samsung
- `0192` firmware: qcom: /dev/qseecom with the downstream ABI
- `0193` input: misc: Egis ET5xx fingerprint sensor control
- `0194` input: misc: egis-et5xx-ctrl: create the fingerprint
- `0195` input: misc: egis-et5xx-ctrl: report the sensor type

### Android kernel features and config (9)

- `0198` kernel/configs: gts3-android.config: what Android
- `0199` fs: Keep PID 1's previous fs_struct while its threads
- `0200` selinux: Add a permissive lock
- `0201` netfilter: Add the quota2 match from the Android
- `0202` mm: Add the ashmem ioctl shim for memfd from the
- `0203` kernel/configs: gts3-android: What Android 13 to 16
- `0205` kernel/configs: gts3-android: The cgroup v1 cpuset hierarchy
- `0219` kernel/configs: gts3-android: vkms for a board without a
- `0222` kernel/configs: gts3-android: The MPM for deep sleep

## The MSM8996 community's patches (70)

Carried from the community tree; their own authors send them upstream. `m024` is marked HACK there.

- `m001` media: qcom: camss: Make ispif_reset go though VFE user
- `m002` dt-bindings: media: camss: qcom,msm8996-camss: Add CAMSS
- `m003` arm64: dts: qcom: msm8996: Add CAMSS power domain and
- `m004` media: qcom: camss: Add 8x96 power domain names
- `m005` drm/msm/dsi: improve power management
- `m006` arm64: dts: qcom: msm8996-xiaomi-common: Fix TUSB320l
- `m007` arm64: dts: qcom: msm8996-oneplus3t: Rebase on MSM8996 Pro
- `m008` arm64: dts: qcom: msm8996pro-oneplus3t: Add msm-id
- `m009` arm64: dts: qcom: msm8996pro-xiaomi-scorpio: Remove DSI PHY
- `m010` arm64: dts: qcom: msm8996-{xiaomi/oneplus}-common: Remove
- `m011` arm64: dts: qcom: msm8996-sony-xperia-tone: Add firmware
- `m012` arm64: dts: qcom: msm8996-sony-xperia-tone: Remove maxcpus=2
- `m013` drm/panel: Add support for SONY JDI Synaptics panel
- `m014` arm64: dts: qcom: msm8996-tone: Add display support
- `m015` arm64: dts: qcom: msm8996-sony-xperia-tone-kagura: add jdi
- `m016` arm64: dts: qcom: msm8996-sony-xperia-tone-keyaki: add jdi
- `m017` drm/panel: Add Samsung S6E3FA3 MIPI DSI panel driver
- `m018` drm/panel: Add Samsung S6E3FA5 MIPI DSI panel driver
- `m019` arm64: dts: qcom: msm8996-oneplus-common: Add display panel
- `m020` arm64: dts: qcom: msm8996 ZTE Axon 7 support
- `m021` arm-smmu: implement qcom,no-smr-check
- `m022` iommu/arm-smmu: Implement qcom,skip-init
- `m023` arm64: dts: qcom: msm8996-xiaomi-scorpio: Enable MDP SMMU
- `m024` HACK: arm64: dts: qcom: xiaomi-scorpio: Remove power-domains
- `m025` drm/panel: Add driver for LG.Philips SW43101 DSI video mode
- `m026` arm64: dts: qcom: msm8996-xiaomi-scorpio: Enable panel
- `m027` dt-bindings: power: supply: Add DT schema for Qualcomm SMBCHG
- `m028` arm64: dts: qcom: pmi8994: Add SMBCHG
- `m029` arm64: dts: qcom: pmi8996: Add SMBCHG
- `m030` arm64: dts: qcom: msm8996-xiaomi-*: Add battery data
- `m031` arm64: dts: qcom: msm8996-xiaomi-*: Enable SMBCHG
- `m032` soc: qcom: Add PMIC secure register write helpers
- `m033` util_macros.h: Add macro to find closest smaller value in
- `m034` power: supply: Add driver for Qualcomm SMBCHG
- `m035` arm64: dts: qcom: msm8996-oneplus-common: Enable SMBCHG
- `m036` arm64: dts: qcom: msm8996-xiaomi-common: Add VBUS power
- `m037` arm64: dts: qcom: msm8996-oneplus-common: Add VBUS power
- `m038` usb: dwc3: Add support for VBUS power control
- `m039` dt-bindings: power: supply: Add DT schema for qcom,fg
- `m040` arm64: dts: qcom: pmi8994: Add fuel gauge
- `m041` arm64: dts: qcom: pmi8996: Add fuel gauge
- `m042` arm64: dts: qcom: msm8996-xiaomi-*: Enable PMIC fuel gauge
- `m043` power: supply: Add driver for Qualcomm PMIC fuel gauge
- `m044` dt-bindings: input: add Qualcomm SPMI haptics driver
- `m045` input: add Qualcomm SPMI haptics driver
- `m046` arm64: dts: qcom: pmi8994: Add haptics
- `m047` arm64: dts: qcom: msm8996-xiaomi-*: Enable haptics
- `m048` arm64: dts: qcom: msm8996: Add CPU cooling support
- `m049` arm64: dts: qcom: msm8996-xiaomi-common: Fix dsi0 supplies
- `m050` arm64: dts: qcom: msm8996-xiaomi-gemini: Add second board-id
- `m051` arm64: dts: qcom: msm8996-xiaomi-scorpio: Enable CAMSS
- `m052` media: i2c: Add a driver for Sony IMX318
- `m053` dt-bindings: extcon: usbc-fusb301: Add bindings for FUSB301
- `m054` extcon: Add onsemi FUSB301 driver
- `m055` arm64: dts: qcom: msm8996-oneplus3: Add FUSB301
- `m057` remoteproc: qcom: Report last event on SSR notifier
- `m058` ASoC: dt-bindings: wcd9335: Add AIF4_CAP
- `m059` ASoC: wcd9335: Add AIF4_CAP
- `m060` arm64: dts: qcom: msm8996-xiaomi-*: Commonize and expand
- `m061` arm64: dts: qcom: msm8996pro-xiaomi-scorpio: Add rear camera
- `m062` arm64: dts: qcom: msm8996-oneplus3: disable usb3 clock to
- `m063` arm64: dts: qcom: msm8996pro-oneplus3t: use per panel dts
- `m064` dt-bindings: arm: qcom: Add Xiaomi Mi 5s
- `m065` arm64: dts: qcom: msm8996pro: xiaomi-capricorn: Add support
- `m066` net: qrtr: smd: Rename qdev to qsdev
- `m067` net: qrtr: Turn QRTR into a bus
- `m068` net: qrtr: Define macro to convert QMI version and instance
- `m069` iio: Add Qualcomm Sensor Manager driver
- `m070` soc: qcom: Add in-kernel sensors registry implementation.
- `m071` remoteproc: qcom: Enable in-kernel sns-reg
