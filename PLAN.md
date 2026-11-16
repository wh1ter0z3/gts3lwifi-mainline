# Plan: the open vendor for the Galaxy Tab S3, and LineageOS on it

Written 2026-11-16. The order is the point: each phase has an exit test, and
nothing moves to the next phase until it passes on a tablet.

## 1. What we make

| Product | Source | For whom | When |
|---|---|---|---|
| **A. The open vendor** (`vendor.img` + our boot image + installer) | AOSP 15 (`android-15.0.0_r36`) + this tree, product `aosp_gts3` | people who want any GSI: AOSP/TrebleDroid, LineageOS GSI, OneUI ports | first |
| **B. LineageOS 24 for the Tab S3** (full ROM: system + vendor + recovery, OTA zip) | LineageOS 24 (Android 17) + this tree, product `lineage_gts3` | people who want a finished, updatable ROM | after A is stable |

One device tree for both. The vendor code is the same; only the Android
level it's built at differs:

- **A stays an Android 15 vendor.** Treble only promises that an *older*
  vendor works with a *newer* system. An Android 15 vendor boots Android 15,
  16 and 17 systems; an Android 17 vendor would only take Android 17 ones, and
  OneUI 8 is Android 16. So the vendor people download for GSIs is always the
  AOSP 15 build.
- **B builds its own vendor from the LineageOS 24 tree**, at Android 17 level,
  for its own system only. That's the normal way a ROM is built, and it also
  shakes out anything in our code that depends on the Android 15 level.

Rules that don't change:
1. Open source only. The tablet's firmware stays on its own partitions
   (gsi-init mounts them); nothing from Samsung or Qualcomm is built in.
2. No change to the system side. A GSI is used as downloaded. `patches/`
   touches vendor-side sources only.
3. Every change is tested on a tablet before it goes in, one change per
   boot; the tester with a screen confirms what the bare board can't.
4. Credit: SamuraiArtem's J5 tree for the method and the parts in NOTICE;
   Jayden for the kernel work; testers by name only with consent.

## 2. Hardware: what works where, and what the vendor needs for it

"Kernel" = mainline 7.3 under postmarketOS (what the drivers can do).
"Android now" = the GloDroid-based vendor of the oneui-test1 release.
"Plan" = the component in this tree and its phase (section 3).

| Hardware | Kernel | Android now | Plan |
|---|---|---|---|
| Display 2048x1536 (MDP5, split DSI) | works | works (T825 tester) | drm_hwcomposer, phase 0; rotation check phase 1 |
| GPU Adreno 530: OpenGL ES | works (freedreno) | works, Mesa 26 | upstream Mesa 26.3 in-tree, phase 0 |
| GPU: Vulkan | no (turnip is a6xx+) | no | Jayden's A530 driver when it exists, `BOARD_MESA3D_VULKAN_DRIVERS`; phase 4 |
| Touchscreen, S-Pen | work | work (T825 tester) | kernel input, nothing in the vendor; phase 1 check |
| Touch keys Back/Recents | work (#13) | untested | key layout file if needed; phase 1 |
| Keyboard cover (POGO) | works (#16 resume fix) | untested | nothing in the vendor; phase 1 check |
| Backlight | works | works (our lights HAL) | lights HAL, phase 0 |
| Video decode H.264 | works | works (hw, Codec2 V4L2) | codec2/, phase 0 |
| Video decode HEVC, VP8, VP9 | work (#12 HEVC) | software only | add to the codec list, test; phase 2 |
| Video encode H.264 | works | works (hw) | phase 0 |
| Speakers (4) | work | work | AIDL audio HAL + mixer script, phase 0 |
| Built-in mics | work | work | phase 0 |
| Headphones, headset mic | work (#14 jack) | no | audio module with routes + jack detection, phase 2 |
| Wi-Fi (ath10k) | works | works | supplicant/hostapd on nl80211, phase 0; Wi-Fi vendor HAL (hotspot, Wi-Fi Direct) phase 2 |
| Bluetooth (QCA6174) | works | works | AIDL HAL on hci0, phase 0 |
| Bluetooth audio (A2DP, headset) | works | untested | phase 2 |
| Accelerometer, gyroscope | work (SLPI) | work | **sensors HAL over IIO, phase 2** (none in this tree yet) |
| Magnetometer | works | no | same HAL, phase 2 |
| Light sensor (auto brightness) | works | works | same HAL, phase 2 |
| Hall sensor (cover) | not yet | no | kernel first; then a sensor or lid switch, phase 3 |
| Battery, charger (SM5705), USB-C PD | work (#21 open: real battery temp) | works (our health HAL) | health HAL, phase 0; #20/#21 into the kernel |
| Torch LED | works | no | lights HAL flashlight or camera; phase 3 |
| microSD | works | untested | vold through fstab, phase 1 check |
| USB: adb, MTP | n/a | no (log shell only) | configfs gadget (init.gts3.usb.rc), phase 2 |
| USB host (OTG) | probably | untested | phase 2 check |
| GPS (T820: modem subsystem) | works (#9) | no | GNSS HAL over QMI LOC: to write; phase 3 |
| Cameras 13 MP / 5 MP | drivers probe, capture untested | no | libcamera + its Android HAL, phase 4 |
| Fingerprint (ET510) | kernel side done; the TA answers 42 | no | parked; phase 4 at the earliest |
| Deep sleep / standby | in progress (RPM vmin reached once; Wi-Fi/PCIe in suspend open) | no (held awake) | Android suspend once the kernel side is reliable, phase 3 |
| Widevine | L3 software only | L3 (system side) | L1 needs Samsung's stack: not in the open vendor |
| SELinux | permissive (gsi-init) | permissive | vendor policy, enforcing, phase 5 |
| SM-T825 LTE | modem boots, no data (IPA) | no | needs the kernel's IPA work first; RIL later |
| Vibration, NFC | not fitted | – | – |

## 3. Phases

### Phase 0: build it, same result as today
Exit: `vendor.img` from `m vendorimage` boots a TrebleDroid AOSP 16 GSI on the
bare T820 (vkms) with everything "Android now" says works: GLES, H.264 both
ways, speakers and mics, Wi-Fi, Bluetooth, battery, backlight, keys.
- Oracle VM: sync AOSP 15, apply patches, build; fix what the first build
  finds (Mesa in-tree, the J5 patches on AOSP, health HAL version).
- Record the project list the build used → cut-down manifest for the PC.
- Rewrite health and lights on the generated AIDL code (now possible
  in-tree), at the versions the matrix wants.

### Phase 1: a real Treble layout, then a real screen
Exit: a tablet repartitioned with a vendor partition boots a GSI flashed to
its system partition with our vendor flashed to its vendor partition, from a
standard Android boot image; the T825 tester confirms display, rotation,
touch, S-Pen, touch keys, keyboard cover, microSD, brightness on it.
- Partitions (one-time, a TWRP zip with sgdisk; undo = the stock PIT in
  Odin): `vendor` 1 GB, `system` grown to 8 GB (OneUI 8's system image is
  6.3 GB; stock system is 4 GB), the rest stays `userdata`.
- A standard boot image behind lk2nd: Android's first-stage init, fstab
  (system, vendor, userdata, the firmware partitions read only at
  `/vendor/firmware_mnt`, zram), modules by ueventd from `/vendor/lib/modules`.
  gsi-init's jobs move into the vendor; gsi-init stays the developer's RAM
  boot (no repartition needed) and the test build keeps a USB log shell.
- The installer copies the firmware files the stock system partition holds
  (Wi-Fi board data and the like) into the vendor partition before that
  partition is overwritten: the device's own files, nothing distributed.
- Our TWRP: awesometic's device tree and kernel plus the vendor partition and
  system-as-image flashing, so "Install Image" works for `vendor.img` and a
  GSI's `system.img`; `fastboot flash vendor` from lk2nd as the PC way.
- The orientation fix (`ro.surface_flinger.primary_display_orientation`), key
  layouts, vold.

### Phase 2: everything the kernel already has
Exit: each line below passes its VTS test on the tablet (section 5).
- Sensors HAL over IIO (accel, gyro, mag, light), AIDL.
- Audio: a gts3 module with routes (speaker/headphones, mic/headset mic),
  jack detection; Bluetooth audio.
- HEVC/VP8/VP9 in the codec list, verified bit-exact as H.264 was.
- USB adb/MTP; Wi-Fi vendor HAL for hotspot and Wi-Fi Direct.
- Kernel modules and firmware links made by this build
  (`BOARD_VENDOR_KERNEL_MODULES`, symlink modules), retiring
  `mkvendor-standalone.sh`.
- #20 (GPU huge pages) and #21 (battery temperature) in the Android kernel.

### Phase 3: standby and location
Exit: 24 h on battery in standby with Wi-Fi, wakes on the power key;
a GPS fix outdoors.
- Android suspend (SystemSuspend) on the kernel's s2idle; wakeup sources.
- GNSS HAL over QMI LOC (rmtfs + qrtr), our own; hall sensor; torch.

### Phase 4: the hard hardware
- Camera: libcamera's Android HAL (simple pipeline + software ISP first).
- Vulkan when a driver exists; fingerprint if the TA problem is solved.

### Phase 5: enforcing, release, LineageOS
Exit: SELinux enforcing with no denials in a day of use; VTS green for every
shipped HAL; soak tests passed on both models; reproducible build.
- Vendor sepolicy for all of the above; `gsi-init` stops locking permissive.
- Release A: vendor + boot images + installers, built on GitHub Actions
  from a tag, SHA256SUMS.
- Product B: `lineage_gts3` on LineageOS 24, recovery, OTA zip, on the
  phase 1 layout (system + vendor partitions).

## 4. Where it builds

| Where | What | Why |
|---|---|---|
| Oracle VM (E3.Flex 16 OCPU/128 GB, 400 GB) | the first full-tree builds, phase 0; the LineageOS 24 tree later (one tree at a time: two don't fit) | RAM and disk |
| the E7270 | cut-down tree, incremental vendor builds, phases 1-5 | daily work, free |
| GitHub Actions | release builds from tags | reproducible, public |

Only open source goes to the cloud or to Actions.

## 5. How "stable enough" is measured

- **VTS** for every HAL we ship (composer3, allocator/mapper, audio core,
  bluetooth, supplicant/hostapd, health, light, sensors, media.c2, keymint,
  gatekeeper, usb, power, drm): built from the tree, run with `atest` over adb,
  green before a HAL counts as done.
- **Soak**: 100 suspend/resume cycles; 8 h of video playback; 24 h Wi-Fi
  with reconnects; 50 boots without a failure; memory over 24 h flat.
- **Both models**: T820 (bare board + a tester with a screen), T825 (tester).
- **Two systems**: a TrebleDroid AOSP GSI and OneUI 8 for every release; the
  LineageOS GSI as the third.

## 6. Unknowns to settle early

- Mesa in-tree: removing AOSP's `external/mesa3d` may break other modules;
  if so keep AOSP's and build ours beside it.
- The J5 patches were made on LineageOS 22; they may need rebasing on AOSP 15.
- Jack detection route: extcon (system_server reads it with no system
  change) vs input events (needs a system overlay a GSI won't have).
- The health HAL version VINTF accepts at target-level 202404.
- Trial limits: 16 cores; whether the boot volume can grow for the
  LineageOS tree.
