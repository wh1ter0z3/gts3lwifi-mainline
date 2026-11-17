Galaxy Tab S3 (SM-T820, SM-T825): Android on the mainline Linux kernel
with our open-source vendor - TEST BUILD, erases the userdata partition

What it is
  Any GSI (a generic Android system image: arm64, A/B, Android 15, 16 or
  17) on our mainline kernel (Linux 7.3.1) and our vendor image, built from
  AOSP 15 source with our device tree: Mesa graphics, hardware video,
  audio, Wi-Fi, Bluetooth. Pick the GSI you like; we maintain the vendor
  and the kernel, not the GSIs. We test with TrebleDroid's AOSP 16.
  lk2nd starts it from the boot partition; the system image, the vendor
  image and /data live on the userdata partition. Nothing from Samsung is
  in this package: the tablet's own firmware (Wi-Fi, Bluetooth, audio DSP,
  video, GPU) is read at every start from its own partitions (apnhlos,
  modem, system, efs), read only. They must still hold Samsung's stock
  firmware (as after any stock or LineageOS install).

State (tested on a T820 board WITHOUT screen, so read "untested" carefully)
  works:      boot (about 1.5 minutes), Wi-Fi, Bluetooth, speakers and
              microphones, GPU (OpenGL ES 3.1), hardware H.264 video
              decoding and encoding (playback, screen recording), battery
              and charging, restarting into recovery
  UNTESTED:   the screen and touch (the developer's board has no panel):
              please report first whether you see Android and can touch
              it; the SM-T825 as a whole
  not yet:    sensors (auto-rotate, auto brightness), headphone switching,
              cameras, fingerprint, GPS, mobile network (SM-T825), Vulkan,
              Widevine L1 (HD streaming), standby: this build keeps the
              tablet awake for the USB log link, so the battery drains

You need
  - a GSI. What we test with: TrebleDroid's AOSP 16,
    system-td-arm64-vanilla.img.xz from
    github.com/TrebleDroid/treble_experimentations/releases, sha256
    d06c66e45c6d1e8d61b45d1261a65cb7ccf2e7946739602011e500a12ae55ad6
    Other GSIs: arm64, A/B ("ab"), as .img, .img.xz, .img.gz or a .zip
  - gts3-android-...-twrp.zip from this release: the whole package (boot
    images, vendor image, lk2nd)
  - and either TWRP / OrangeFox on the tablet (way A, no PC) or a Linux PC
    with lk2nd on the tablet (way B)

A) Without a PC: TWRP or OrangeFox (TWRP 3.x, old or new). The zip brings
   its own tools and writes lk2nd itself.
  1. Copy gts3-android-...-twrp.zip and the GSI into the same folder on the
     microSD card or a USB drive. NOT internal storage: it gets erased.
  2. Boot the recovery: power off, then hold Power + Volume Up + Home.
  3. Install > gts3-android-...-twrp.zip, swipe. It takes some minutes.
  4. Reboot > System.

  Not the recovery's "Install Image" with the separate .img files: the boot
  image needs lk2nd in front of it, and the system and vendor images go on
  the userdata partition, not on partitions of their own.

B) From a Linux PC, with lk2nd
  The PC needs fastboot, e2fsprogs (mke2fs 1.43+), unzip, xz, and img2simg
  if possible (faster flashing), about 15 GB free (Debian/Ubuntu: apt
  install fastboot e2fsprogs unzip xz-utils android-sdk-libsparse-utils;
  Arch: pacman -S android-tools e2fsprogs unzip xz).
  1. lk2nd (skip if you have it from a postmarketOS or OneUI test round)
     Download mode: power off, hold Power + Volume Down + Home. Then
       Linux:  heimdall flash --BOOT lk2nd-gts3lwifi.img   (SM-T825: gts3llte)
       Odin:   lk2nd-gts3lwifi-odin.tar in the AP slot     (SM-T825: gts3llte)
     Use the file for your model: the wrong one makes the bootloader loop.
  2. lk2nd's fastboot mode: power off, then power on holding Volume Down
     only (never both volume keys: that is the emergency download mode).
     Keep it held past the Samsung logo until lk2nd's fastboot screen.
     Check: fastboot devices
  3. Put install-gsi.sh and gts3-android-...-twrp.zip in one folder, then
       ./install-gsi.sh ~/Downloads/system-td-arm64-vanilla.img.xz
     It detects the model, builds the userdata image on the PC, asks, then
     flashes the boot image (after lk2nd) and userdata, and restarts.

First start (both ways)
  A few minutes: Samsung logo, a black screen for a while, then the GSI's
  setup or lock screen.

Report
  - first: does the screen show Android, does touch work; then rotation,
    brightness, S-Pen, keyboard cover
  - Wi-Fi, Bluetooth, sound, video playback, charging, anything that
    freezes, restarts or crashes (and when), and which GSI you used

Logs (tablet on USB)
  The PC gets a USB network interface and the address 172.16.42.2 from the
  tablet by DHCP (no gateway, no DNS: the PC's internet is not affected).
  If the PC doesn't take addresses by DHCP on new interfaces, set it by hand
  (the interface name from `ip -br link`, an enx... one):
      sudo ip addr add 172.16.42.2/16 dev enx9e74304f0ee6
  The tablet has a root shell for logs on 172.16.42.1 port 23 (USB only,
  not on Wi-Fi). Save the logs:
      printf 'dmesg; exit\n' | nc 172.16.42.1 23 > dmesg.txt
      printf 'logcat -d; exit\n' | nc 172.16.42.1 23 > logcat.txt
  After a reset or freeze, the kernel log of the previous start:
      printf 'cat /mnt/vendor/store/android/boot.prev.log; exit\n' | nc 172.16.42.1 23 > boot.prev.log

Sources and licenses
  - kernel (Linux 7.3.1, GPL-2.0), gsi-init (the boot ramdisk) and the
    installers: github.com/wh1ter0z3/gts3lwifi-mainline, branch gsi
    (kernel: the android branch's patches plus gsi/kernel/)
  - vendor image: AOSP 15 (android-15.0.0_r36; Apache-2.0 and others) with
    our device tree and patches, Mesa 26.3 (MIT), busybox 1.38.0 (GPL-2.0,
    Alpine's busybox-static package), the kernel's modules (GPL-2.0),
    firmware from linux-firmware and wireless-regdb under their own
    licenses (the ath10k notice is in vendor.img:
    firmware/ath10k/QCA6174/hw3.0)
  - lk2nd: github.com/msm8916-mainline/lk2nd, the Tab S3 builds as in this
    repository's lk2nd release

Undo
  Flash the stock firmware (Odin), or lk2nd again plus your previous system.
  The userdata partition (all Android data, a postmarketOS install) is gone.
