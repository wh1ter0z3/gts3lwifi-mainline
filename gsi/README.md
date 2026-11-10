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
   (`/dev/gsi/modprobe`), and sets up zram swap (lz4, three quarters of the RAM: OneUI's
   pages compress about 5:1; Android's `swapon_all` fails on 7.x);
3. USB NCM gadget (same IDs and address as pmOS: 172.16.42.1), a root shell on TCP port 23
   (`nc 172.16.42.1 23`), hardware watchdog feeder, kernel log streamed to `log.img`,
   dead-man timer for tests (`gsi.deadman=MIN`: reboots unless `<dir>/keep` exists by then;
   left out for daily use), a keeper for the USB link's routes (netd's routing policy would
   cut it);
4. mounts `system.img` (+ `product.img`, `system_ext.img`), `vendor.img`, `userdata.img` on
   `/data` (loop7; the fstab marks it `first_stage_mount`), binderfs, selinuxfs, and a tmpfs
   `/odm` holding the board's `ueventd.rc` and an init rc: `sys.use_memfd=true` (no ashmem in
   mainline), `kernel.modprobe` and sysrq again after `init.rc` blanks them, and with
   `gsi.vkms=1` `vendor.hwc.drm.device` = the vkms card;
5. binds a copy of `/proc/cmdline` with the `androidboot.*` parameters (they can't be in the
   boot image: lk2nd would take it for a downstream boot), locks SELinux permissive
   (`gsi.selinux=permissive`, the default), and starts Android's init at `selinux_setup`.

Files in `<dir>/odm/` are copied into that `/odm` at boot: board files for one image set
(init rc files, VINTF fragments, HALs and their libraries, a `media_codecs.xml`, which
Android reads from `/odm/etc` before `/vendor/etc`) without rebuilding the vendor image.

Paths listed in `<dir>/vendor-bind/BIND` (one per line, relative to `/vendor`) are bound
from `<dir>/vendor-bind/` over the vendor's: another driver without a vendor rebuild (see
"Mesa"); delete the folder to go back.

Parameters: `gsi.dir=` (image directory, default `/android`), `gsi.data=SIZE` (size of
`userdata.img`: made at it, or grown to it offline after an `e2fsck` when it is smaller and the
pmOS root has room for all of it and 1 GB more; never shrunk), `gsi.deadman=MIN`, `gsi.vkms=1` (virtual display for a board without
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
- 0192: the DRM crtc fence's name callbacks hit a BUG_ON when the fence is signalled while
  they run (7.x detaches fence ops on signal); SurfaceFlinger asks for fence names all the
  time, so OneUI panicked the kernel within the hour.
- 0193: `CPUSETS_V1`, Android's cpuset hierarchy (gsi-init's board rc puts background work
  on CPUs 0-1).
- 0194, 0195: Venus encoder for Android's `v4l2_codec2` (see "Hardware video encoding"):
  RGBA input also under the name `BGR32`, which the component asks for (not for upstream),
  and bitstream buffers of the driver's own when the capture queue imports dma-bufs: the
  msm8996 firmware wants all of them when the session starts and has no dynamic mode.
- 0196: frame and bitstream sizes that fit what `v4l2_codec2` hands over (RGBA lines 64
  pixels aligned like the GPU's, not 128; bitstream dma-bufs held to the firmware's size).

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
Wi-Fi Direct feature, our mixer configuration for its tinyhal audio HAL
(`vendor-overlay/etc/audio.gts3.xml`, from the UCM profile: speakers and built-in mics, 40 ms
periods), the effects configuration and the audio policy also under Samsung's names
(`audio_effects_sec.xml`: OneUI's audioserver aborts at the first sound without one;
`audio_policy_configuration_sec.xml`: otherwise OneUI uses a built-in default without
remote submix, USB or Bluetooth audio), busybox and `gsi-login` for the
root shell; labels are kept (init
refuses services whose binary has none, even when permissive). With `HEALTH=`, `LIGHTS=`
and `SEHSTUBS=` it puts in our HALs (`health/`, `sh health/build.sh` with the Android NDK):

- `health-mainline`: IHealth v1 from `/sys/class/power_supply`, plus a stub of Samsung's
  `ISehHealth` as its binder extension (OneUI calls `getExtension()` on the health HAL
  without a null check, and takes its battery updates only from Samsung's callback).
  It reports a full battery on the charger as full while the charger tops it up (else
  OneUI plays its charging sound at every top-off), shows a non-good health only after
  10 s, and tells the callbacks only about visible changes (at most once a minute
  otherwise);
- `lights-mainline`: ILights v2, the backlight and the touch key LED, and battery,
  notification and attention lights that do nothing (OneUI's LightsService assumes them);
- `seh-stubs`: stand-ins for the Samsung HALs a OneUI system_server waits for forever
  (display quality, KHDM, DDAR): every call answered with "no exception" and zeros.

They link against a stub of `libbinder_ndk` made from `health/binder-syms.txt` (symbols and
version nodes as Android 13 exports them); plain system images use only the standard
interfaces.

`tools/mkvendor-waydroid.sh` (older path): Waydroid's vendor with pmOS's cage and PipeWire
as its compositor and audio server; it only works with Waydroid's own system image.

## Hardware video encoding

The vendor has no Codec2 component for V4L2 encoders. `tools/mkodm-c2v4l2.sh` makes a
`<dir>/odm` overlay with AOSP's `v4l2_codec2` service from GloDroid's Raspberry Pi 4 build
(`gd-aosp-gd_rpi4-*.tar.xz`, Android 15; a VNDK-less vendor, so the service's 66 libraries
come along, in `/odm/lib64/c2v4l2` for this service only), our init rc, VINTF fragment and
`media_codecs.xml` (`odm-c2v4l2/`). It needs patches 0194 and 0195 and an Android 15+ system
image (its `libui` uses an API 35 `libbinder_ndk` call). MediaCodec then lists
`c2.v4l2.avc.encoder` first (rank 1), the default H.264 encoder:
`scrcpy -s 172.16.42.1:5555 --max-fps 60 --video-bit-rate 12M`. gsi-init keeps Venus out of
runtime suspend (see "Known issues"). Scrolling in Settings
records at about 29 frames per second at 768x1024 and up to 38 at 1536x2048, against 2.6
with the software encoder at the same CPU load. At 1440x1920 the GPU hands over a tiled
frame and the picture is scrambled. The decoder is not listed yet.

The virtual display is 1024x768 at 320 dpi, a phone's layout (512x384 dp). For the Tab S3's
(2048x1536 at 320 dpi, 1024x768 dp): `wm size 2048x1536` (kept across boots); scrcpy then
mirrors 1536x2048.

## Mesa

The GloDroid vendor's Mesa 23.1.7 draws OneUI's rounded list cards with square bites at
their corners (in screenshots too, so in the app's rendering). Mesa 26.2.6 doesn't.
`tools/build-mesa.sh <mesa source> <build dir> <out dir>` builds its EGL/GLES with the NDK
(freedreno only, Android 13 API, libdrm and libc++ static, about 20 minutes on two
cores); the four libraries go to `<dir>/vendor-bind/lib64/egl/` with `lib64/egl` in
`<dir>/vendor-bind/BIND`.

Built outside AOSP, Mesa has no IMapper backend and can't learn a buffer's layout, so it
falls back to single-plane RGB and can't import YUV buffers (video). `gralloc-shim/` is a
gralloc0 module named "CrOS Gralloc" that answers the one call Mesa's minigbm backend
makes (`CROS_GRALLOC_DRM_GET_BUFFER_INFO`) from the minigbm handle itself (the magic
checked at byte 116, as the vendor's minigbm does); `sh gralloc-shim/build.sh`, then
`gralloc.gts3.so` to `<dir>/odm/lib64/hw/`. Mesa logs "Using gralloc0 CrOS API";
`yuvtest` (same folder) imports a YUV buffer and reads back the expected grey.

## OneUI notes

MysticGSI is built from the Chinese Tab S9 firmware: its setup wizard starts in Chinese and
insists on an online activation at the Wi-Fi step. Accepting the required terms and then
`settings put global device_provisioned 1`, `settings put secure user_setup_complete 1`,
`pm disable-user com.sec.android.app.SecSetupWizard` gets to the home screen. adb: write the
PC's `adbkey.pub` to `/data/misc/adb/adb_keys` (`system:shell`, 0640) from the root shell,
`setprop persist.adb.tcp.port 5555`, restart `adbd`; `scrcpy -s 172.16.42.1:5555 --max-fps 30 --video-bit-rate 4M`
(audio works since the `_sec` policy file). The root shell itself can't run Android's
Java tools (it lives in the mount namespace from before APEX activation).

## Known issues and next steps

- Android's `ueventd` ignores live uevents after its coldboot here (no nodes for devices that
  appear later); pmOS's udevd covers the host side.
- Audio: Android plays through the speakers (tinyhal, `audio.gts3.xml`); headphones and the
  headset mic (PCMs of their own) not yet; playback capture works (scrcpy's audio);
  sensors, camera untested;
- Video encoding in hardware needs the `odm-c2v4l2` overlay (see above); without it scrcpy
  wants `--max-fps 30 --video-bit-rate 4M`. While scrolling, OneUI's list cards show
  rectangular notches at their edges in recordings with either encoder: from the rendering,
  not the encoder (not looked into); Samsung daemons `perfsdkserver` and `ss_conn_daemon2` crash (harmless).
- A real panel (`drm_hwcomposer` on the MDP) needs a tester with a display.
- After a reset, read `fastboot oem ramoops console` from lk2nd before booting anything else
  (`lk2nd.pass-ramoops=zap` wipes it); that is how the fence panic (0192) was found. gsi-init
  keeps the previous boot's kernel log as `<dir>/boot.prev.log`. Android lowers the console
  log level to 3: `echo 7 4 1 7 > /proc/sys/kernel/printk` from the root shell.
- Venus runtime PM: the HFI v3 core powers down about 2 s after it goes idle (after an encoder
  session, and within one while the screen is static); with that, OneUI froze the board
  (silent, watchdog reset) eight times within minutes of hardware encoder sessions. With its
  `power/control` at `on` (gsi-init), none in dozens of sessions and hours. The driver's
  power collapse/resume on HFI v3 is still to be fixed; this costs some idle power.
- The Venus encoder's drain at the end of a session sometimes races the codec's restart
  (`V4L2_ENC_CMD_START` while still draining: -EBUSY); scrcpy then ends with an encoding
  error, at its end only.
- USB is our network gadget's: gsi-init's board rc unmounts adbd's FunctionFS and stops the
  vendor's gadget HAL, which otherwise retried binding Android's `g1` a few times a second
  (a uevent storm; it once took the link down). adb runs over TCP.
- Bluetooth: gsi-init sets the controller's address from efs (pmOS's `gts3-bdaddr`; without
  it hci0 stays unconfigured and the btlinux HAL waits forever) and the board rc turns off
  Android's vendor HCI capabilities (`bluetooth.core.le.vendor_capabilities.enabled`): the
  QCA6174 firmware claims them but doesn't answer `LE_ADV_FILTER` and the stack aborted.
- A board without panel: add `video=DSI-1:d` to the command line, or the framebuffer
  console keeps committing to the MDP (pp done timeouts, a dozen a second).
- OneUI daily use: a crash-looping `com.sec.android.sdhms` (uninstalled for user 0) fed
  Samsung's bug report tool (`dmabuf_dump` at 100% CPU); `com.qti.qcc` polled a missing HAL
  every second. Disabled (`pm disable-user`) to save RAM on a Wi-Fi tablet without camera:
  Secure Folder, diagmon, CHN device management, beacon manager, smart suggestions, MCF,
  VEX framework, SVE, SCPM, IMS.
- OneUI's list cards showed square bites at their rounded corners with the vendor's
  Mesa 23.1.7: fixed with Mesa 26.2.6 (see "Mesa").
- The kernel patches are kept here, not yet in the pmaports package.
