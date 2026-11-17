# Galaxy Tab S3 vendor (mainline kernel, Android 15)

An open vendor image for the Galaxy Tab S3 (SM-T820 `gts3lwifi`, SM-T825
`gts3llte`, MSM8996) on the mainline Linux kernel, for unmodified Android
system images (GSIs): AOSP/TrebleDroid, LineageOS, OneUI ports.

- **Vendor only.** No boot, recovery or system image is built here: lk2nd and
  our boot ramdisk (gsi-init, in `gsi/` of wh1ter0z3/gts3lwifi-mainline) start
  Android with the system image the user downloads, this vendor image, and
  `/data`, all files on the userdata partition.
- **Open source only.** Nothing from Samsung or Qualcomm is in it. The
  tablet's own firmware (GPU zap shader, Venus, ADSP, SLPI, Wi-Fi board data,
  ...) stays on its partitions; gsi-init mounts them read only.
- **No system-side changes.** A GSI is used as it is, so everything the vendor
  needs is on the vendor side. `patches/` holds the few changes to AOSP sources,
  all vendor side.
- **Android 15 vendor (target-level 202404)**, presented to GSIs as VNDK 34 so
  that Android 15, 16 and 17 system images boot on it.

Built on the shoulders of SamuraiArtem's Galaxy J5 2016 tree (mainline
MSM8916, LineageOS 22), the first to run GSIs on a mainline-kernel vendor of
this kind; see NOTICE for what comes from it.

## What is in it

| Part | Implementation |
|---|---|
| GPU | upstream Mesa 26.3 freedreno (EGL, GLES 3.1) |
| Display | drm_hwcomposer (AIDL composer3) on MDP5 |
| Buffers | minigbm, msm backend (allocator AIDL, mapper 5) |
| Video | Venus through AOSP's v4l2_codec2, our AIDL service: H.264 decode and encode |
| Audio | AIDL default HAL on the q6 card: speakers, built-in mics |
| Wi-Fi | wpa_supplicant/hostapd on nl80211 (ath10k), no vendor Wi-Fi HAL |
| Bluetooth | AIDL HAL on the HCI user channel (QCA6174) |
| Battery | our health HAL (`hal/health`), with Samsung's ISehHealth for OneUI |
| Lights | our lights HAL (`hal/lights`): backlight, touch key LED |
| Sensors | AOSP's AIDL multihal with our IIO sub-HAL (`hal/sensors`): accelerometer, gyroscope, magnetometer, light, from the SLPI |
| Camera | libcamera 0.8.0 (Nura's patch series plus `camera/libcamera-patches/01xx`) with its Android HAL3 module under STMicroelectronics' AIDL provider: IMX258 rear (autofocus), S5K5E3 front; preview and JPEG together from one software-ISP stream (rear ~8 fps preview with a 13 MP JPEG stream, front ~14 fps) |
| GNSS | our AIDL HAL (`hal/gnss`) on the modem DSP's QMI LOC service; Linaro's rmtfs and tqftpserv run the modem. NMEA and engine tested indoors; a fix at a window still to test |
| USB | AOSP port HAL; adb/MTP/PTP gadget on configfs |
| Security | software KeyMint and Gatekeeper |
| DRM | ClearKey |
| OneUI | stand-ins for Samsung HALs its system_server waits for (`hal/samsung`) |

To do, each tested on the tablet before it goes in: the hall sensor (cover),
headphone/headset routing, Bluetooth audio, camera speed (a GPU software ISP) and rotation check, Vulkan (turnip on a5xx),
deep sleep, SELinux enforcing (vendor policy for all of the above), kernel
modules and firmware links from this build instead of
`gsi/tools/mkvendor-standalone.sh`.

## Building

On a Linux PC with 64 GB of RAM or more and 300 GB free (or a cloud machine),
Ubuntu 22.04, the AOSP build packages, plus `meson`, `mako` and `ply` for Mesa:

```bash
repo init -u https://android.googlesource.com/platform/manifest -b android-15.0.0_r36 --depth=1
git clone https://github.com/wh1ter0z3/gts3lwifi-mainline ../gts3lwifi-mainline
mkdir -p .repo/local_manifests && cp ../gts3lwifi-mainline/android/device-gts3/manifests/gts3.xml .repo/local_manifests/
repo sync -c -j8 --no-tags
mkdir -p device/samsung && cp -a ../gts3lwifi-mainline/android/device-gts3 device/samsung/gts3
device/samsung/gts3/patches/apply.sh
source build/envsetup.sh
lunch aosp_gts3-bp1a-userdebug
m vendorimage
```

`out/target/product/gts3/vendor.img`, then `gsi/tools/mkvendor-standalone.sh`
for the kernel modules and the firmware links of one kernel build.

First-build checks (unknowns until the tree has been built once):
- upstream Mesa beside AOSP's `external/mesa3d` (gfxstream subset, no name
  clashes): settled on the first build;
- the J5 patches were made on LineageOS 22.0 and may need rebasing;
- health HAL v1 at target-level 202404 (else v3);
- the release config: `bp1a` on android-15.0.0_r36.
