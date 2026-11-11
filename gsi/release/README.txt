Galaxy Tab S3 (SM-T820, SM-T825): OneUI 8 on the mainline Linux kernel
TEST BUILD - experimental, erases the tablet's userdata partition

What it is
  OneUI 8 (Android 16) from a GSI system image, on our mainline kernel and
  an open vendor image (Mesa graphics, Codec2 V4L2 video decoding). No
  postmarketOS on the tablet. lk2nd starts it from the boot partition; the
  system image, vendor image and /data live on the userdata partition.
  Nothing from Samsung is in this package: the tablet's own firmware (Wi-Fi,
  Bluetooth, audio DSP, sensors, video, GPU) is read at every start from its
  own partitions (apnhlos, modem, system, efs), read only. They must still
  hold Samsung's stock firmware (as after any stock or LineageOS install).

State (tested on a T820 board WITHOUT screen, so read "untested" carefully)
  works:      Wi-Fi, Bluetooth, sensors (accelerometer, gyroscope, light),
              audio, GPU (OpenGL ES), hardware H.264 decoding, OneUI setup
              and apps (used remotely)
  UNTESTED:   the screen and touch under Android (the developer's board has
              no panel): please report first whether you see OneUI and can
              touch it; the SM-T825 as a whole
  not yet:    cameras, fingerprint, mobile network and GPS (SM-T825), Vulkan,
              Widevine L1 (HD streaming), deep sleep (standby drains the
              battery faster than stock)

You need
  - lk2nd on the tablet (from the earlier postmarketOS test rounds, or below)
  - a Linux PC with: fastboot, e2fsprogs (mke2fs 1.43+), unzip, and
    img2simg if possible (faster flashing), about 15 GB free
    (Debian/Ubuntu: apt install fastboot e2fsprogs unzip
     android-sdk-libsparse-utils; Arch: pacman -S android-tools e2fsprogs unzip)
  - the GSI: Oneui-gts9wifi-16-X710ZCS5DZA1-AB-20260211-MysticGSI.zip
    (MysticGSI's OneUI 8 build), sha256
    af400e8702f35234d9b004175f18da1697acb320232bd86dd5fc78f5b794537a

1) lk2nd (skip if you have it from a test round)
   Download mode: power off, hold Power + Volume Down + Home. Then
     Linux:  heimdall flash --BOOT lk2nd-gts3lwifi.img   (SM-T825: gts3llte)
     Odin:   lk2nd-gts3lwifi-odin.tar in the AP slot     (SM-T825: gts3llte)
   Use the file for your model: the wrong one makes the bootloader loop.

2) lk2nd's fastboot mode
   Power off, then power on holding Volume Down only (never both volume
   keys: that is the emergency download mode). Keep it held past the
   Samsung logo until lk2nd's fastboot screen. (After a forced restart,
   Power + Volume Down for 10 s, lk2nd ignores the keys: power off first.)
   Check: fastboot devices

3) Install
     ./install-oneui.sh ~/Downloads/Oneui-gts9wifi-16-X710ZCS5DZA1-AB-20260211-MysticGSI.zip
   It detects the model, builds the userdata image on the PC, asks, then
   flashes the boot image (after lk2nd) and userdata, and restarts.
   The first start takes about 8 minutes: Samsung logo, a black screen for a
   while, then OneUI's lock screen (swipe up). There is no setup wizard (this
   OneUI build is a Chinese one: its wizard wants an online activation), and
   OneUI starts in English. Then in Settings: Connections > Wi-Fi; General
   management > Language for another language; Accounts and backup for a
   Samsung account. A question about "S Share" and nearby devices may come
   up: either answer is fine.

Report
  - first: does the screen show OneUI, does touch work; then rotation,
    brightness, S-Pen, keyboard cover
  - Wi-Fi, Bluetooth, sound (speakers, headphones), charging, video
    playback, anything that freezes, restarts or crashes (and when)

Logs (tablet on USB)
  The PC gets a USB network interface and the address 172.16.42.2 from the
  tablet by DHCP (no gateway, no DNS: the PC's internet is not affected).
  If the PC doesn't take addresses by DHCP on new interfaces, set it by hand
  (the interface name from `ip -br link`, an enx... one):
      sudo ip addr add 172.16.42.2/16 dev enx9e74304f0ee6
  The tablet has a root shell for logs on 172.16.42.1 port 23 (USB only,
  not on Wi-Fi). Save the logs:
      printf 'dmesg\n' | timeout 20 nc 172.16.42.1 23 > dmesg.txt
      printf 'logcat -d\n' | timeout 60 nc 172.16.42.1 23 > logcat.txt
  After a reset or freeze, the kernel log of the previous start:
      printf 'cat /mnt/vendor/store/android/boot.prev.log\n' | timeout 30 nc 172.16.42.1 23 > boot.prev.log

Sources and licenses
  - kernel (Linux 7.3.1, GPL-2.0), gsi-init (the boot ramdisk), the
    installer and our vendor parts: github.com/wh1ter0z3/gts3lwifi-mainline,
    branch gsi (kernel: the android branch's patches plus gsi/kernel/)
  - vendor image: GloDroid's Android 13 vendor (AOSP, Apache-2.0 and
    others), Mesa 26.2.6 (MIT), Codec2 V4L2 libraries from GloDroid,
    busybox 1.38.0 (GPL-2.0, Alpine's busybox-static package), firmware
    from linux-firmware and wireless-regdb under their own licenses (the
    ath10k notice is in vendor.img: firmware/ath10k/QCA6174/hw3.0)
  - lk2nd: github.com/msm8916-mainline/lk2nd, the Tab S3 builds as in this
    repository's lk2nd release

Undo
  Flash the stock firmware (Odin), or lk2nd again plus your previous system.
  The userdata partition (all Android data, a postmarketOS install) is gone.
