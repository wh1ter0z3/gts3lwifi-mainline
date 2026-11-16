# Vendor-side patches

Changes to AOSP sources that only end up in the vendor image (or, for
`build/make`, only in how the vendor image is built): a GSI's system image is
never modified, so nothing here may be needed on the system side.

Most of them come from SamuraiArtem's
[android_patches_j5xnlte](https://github.com/SamuraiArtem/android_patches_j5xnlte)
(Galaxy J5 2016 on mainline MSM8916, LineageOS 22.0, Apache-2.0), taken as they
are; they were written against LineageOS 22.0 and may need rebasing on AOSP 15.

| Patch | Project | What for |
|---|---|---|
| pending-02 | external/drm_hwcomposer | MDP5: plane flattening, VSync worker fixes, solid fill (the same display controller as ours). Made for LineageOS 22's copy; doesn't apply to AOSP 15's yet: rebased for the real-screen phase |
| 03 (audio part) | hardware/interfaces | AIDL default audio HAL: service registration retries, the effects configuration in its APEX. (AOSP 15 already picks the PCM from the device port's address and labels the software Gatekeeper, so those hunks are gone) |
| 04 (ours) | hardware/interfaces | AIDL Bluetooth HAL: unblock rfkill before binding the HCI user channel. AOSP blocks it there, and mainline Linux refuses to open an rfkilled controller (ERFKILL), so Bluetooth never came up (found on the T820, 2026-11-16) |
| 05 | external/minigbm | the msm backend's AIDL allocator service and mapper |
| 09 | system/media | ALSA device proxy (libalsautils, used by the audio HAL) |
| 14 | external/libdrm | root headers for vendor modules |
| 16 | external/tinyalsa | tinymix and friends on the vendor partition |

Not taken from that series: his Mesa (we use upstream Mesa 26), and every
system-side change (system/core, frameworks/*, linkerconfig, recovery,
Connectivity, build/soong).

`./apply.sh [--check] [<aosp top>]`
