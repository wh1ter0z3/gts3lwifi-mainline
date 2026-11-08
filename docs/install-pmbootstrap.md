# Installing postmarketOS with pmbootstrap (main or android kernel)

From zero to postmarketOS with Plasma on the Galaxy Tab S3, built yourself with
pmbootstrap from this repository. The same steps give either kernel: branch `main` (the
normal one) or branch `android` (the same kernel plus what Android needs, see
[android.md](android.md)). For a ready-made image instead, see the preview (not released
yet).

**Status:** written 2026-11-08 from our install notes ([pmos-install.md](pmos-install.md),
[lk2nd.md](lk2nd.md)), the device's flashing settings and the preview build. Not yet run
end to end by anyone as written: the package overlay in step 2 and the Waydroid autologin
in step 7 are untested. Tell us where it goes wrong.

**This erases the tablet's data** (userdata) **and replaces Android's boot partition.**
Back up first. Recovery (TWRP), `system`, `efs` and `modem` are not touched; don't wipe
`system` or `efs` either, postmarketOS reads the Wi-Fi board data and the sensor
calibration from them.

## 0. What you need

- The tablet with OEM unlocking on (Developer options) and able to flash custom images.
  If it runs TWRP or LineageOS, it is. Battery above 50 %.
- A Linux PC with `git`, `fastboot` and pmbootstrap
  (`git clone https://gitlab.postmarketos.org/postmarketOS/pmbootstrap.git`, then run
  `pmbootstrap/pmbootstrap.py`).
- A USB cable that carries data.

## 1. Install lk2nd

lk2nd is the bootloader postmarketOS starts from; it goes in the boot partition and keeps
its first 4 MiB, with postmarketOS's boot image behind it.

- SM-T820: `lk2nd-samsung-gts3lwifi.img` from this repository's releases (`lk2nd-v1`).
- SM-T825: no lk2nd is released yet. Keep one you already have, or build one with
  `scripts/pack-lk2nd-samsung.sh` ([lk2nd.md](lk2nd.md)).

Flash it to **boot**, one of:

- TWRP: `adb push lk2nd-samsung-gts3lwifi.img /sdcard/`, then on the tablet Install,
  Install Image, the file, partition *Boot*, swipe, and reboot.
- Download mode (Power + Volume Down + Home from off, then Volume Up):
  `heimdall flash --BOOT lk2nd-samsung-gts3lwifi.img`

You should see lk2nd's menu, waiting in fastboot. On the PC, `fastboot devices` shows one
line.

## 2. Our packages in pmbootstrap

Run `pmbootstrap init` once and accept the defaults, so it downloads pmaports. Then put
our packages over it (`-b android` instead of `-b main` for the Android kernel):

```sh
git clone -b main https://github.com/wh1ter0z3/gts3lwifi-mainline
R=$PWD/gts3lwifi-mainline/pmaports
P=$(pmbootstrap config aports)
# our device package (new) and kernel package (replaces pmaports' own)
rm -rf "$P/device/testing/device-samsung-gts3lwifi" "$P/device/testing/linux-postmarketos-qcom-msm8996"
cp -r "$R/device-samsung-gts3lwifi" "$R/linux-postmarketos-qcom-msm8996" "$P/device/testing/"
mv "$P/device/testing/linux-postmarketos-qcom-msm8996/APKBUILD.pmaports" "$P/device/testing/linux-postmarketos-qcom-msm8996/APKBUILD"
rm "$P/device/testing/linux-postmarketos-qcom-msm8996/APKBUILD.diff"
# our versions of these replace pmaports' own, wherever pmaports keeps them
for p in kwin libcamera iio-sensor-proxy gnss-share; do
  d=$(dirname "$(find "$P" -path "*/$p/APKBUILD" | head -1)")
  [ "$d" = . ] && d="$P/temp/$p"
  rm -rf "$d" && mkdir -p "$(dirname "$d")" && cp -r "$R/$p" "$d"
done
```

`kwin` here is postmarketOS's systemd KWin with our rotation fixes; it must replace the
one in `extra-repos/systemd/kwin`, which the loop finds.

## 3. Configure

`pmbootstrap init` again, and choose: channel **edge**; vendor **samsung**, device
**gts3lwifi** (also for the SM-T825: one build runs on both models); user interface
**plasma-desktop**; **systemd: yes**; your user name and the rest as you like.

## 4. Build the image

```sh
pmbootstrap install --add device-samsung-gts3lwifi-waydroid
```

It builds the kernel first, which takes a while. Leave out `--add ...` if you don't want
Waydroid. It asks for the user's password.

## 5. Flash

With the tablet in lk2nd's fastboot (as after step 1):

```sh
pmbootstrap flasher flash_rootfs     # writes userdata: Android's data is erased
pmbootstrap flasher flash_kernel     # writes the boot image behind lk2nd; lk2nd stays
fastboot reboot
```

## 6. First boot

It boots to the Plasma login. On the first boot the tablet copies its own firmware (Wi-Fi,
GPU, audio, modem) from its Android partitions (`gts3-firmware`). If Wi-Fi or sound is
missing on the very first boot, reboot once.

## 7. Optional: boot straight into Android (Waydroid)

Needs `device-samsung-gts3lwifi-waydroid` (step 4, or later with
`pmbootstrap sideload device-samsung-gts3lwifi-waydroid`). Details: [waydroid.md](waydroid.md).

1. Download Android once, on the tablet: `sudo waydroid init` (about 1 GB; if the download
   breaks, see "Downloading the images" in [waydroid.md](waydroid.md)).
2. Try it once: log out and pick the session "Android (Waydroid)" on the login screen.
   Leave the tablet alone during Android's first boot, about 5-6 minutes.
3. Log in to it automatically at every boot (the login manager of the systemd Plasma
   flavour is plasma-login-manager; replace `user` with your user name):

   ```sh
   sudo mkdir -p /etc/plasmalogin.conf.d
   printf '[Autologin]\nUser=user\nSession=waydroid-fullscreen.desktop\nRelogin=false\n' |
       sudo tee /etc/plasmalogin.conf.d/50-waydroid-autologin.conf
   ```

   There is no password prompt at boot then.
4. Back to Plasma: power Android off from its menu, which ends the session at the login
   screen, or remove that file.

## 8. Later: kernel updates, and switching between main and android

lk2nd boots the copy of the boot image in the boot partition, not the one in `/boot`.
After a new kernel is installed, write it there again, on the tablet:

```sh
grep PARTNAME=boot /sys/class/block/mmcblk0p28/uevent   # must print PARTNAME=boot
sudo dd if=/boot/boot.img of=/dev/mmcblk0p28 bs=1M seek=4 conv=fsync
```

or from lk2nd's fastboot with `pmbootstrap flasher flash_kernel`.

To switch kernels, copy the kernel package of the other branch as in step 2 (the
`rm`/`cp`/`mv`/`rm` lines for `linux-postmarketos-qcom-msm8996`), then
`pmbootstrap build --force linux-postmarketos-qcom-msm8996`,
`pmbootstrap sideload linux-postmarketos-qcom-msm8996`, and the `dd` above.

## Back to Android

TWRP is still in recovery (Power + Volume Up + Home from off). Flash a ROM and format
data.
