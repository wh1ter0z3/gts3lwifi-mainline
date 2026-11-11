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
  - does the screen show OneUI, does touch work, rotation, brightness
  - Wi-Fi, Bluetooth, sound, charging, video playback
  - with the tablet on USB, the PC sees a network interface (172.16.42.2);
    a root shell for logs is on 172.16.42.1 port 23 (USB only, not on
    Wi-Fi):
      nc 172.16.42.1 23
      dmesg > /data/local/tmp/dmesg.txt; logcat -d > /data/local/tmp/logcat.txt
    and the kernel log of the previous start: /mnt/vendor/store/android/boot.prev.log

Undo
  Flash the stock firmware (Odin), or lk2nd again plus your previous system.
  The userdata partition (all Android data, a postmarketOS install) is gone.
