# Vendor-side patches

Changes to AOSP sources that only end up in the vendor image (or, for
`build/make`, only in how the vendor image is built): a GSI's system image is
never modified, so nothing here may be needed on the system side.

All of them come from SamuraiArtem's
[android_patches_j5xnlte](https://github.com/SamuraiArtem/android_patches_j5xnlte)
(Galaxy J5 2016 on mainline MSM8916, LineageOS 22.0, Apache-2.0), taken as they
are; they were written against LineageOS 22.0 and may need rebasing on AOSP 15.

| Patch | Project | What for |
|---|---|---|
| 02 | external/drm_hwcomposer | MDP5: plane flattening, VSync worker fixes, solid fill (the same display controller as ours) |
| 03 (audio part) | hardware/interfaces | AIDL default audio HAL: card/PCM device properties for the primary module, service registration retries; the software Gatekeeper's file context |
| 05 | external/minigbm | the msm backend's AIDL allocator service and mapper |
| 07 | build/make | `BOARD_API_LEVEL_PROP_OVERRIDE` (see BoardConfig.mk), where the build doesn't have it yet |
| 09 | system/media | ALSA device proxy (libalsautils, used by the audio HAL) |
| 14 | external/libdrm | root headers for vendor modules |
| 16 | external/tinyalsa | tinymix and friends on the vendor partition |

Not taken from that series: his Mesa (we use upstream Mesa 26), and every
system-side change (system/core, frameworks/*, linkerconfig, recovery,
Connectivity, build/soong).

`./apply.sh [--check] [<aosp top>]`
