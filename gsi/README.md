# Android system images (GSIs) on the mainline kernel

Boot an Android system image (a GSI, or any system-as-root build) on the Tab S3's mainline
kernel with one vendor image of our own, Treble style: no ROM build, nothing reflashed. The
images are files on the postmarketOS root partition; pmOS keeps booting from the same disk.
Experimental, on this `gsi` branch.

What booted on the SM-T820 (bare board, no panel: on the virtual vkms display):

| System image | Android | Result |
|---|---|---|
| GloDroid (its own system) | 13 | launcher; SurfaceFlinger on the GPU (`freedreno, FD530, Mesa 23.1.7`) |
| TrebleDroid `system-td-arm64-vanilla` (ci-20250617) | 16 | launcher; all vendor HALs up |
| MysticGSI OneUI 8 (Galaxy Tab S9, SM-X710) | 16 | setup wizard, home screen; battery shown |

## How it works

`ramdisk/init` ("gsi-init") is the boot ramdisk and does the job of Android's first stage
init:

1. mounts the pmOS root (`pmos_root_uuid`, as pmOS's own boot image) and uses it as a chroot
   for host services: pmOS's `systemd-udevd` runs there for the whole session (modules for
   devices that appear later, the udev database; `SYSTEMD_IGNORE_CHROOT=1`), with the
   ramdisk's devtmpfs as its `/dev`;
2. loads the iptables modules netd needs (Android blanks `kernel.modprobe`, and netd's legacy
   iptables can't load them) and points the kernel's module requests at pmOS's `modprobe`
   (`/dev/gsi/modprobe`);
3. USB NCM gadget (same IDs and address as pmOS: 172.16.42.1), a root shell on TCP port 23
   (`nc 172.16.42.1 23`), hardware watchdog feeder, kernel log streamed to `log.img`,
   dead-man timer (`gsi.deadman=MIN`: reboots unless `<dir>/keep` exists by then), a keeper
   for the USB link's routes (netd's routing policy would cut it);
4. mounts `system.img` (+ `product.img`, `system_ext.img`), `vendor.img`, `userdata.img` on
   `/data` (loop7; the fstab marks it `first_stage_mount`), binderfs, selinuxfs, and a tmpfs
   `/odm` holding the board's `ueventd.rc` and an init rc: `sys.use_memfd=true` (no ashmem in
   mainline), `kernel.modprobe` and sysrq again after `init.rc` blanks them, and with
   `gsi.vkms=1` `vendor.hwc.drm.device` = the vkms card;
5. binds a copy of `/proc/cmdline` with the `androidboot.*` parameters (they can't be in the
   boot image: lk2nd would take it for a downstream boot), locks SELinux permissive
   (`gsi.selinux=permissive`, the default), and starts Android's init at `selinux_setup`.

Parameters: `gsi.dir=` (image directory, default `/android`), `gsi.data=SIZE` (new
`userdata.img`), `gsi.deadman=MIN`, `gsi.vkms=1` (virtual display for a board without
panel), `gsi.selinux=permissive|enforcing`, and for the Waydroid vendor
`gsi.compositor=cage|cage-headless` and `gsi.audio=pipewire` (cage and PipeWire from pmOS).

## Kernel

The `android` kernel branch (7.3, `gts3-android.config`) plus `kernel/0187`..`0191`:

- 0187: Linux 7.x points PID 1's previous fs_struct at nullfs when PID 1 unshares it;
  Android's init does so while its property service thread runs on it, which then loses
  `/` (every property set fails). Kept while other threads still use it.
- 0188: `selinux.permissive_lock` (gsi-init sets it): for user builds that switch to
  enforcing mode whatever `androidboot.selinux` says.
- 0189: `xt_quota2` from the Android Common Kernel (netd's global alert, Android 13).
- 0190: the memfd ashmem ioctl shim from the Android Common Kernel (Android 16's libcutils).
- 0191: the config: netd's tables and matches, IPsec, IPv6 sysctls, socket destruction,
  dummy0, the shim.

`tools/mkgsiboot.sh <Image.gz> <board.dtb> <pmos root uuid> <out.img>` makes the boot image
(`WDT=1` adds the watchdog node, `VKMS=1`, `EXTRA_CMDLINE=`; it wants a static aarch64
busybox as `ramdisk-bin-busybox`, e.g. Alpine's `busybox-static`). Build the kernel with
its own `LOCALVERSION` (e.g. `-gsi`) so its modules don't replace pmOS's. Boot with
`fastboot boot` from lk2nd (boot slot cleared).

## Vendor image

GloDroid's `qcom-msm8916-series` Android 13 build (`glodroid-aosp-idol347-…zip`) has the
vendor this needs: Mesa freedreno (it knows the A530), drm_hwcomposer, minigbm, generic HALs,
built for cortex-a53 (no ARMv8.1+ instructions, unlike Linaro's db845c vendor, which crashes
on the Kryo), stock AOSP libraries (unlike Waydroid's, tied to Waydroid's patched
libhidlbase), VNDK 33, so Android 13 to 16 system images accept it.

`tools/mkvendor-glodroid.sh` (root, on the tablet) adapts its `vendor.img` (unsparsed):
our fstab, firmware from `/lib/firmware`, no kernel modules, no phone modem scripts (they
start the MSS), no vibrator HAL (no motor), KeyMint patch levels, `ro.hardware.egl=mesa`,
Wi-Fi Direct feature, busybox and `gsi-login` for the root shell; labels are kept (init
refuses services whose binary has none, even when permissive). With `HEALTH=`, `LIGHTS=`
and `SEHSTUBS=` it puts in our HALs (`health/`, `sh health/build.sh` with the Android NDK):

- `health-mainline`: IHealth v1 from `/sys/class/power_supply`, plus a stub of Samsung's
  `ISehHealth` as its binder extension (OneUI calls `getExtension()` on the health HAL
  without a null check, and takes its battery updates only from Samsung's callback);
- `lights-mainline`: ILights v2, the backlight and the touch key LED, and battery,
  notification and attention lights that do nothing (OneUI's LightsService assumes them);
- `seh-stubs`: stand-ins for the Samsung HALs a OneUI system_server waits for forever
  (display quality, KHDM, DDAR): every call answered with "no exception" and zeros.

They link against a stub of `libbinder_ndk` made from `health/binder-syms.txt` (symbols and
version nodes as Android 13 exports them); plain system images use only the standard
interfaces.

`tools/mkvendor-waydroid.sh` (older path): Waydroid's vendor with pmOS's cage and PipeWire
as its compositor and audio server; it only works with Waydroid's own system image.

## OneUI notes

MysticGSI is built from the Chinese Tab S9 firmware: its setup wizard starts in Chinese and
insists on an online activation at the Wi-Fi step. Accepting the required terms and then
`settings put global device_provisioned 1`, `settings put secure user_setup_complete 1`,
`pm disable-user com.sec.android.app.SecSetupWizard` gets to the home screen. adb: write the
PC's `adbkey.pub` to `/data/misc/adb/adb_keys` (`system:shell`, 0640) from the root shell,
`setprop service.adb.tcp.port 5555`, restart `adbd`; `scrcpy -s 172.16.42.1:5555 --no-audio`
(the audio configuration is still the Idol 3's). The root shell itself can't run Android's
Java tools (it lives in the mount namespace from before APEX activation).

## Known issues and next steps

- Android's `ueventd` ignores live uevents after its coldboot here (no nodes for devices that
  appear later); pmOS's udevd covers the host side.
- Audio: GloDroid's tinyhal needs a mixer XML for our card (from the UCM profile); sensors,
  camera untested; Samsung daemons `perfsdkserver` and `ss_conn_daemon2` crash (harmless).
- A real panel (`drm_hwcomposer` on the MDP) needs a tester with a display.
- One unexplained reboot of OneUI after ~1.5 h: read `fastboot oem ramoops console` from lk2nd
  before booting anything else (`lk2nd.pass-ramoops=zap` wipes it).
- The kernel patches are kept here, not yet in the pmaports package.
