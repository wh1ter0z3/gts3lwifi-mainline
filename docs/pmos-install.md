# postmarketOS with systemd and Plasma on the Galaxy Tab S3

## Status (2026-10-29): installed, boots to Plasma (Wayland)

**Packaged since 2026-10-30.** The pmaports packages in this repository now
carry what this install needed: the kernel (pkgrel 7) has patches 0001-0075
and the options below (`CONFIG_QCOM_SPMI_VADC=m`, which is enough), the main
DTS has the display, touch, S-Pen, microSD and keys, and the deviceinfo has
the command line below. Since 2026-10-31 the kernel package lists patches
0001-0104 (USB OTG and USB 3.0, HBM, the cameras, the cover as a dock) and
sets the camera sensor and lens options; 0105-0112 fix an oops in the
SLIMbus controller at boot (docs/audio.md). The keyboard cover joined the
main DTS on 2026-10-31 (attached at boot; no hotplug yet); only the panel
temperature channel (`dts/test/test-display-temp.dts`) is still a test DT.
The rest of this page records the original install.

postmarketOS edge with systemd and KDE Plasma on Wayland is installed on
the eMMC of an SM-T820, behind lk2nd, and boots to the desktop with
autologin. A first install with OpenRC and XFCE (X11) found the cursor
bug that patch 0068 fixes (docs/display.md, round 21).

What the install runs, against this repository:

- **Kernel:** the same 6.19.5 tree with patches 0001-0075, built on a
  host (the package APKBUILD lists patches up to 0043 only).
- **Device tree:** `dts/test/test-keyboard.dts` (display, touch, S-Pen,
  microSD, keys, keyboard cover; merged into the main DTS since) plus the
  two nodes of
  `dts/test/test-display-temp.dts` (panel temperature channel).
- **Kernel config:** `config-postmarketos-qcom-msm8996.aarch64` plus
  these, and nothing else:

  | Option | For |
  |---|---|
  | `CONFIG_TOUCHSCREEN_WACOM_W9000=m` | S-Pen, patches 0056-0063 |
  | `CONFIG_QCOM_SPMI_VADC=y` | panel temperature, 0066-0067 (the panel waits for the ADC if the channel is in the DT) |
  | `CONFIG_FRAMEBUFFER_CONSOLE_ROTATION=y` | console in portrait, 0055 |
  | `CONFIG_MFD_STMPE=m`, `CONFIG_STMPE_I2C=m`, `CONFIG_KEYBOARD_STMPE=m` | keyboard cover, 0069-0075 |

  (`CONFIG_QCOM_VADC_COMMON` becomes =y with the ADC.)
- **deviceinfo:** as in `pmaports/device-samsung-gts3lwifi`, except
  `deviceinfo_dtb` (the DTB above) and

      deviceinfo_kernel_cmdline="console=tty0 clk_ignore_unused pd_ignore_unused sysctl.kernel.firmware_config.ignore_sysfs_fallback=1"

  With the display in the DT the msm driver drives the panel, and the GPU
  binds to the display's DRM device, so `lk2nd.pass-simplefb=autorefresh`
  and `msm.separate_gpu_kms=1` go. `msm` then autoloads for the display
  subsystem; `modules-load.conf` can stay.
- **Firmware:** `linux-firmware-ath10k`, `-qca` and `-qcom` (device
  package dependencies; linux-firmware's `a530_pm4.fw`, `a530_pfp.fw` and
  `a530v3_gpmu.fw2` work), plus Samsung's signed files from the tablet's
  own partitions: zap shader and Venus from `apnhlos`, ADSP from `modem`,
  in `qcom/msm8996/samsung/gts3/` (docs/gpu.md, docs/venus.md,
  docs/audio.md), and the Wi-Fi board data `bdwlan30.bin` as
  `ath10k/QCA6174/hw3.0/board.bin` (docs/wifi.md). We put the board file
  under `/lib/firmware/updates/`, which the kernel searches first.

## Layout and install

The layout is what `pmbootstrap install` makes for a fastboot device
with the rootfs on userdata: userdata (`mmcblk0p47`) holds an MBR with
`pmOS_boot` (ext2, 512 MiB) and `pmOS_root` (ext4, the rest), which the
postmarketOS initramfs finds as subpartitions. The boot image goes 4 MiB
into the boot partition, behind lk2nd, which boots it from there
(docs/lk2nd.md). Android's userdata is gone; TWRP stays in the recovery
partition.

We installed without pmbootstrap, from a RAM-booted installer image with
USB networking and `apk.static`, doing what pmbootstrap does:

1. Get into lk2nd's fastboot: hold Volume Down while powering on. From a
   running system, as root, zero the boot image header behind lk2nd and
   reboot; lk2nd then finds no kernel and stays in fastboot:

       grep PARTNAME=boot /sys/class/block/mmcblk0p28/uevent
       dd if=/dev/zero of=/dev/mmcblk0p28 bs=4096 seek=1024 count=1

2. `fastboot boot` the installer image. Set its clock first (it starts in
   January and TLS fails) and give it a CA bundle.
3. Partition userdata as above (`sfdisk`; `mkfs.ext2 -L pmOS_boot`;
   `mkfs.ext4 -O ^metadata_csum -L pmOS_root`, as pmbootstrap does).
4. Base system: the usr-merge symlinks (`bin`, `sbin`, `lib`,
   `usr/sbin`) before anything is installed, as pmbootstrap's chroot
   setup does; `apk --initdb` with `alpine-baselayout apk-tools busybox
   musl-utils`; then `postmarketos-base-systemd` (from the
   `extra-repos/systemd/master` repository, listed before `master`),
   `postmarketos-base`, `postmarketos-base-nofde` and `soc-qcom-msm8996`.
5. `/etc/fstab` with the two filesystems' UUIDs, then the kernel,
   firmware and device packages (the kernel's install runs mkinitfs),
   then `postmarketos-ui-plasma-desktop`.
6. Configuration: the user in `netdev plugdev wheel audio input video
   autologin`; autologin through plasma-login-manager, the login manager
   of the systemd Plasma flavour (`/etc/plasmalogin.conf.d/`, an
   `[Autologin]` section with `User=` and `Session=plasma.desktop`, the
   same keys as SDDM); a NetworkManager Wi-Fi profile; ssh keys; the Home
   key hwdb entry (docs/input.md).
7. Write `/boot/boot.img` 4 MiB into the boot partition
   (`dd if=/boot/boot.img of=/dev/mmcblk0p28 bs=1M seek=4 conv=fsync`,
   after the same `PARTNAME=boot` check) and reboot.

After a kernel or initramfs update, write `/boot/boot.img` behind lk2nd
again (step 7): lk2nd reads the boot partition, not `pmOS_boot`.

Back to Android: TWRP is still in recovery (Power + Volume Down to reset,
then Power + Volume Up + Home); flash a ROM and format data, since the
install replaced userdata.

## CPUs

Two clusters of two Kryo cores: cpu0-1 up to 1.5936 GHz, cpu2-3 up to
2.1504 GHz, one cpufreq-dt policy each with schedutil. The CPU
capacities are 758 and 1024, from the frequency ratio (the same core,
equal `capacity-dmips-mhz`), so the scheduler moves heavy tasks to
cpu2-3. The CPU, cluster (L2 GDHS) and system (retention) idle states are
all entered (docs/cpuidle.md).

There is no energy model: the DT has no `dynamic-power-coefficient`, so
energy-aware scheduling is not active although
`/proc/sys/kernel/sched_energy_aware` reads 1.

## Known issues

- **Kernel log, all known and harmless so far:** four `pclk0/pclk1_clk_src:
  rcg didn't update its configuration` WARN traces while the DSI clocks
  are set up; `fb0: sys_imageblit: framebuffer is not in virtual address
  space` (the msm fbdev has no FBINFO_VIRTFB); bam-dma at 0x644000 fails
  to probe with -22 (`num-channels` missing), so the crypto engine stays
  deferred; `sns.reg` firmware missing; ath10k falls back from
  `board-2.bin` to `board.bin`; the initramfs udev logs "No such file" for
  modules that are only on the rootfs; Bluetooth "Frame reassembly failed
  (-84)" during the UART baud change; the display series' debug lines
  (`split dbg`, `op_mode was`).
- **GPU faults:** under Plasma the kernel logged bursts of GPU page faults
  of type TRANSLATION (all reads, one ttbr0), on every Plasma boot so far
  and not only at login: one boot logged bursts at 40, 55, 63, 374 and
  618 s, and the log is rate-limited (6350 more suppressed in the 63 s
  burst alone). No hang or GPU recovery followed and nothing visibly broke.
  Not looked into yet (which client and which buffer).
- **X11 hardware cursor:** needs patch 0068, and then lags; use
  `Option "SWcursor"` under X (docs/display.md, round 21). Wayland is not
  affected.
- **Keyboard cover:** only when attached at boot (docs/input.md).

## TODO

- Hall sensor (PM8994 GPIO 7, stock `hall_flip`) as SW_LID once suspend
  works; until then logind would suspend whenever the cover closes.
- Back/Recent touch keys: Coreriver TC305K (docs/input.md).
- Energy model: `dynamic-power-coefficient` for both clusters (measured
  with the fuel gauge, or from the downstream energy tables), which turns
  energy-aware scheduling on.
- Keyboard cover hotplug through the SM5705 MUIC (docs/input.md).
- The USB 3.0 redriver stays enabled (PM8994 GPIO 14): measure its
  suspend drain once suspend is reliable (docs/usb.md).
- An image built with pmbootstrap (or a recovery zip for TWRP) instead of
  the manual install.
