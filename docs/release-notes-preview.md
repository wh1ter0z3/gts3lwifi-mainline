# postmarketOS preview for the Galaxy Tab S3: release notes

postmarketOS edge with Plasma (Wayland, systemd) and Linux 7.3.1, for the SM-T820 and
SM-T825. Install: [install-preview.md](install-preview.md).

## Warnings

- **This erases the tablet.** Installing replaces userdata, so all of Android's data is
  gone, and the boot partition, so Android stops booting.
- **Knox and warranty.** Flashing anything unofficial trips the Knox flag for good.
  Samsung's warranty and Knox features (Secure Folder, Samsung Pay) are gone. If you
  already run TWRP or LineageOS, this already happened.
- **This is a preview.** One person develops on one SM-T820. Expect crashes and rough
  edges. Don't keep anything on it you can't lose.
- **Not yet run on hardware.** When this was written, this image had been built and
  checked offline, not booted on either model. That includes the login steps below. Test
  results get added here.
- **Default password.** The image has one user, `user`, with the password `147147`, which is
  public. It shows a prompt to change it at each login until you do. There is no
  autologin. Root has no password, and SSH is off until you turn it on. Please change the
  password before connecting to a network you don't trust.
- **SM-T825: no modem.** No LTE and no SIM. GPS is off on this model too. **Nobody has
  booted Linux 7.3.1 on an SM-T825**, only 6.19. The lk2nd for the SM-T825 in this release is
  a new build that has not been run either; an lk2nd you already have works too.
- **Suspend and battery.** Suspend is s2idle only, no deep sleep. It passed 4 of 4 cycles of
  20 s on the SM-T820. Battery life in suspend hasn't been measured, so don't count on
  it lasting. Plasma's idle timer suspends after a while, and until the Power key wakes
  it the tablet has no USB or Wi-Fi.
- **Don't update.** Don't run `apk upgrade` or accept updates in Discover. This image
  mixes our packages with postmarketOS edge. An update can replace ours with stock ones
  as soon as those have a higher version, and KWin will at 6.8.3. A new kernel also needs
  a manual step to take effect ([details](install-preview.md#after-a-kernel-update)).
  Installing new apps with `apk add` can pull updates too.
- **Crashes can freeze the tablet.** Core dumps are on, as in any postmarketOS install.
  A big one (1 GB) froze and reset our tablet. We turned them off on ours; this image
  hasn't.

## What's reachable on the network

Read from the image's configuration, not yet measured on a running tablet. SSH is off and
has no host keys until you turn it on. These should be running: `avahi-daemon` (mDNS),
`unudhcpd` (DHCP for the USB network), `systemd-resolved` (DNS for the tablet itself),
NetworkManager. CUPS only has a local socket. The firewall drops what it doesn't know on
Wi-Fi, and accepts everything on the USB network, as on any postmarketOS install. So
whoever holds the USB cable can reach whatever runs, and the default password is one more
reason to change it. KDE Connect runs in your session and opens its own ports when you
use it.

## Known broken or missing

| What | State |
|---|---|
| LTE, SIM (SM-T825) | Not supported |
| GPS (SM-T825) | Off until tested |
| Home key | Does nothing in Plasma: the mapping isn't in this image's packages yet. Use the application menu in the panel |
| Back and Recent touch keys | Not yet |
| Headphone jack detection, headset mic | Not yet. Speakers, headphones and the built-in mics work |
| Top speakers (SM-T825) | Left out until checked. Bottom pair, headphones and mics are on |
| Fingerprint | Blocked: its pins belong to Samsung's TrustZone |
| Cover (hall) sensor, grip sensor (SM-T825) | Not yet |
| Compass heading | Not yet |
| Microphone level | Quiet, peaks near 5 %: gain never tuned |
| Camera app | None in this image. The kernel and libcamera side work. Plasma Camera, from the repositories, is broken on this tablet (wrong resolution, smeared photos, empty video, crash when switching cameras) |
| Bluetooth | On some boots there's no controller (`Opcode 0x0c03 failed: -110` in the log). Bring it back with `sudo rmmod hci_uart; sudo modprobe hci_uart` |
| Kernel log | `pclk0_clk_src: rcg didn't update its configuration` on most boots. Looks harmless: the picture was right on a boot with it |
| DNS on some routers | On our home router, systemd-resolved's DNSSEC check fails and no names resolve. We turned the check off by hand; this image has the default. Fix: [the install guide, step 6](install-preview.md#6-reboot-once) |
| Wi-Fi power save | NetworkManager's default is on. On our tablet that gave replies of 84 to 205 ms when idle, and once missed ARP requests. Off: `nmcli connection modify "NAME" 802-11-wireless.powersave 2`, then reconnect. No fix that keeps it on yet |
| Time zone | The image is set to GMT. Change it in System Settings |

## What works, and what it was tested on

All of this is from the SM-T820, from [the README](../README.md) and
[kernel-7.3.md](kernel-7.3.md), most of it on the 7.3.1 kernel package this image builds,
on a system installed and modified by hand over several weeks. This image differs: it
has Plasma 6.8.2 and KWin 6.8.2, where our tablet has 6.8.1. The SM-T825 has booted to KDE on
earlier kernels, nothing more is known.

| Area | README | Hand checks on 7.3.1 |
|---|---|---|
| Boot, cpufreq, thermal, eMMC, microSD, reboot, power-off | Works | 13 of 13 boots clean; the rest not rechecked |
| Display, brightness, rotation | Works | picture and brightness by eye, rotation by hand |
| GPU (Adreno 530, freedreno) | Works | KWin running |
| Touch, S-Pen, keyboard cover | Works | by hand |
| Power, Volume keys | Works | Power: sleeps and wakes twice in a row |
| Wi-Fi 2.4 and 5 GHz | Works | passes traffic after suspend |
| Bluetooth | Works | stays powered after suspend; see the timeout above |
| USB device mode | Works | gadget running |
| USB host, USB 3, USB-C power | Works | not rechecked |
| GPS through geoclue (SM-T820) | Works | first fix after 80 s at a window, probably with orbit data kept from earlier fixes. README: 2.5 to 8 min after boot |
| Battery gauge, charging | Works | not rechecked |
| Speakers, headphones, mics | Works | by ear; mic quiet |
| Venus video | Partial | H.264 and HEVC decode match software. Encode not rechecked |
| Cameras | Works | both capture, in Ocellus, which this image doesn't have |
| Sensors | Works | accelerometer, gyroscope, magnetometer, light, ADC; orientation reads normal |
| Waydroid, Widevine | Optional extras, not in this image | not rechecked |

## What we'd like you to check

Tell us for each: works, doesn't work, or not tried.

1. Which install route you took, and whether the guide was right at each step.
2. Did `fastboot boot` from RAM work (step 3 of the guide)?
3. Did it boot from flash to the login screen? About how long did the first boot take?
4. Tapping the password field: did the on-screen keyboard appear? Did `147147` work?
5. Did the *Set your own password* window appear? Did changing the password work, and
   did the window stay away after the next login?
6. After the reboot: Wi-Fi, Bluetooth, audio from the speakers.
7. Touch, S-Pen, rotation, keyboard cover if you have one.
8. Does suspend work with the Power key, and does it wake? How much battery did it lose
   over a night?
9. Anything that crashed or looked wrong.

### Send us the logs

In Konsole, once it has been up a few minutes:

    sudo sh -c '{ uname -a; cat /proc/device-tree/model; echo; grep " / " /proc/mounts; ls -R /lib/firmware/updates; systemctl --failed; dmesg; journalctl -b --no-pager; } > /tmp/gts3-report.txt 2>&1; chmod 644 /tmp/gts3-report.txt'

Then copy `/tmp/gts3-report.txt` off the tablet. **Read it before you share it**: it has
your Wi-Fi network names, MAC addresses and serial numbers. Remove what you'd rather not
post. Please don't send any GPS coordinates. If it doesn't get to a desktop, send what you
saw on screen, and lk2nd's log:

    fastboot oem log
    fastboot get_staged lk2nd.log

### SM-T825: the main question

The project moves to Linux 7.3 if it boots on both models, and so far it hasn't been
tried on an SM-T825. Your first report matters even if nothing else works. Please tell us:

- **Which lk2nd:** the one from this release, or the one you had. Did fastboot show up on
  USB with it?
- **RAM boot:** does `fastboot boot pmos-gts3-preview1-boot.img` bring up the USB network?
  Does the tablet reset by itself, and after how many seconds? A pin that Samsung's
  TrustZone owns resets the tablet about 9 s into boot.
- **Device tree:** what `cat /sys/firmware/devicetree/base/model` says. It should be
  "Samsung Galaxy Tab S3 LTE".
- **Boot to the desktop:** yes or no, and the log above.
- If you flashed the image, whether the modem stayed quiet: `dmesg | grep -i -E "mss|modem|remoteproc"`.

## How this image was built

`scripts/build-preview.py`, from the commits in `scripts/preview/lock.json`.
`manifest-apks.txt` in the release lists every installed package with its version.
It doesn't contain any Samsung firmware: the tablet copies its own at first boot. lk2nd's
images do contain the device trees of an Android boot image for that model, because Samsung's
bootloader doesn't start it without them.

Alpine and postmarketOS edge change daily, and their mirrors keep only the newest
packages. So a later build from the same lock gets other versions of what isn't ours;
it reports those as differences from the manifest rather than hiding them.
