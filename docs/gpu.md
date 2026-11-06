# GPU: Adreno 530 under mainline

## Status (2026-10-28): working headless with freedreno

On a Tab S3 with no panel attached, booted from RAM with
`dts/test/test-gpu.dts` (merged into the main DTS on 2026-10-28) and
`msm.separate_gpu_kms=1`:

- nothing loads `msm` by itself: the module only has aliases for the
  display subsystem, which is off. The device package loads it at boot
  (`modules-load.conf`) and puts `msm.separate_gpu_kms=1` on the cmdline;
- the GPU probes as its own DRM device (`/dev/dri/renderD128`), Mesa reports
  **FD530, OpenGL ES 3.1 / OpenGL 3.1 (Mesa 26.2.5)**;
- Samsung's signed zap shader is accepted by TrustZone, firmware loads
  (`a530_pm4.fw`, `a530_pfp.fw`, `a530v3_gpmu.fw2`), no hangs or faults;
- `tests/gltri.c` (surfaceless EGL, triangle into an FBO, read back) renders
  correctly: green inside the triangle, the red clear colour outside;
- devfreq offers 133 to 624 MHz.

## What it takes

- **Power:** VDD_GFX is PM8004 S2 (downstream qpnp regulator at 0x1700,
  400 to 1015 mV, GFX CPR ceiling 1015 mV at the top corner). Mainline has no
  GFX CPR for MSM8996, so the test DTS pins it at 1015 mV, always on, like the
  Sony MSM8996 boards. `mmcc` gets `vdd-gfx-supply` for the GPU GX GDSC.
- **Firmware** (not in this repo; copy from the tablet itself, read-only):
  - `/system/vendor/firmware/a530_pm4.fw`, `a530_pfp.fw`, `a530v3_gpmu.fw2`
    to `/lib/firmware/qcom/`; linux-firmware's copies of the three
    (`linux-firmware-qcom`, which the device package depends on) work as
    well (docs/pmos-install.md)
  - Samsung's signed zap shader from the `apnhlos` partition,
    `image/a530_zap.mdt` + `.b00` to `.b02`, to
    `/lib/firmware/qcom/msm8996/samsung/gts3/` (the DTS sets that path).
    The db820c zap in linux-firmware would not be accepted by Samsung's TZ.
- **DRM device:** `msm.separate_gpu_kms=1` while MDSS is not in the main DTS,
  so the GPU binds on its own instead of waiting for the display.
- `msm` is a module; load it with `modprobe msm` (nothing autoloads it
  without the display).

## Faults when buffers are shared between processes (fixed, 0122-0123)

Under Plasma, rotating the screen gave bursts of GPU faults
(`*** gpu fault: ... dir=READ type=TRANSLATION`), most often with the lock
screen up. A run of 8 rotations caught 1822 faults, in 7 bursts.

The A5xx has no per-process page tables, so every DRM file shares one GPU
address space (`gpu->vm`). Since v6.17 (111fdd2198e6, "drm/msm: drm_gpuvm
conversion"), `msm_gem_close()` unmaps a buffer from that shared VM as soon
as any one process closes its handle. A dma-buf imported by another
process keeps the old address, and the next allocation reuses it. Kprobes
on `msm_gem_vma_unmap` and the fault handler showed every burst hitting a
14.7 MB buffer that `kscreenlocker_greeter` had unmapped ("close") 0-33 ms
earlier, while the GPU ran a KWin submit that still sampled it.

Dmitry Baryshkov's series "drm/msm: fix dma-buf sharing on targets without
per-process pgtables" (v1, 2026-08-22, not merged yet; its 2/2 names
A530/MSM8996) moves the teardown on shared VMs to the last reference drop.
Patches 0122-0123 carry it; 7.3.1 does not have it yet.

Tested on the tablet: the same 8 rotations gave 0 faults without the lock
screen (929 submits) and with it (905 submits); four later runs also had 0.
No GPU hangs or recoveries.

## Memory clock (0170)

The GPU has an interconnect path to DDR, but its OPPs had no bandwidth to ask for. So
it asked for nothing, and DDR ran at what the display asks: 800 MHz of a possible 1804.
Patch 0170 gives each GPU clock the level the stock kernel has for it (`qcom,bus-freq`).
It's device tree only, in `msm8996.dtsi`, so every MSM8996 board in this package gets it.

SM-T820, 7.3.1, screen on, USB plugged in. `glmark2-es2-wayland --off-screen -s 1920x1080`,
two boots each way, alternating, three rounds per boot:

| Scene | fps before | fps after | |
|---|---|---|---|
| desktop, 4 windows, blur | 87 | 111 | +27 % |
| refract | 35 | 45 | +26 % |
| terrain | 24 | 30 | +23 % |
| texture | 275 | 329 | +20 % |
| effect2d, 5x3 kernel | 150 | 162 | +8 % |
| shading, phong | 292 | 324 | too noisy to call: 275 to 418 |

| Cost | Before | After |
|---|---|---|
| Battery current over those scenes | 632 mA | 744 mA |
| Peak GPU temperature over them, about 4 min | 59 C | 66 C |
| GPU idle | vote 0 | vote 0 |

Terrain for 10 minutes on it: 29 to 31 fps throughout, with no slowdown. The GPU levels off at
76 C, and its trip is 90 C. No GPU faults or hangs in any of these.

The stock kernel also moves this level up and down at runtime, from the GPU's memory
wait counters. This doesn't, so it can hold DDR higher than stock would, and a
workload that doesn't need the bandwidth still pays for it.

In a single run of opening Plasma panels, KWin's GPU time per frame went from 45-49 to about
32 ms, summed over its jobs, but frame pacing was no better. Cold popups are mostly CPU.

Not tested: a T825, a built package, sleep current. The deep sleep test kernels carried this
patch and still reached VDD minimisation.

## Next

- Move the GPU nodes into the main DTS once the regulator choice is settled
  (check whether a lower pinned voltage with capped OPPs is stable).
- Run the db820c dEQP-GLES31 baseline from Mesa CI on the tablet.
- Vulkan: see docs/vulkan-a5xx.md (this was milestone M0 there).

Known issue found on the way: rebooting from Linux hung (PSCI
`SYSTEM_RESET` never returns from Samsung's TrustZone). Fixed in the board
DTS, see docs/reboot.md.
