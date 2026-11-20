# TrustZone on mainline (2026-11-20)

Summary of a full research day on QSEE TrustZone features — keymaster,
Widevine L1, fingerprint — running on the Tab S3's own signed TrustZone
applets (TAs) from a fully open vendor (mainline kernel 7.3.1, A16 GSI,
open `libQSEEComAPI` with dma-heap).

The earlier verdict "mainline lacks QSEECOM app loading, TZ features not
feasible" is **obsolete**. Our open dma-heap `libQSEEComAPI` loads and
operates the TAs. No public prior art for this was found (see below).

## What works

| Piece | Status | Notes |
|---|---|---|
| TA load (widevine, skeymast, dualfp, authhat, cmnlib/64) | works | `QSEECom_start_app` rc=0 through our open lib |
| Widevine TA operate | works | cmd 0x402 (ver 2.4) + 0x405 (caps 1), status=0 |
| Keymaster RoT handshake | works | SET_ROT, SET_BOOT_STATE, MILESTONE_CALL accepted from userspace |
| Keymaster add_rng_entropy | works | round-trips through the TA cleanly |
| 32-bit libQSEEComAPI | works | `void *` → `uint64_t` fix for compat_ptr_ioctl; both arches proven |
| hwservicemanager on A16 GSI | works | VINTF manifest wake recipe (see below) |
| A9/A16 ABI isolation | works | LLNDK set from A16, rest from A9, no second-libc crash |
| 32-bit Widevine HIDL service | works | reaches `joinThreadPool`, no crash, HwBinder threadpool created |
| qseecomd listeners (7 of 8) | works | must run as root (SSD partition) |
| Fingerprint HAL → sensor | works | ET510 power/clocks/interrupt through /dev/esfp0 |

## What does not work (yet)

| Feature | Symptom | Cause | Next step |
|---|---|---|---|
| Keymaster generate_key | returns -8 for all key types (AES/HMAC/RSA/EC) | TA-internal precondition; not the RoT handshake (all three accepted) | trace from a working downstream device |
| Fingerprint dualfp commands | returns 42 on every cmd | BLSP1 QUP6 → TZ SPI ownership handoff missing in mainline driver | port the downstream SCM ownership call |
| Widevine L1 playback | untested | secure video path (secure heaps, Venus secure, secure display) absent on mainline msm8996 | L1 reporting likely works; playback needs kernel work |

## Keymaster protocol (from lk2nd source)

The MSM8996 bootloader provisions keymaster via three TZ commands
(`km_main.h`, `boot_verifier.c` in the LK base at `platform/msm_shared/`):

| Command | ID | Purpose |
|---|---|---|
| GET_VERSION | 0x200 | returns major=2, minor=0, TA=2.24 |
| SET_ROT | 0x201 | latch Root-of-Trust; SHA-256 of unlock-state (± OEM key); one-shot per boot |
| SET_BOOT_STATE | 0x208 | boot colour + OS version + security patch level (45-byte struct) |
| MILESTONE_CALL | 0x204 | "bootloader finished" signal |
| ADD_RNG_ENTROPY | 0x107 | seed the TA's RNG |
| GENERATE_KEY | 0x108 | generate a hardware-backed key |

Stock boot order: `set_os_version` → `send_rot_command` (SET_ROT then
SET_BOOT_STATE) → `send_milestone_call_to_tz` → jump to kernel.

Our lk2nd+mainline chain leaves the keymaster RoT **unset** (S-Boot does
not provision this TA). Userspace **can** set it — no lk2nd rebuild needed.
Repeat SET_ROT returns -1/-15: the TA latches once per boot, as designed.

The -8 is **not** caused by the RoT handshake (all three accepted with
status=0, -8 unchanged). It is param-independent (four algorithms), and the
kernel transport is clean (zero scm/qseecom errors). The signed `skeymast`
TA is not disassemblable, so the reason cannot be read directly. The
identical keymaster blob stack generates keys on LineageOS 24 for this
tablet (downstream 3.18 kernel), so the -8 is specific to something mainline
does differently that the TA checks internally. A QSEECom command trace from
that working setup is the most direct way to find it.

## Widevine TA protocol (from liboemcrypto disassembly)

Extracted by disassembling the 64-bit `liboemcrypto.so`
(`qsappsver_getTzVersion` at 0x11bf0, `qsapps_get_capabilities` at 0x11d04):

```
QSEECom_send_cmd(handle, req=4B {u32 cmd_id}, resp=12B)
  resp layout: [0] echo  [4] value  [8] status (0=ok)

cmd 0x402 = getTzVersion   → resp[4]=0x00020004 (ver 2.4), status=0
cmd 0x405 = get_capabilities → resp[4]=1, status=0
```

No listeners required (unlike keymaster). Did not need liboemcrypto itself
(its `vendor.display.config@1.0` dep needs stock libhidltransport's
`gBnConstructorMap`, dropped by the A16 GSI; force-loading stock A9 libs
in the A16 process segfaults at C++ ctors).

## hwservicemanager wake on A16 GSI

The A16 GSI ships hwservicemanager as a deprecation stub. It checks for
exactly one interface — `android.hidl.manager@1.2::IServiceManager/default`
— in the merged VINTF manifests. If absent, it logs "HIDL is not supported"
and shuts down.

**Wake recipe:** add a vendor VINTF fragment at
`/vendor/etc/vintf/manifest/` (chcon `vendor_configs_file`) declaring that
interface:

```xml
<manifest version="2.0" type="device">
  <hal format="hidl">
    <name>android.hidl.manager</name>
    <transport>hwbinder</transport>
    <version>1.2</version>
    <interface>
      <name>IServiceManager</name>
      <instance>default</instance>
    </interface>
  </hal>
  <!-- then your actual HIDL HAL entries -->
</manifest>
```

**Caution:** enabling hwservicemanager at boot before any HIDL HAL actually
registers blocks `sys.boot_completed` (it churns on every dead
framework-declared HIDL HAL). Gate the enable on `sys.boot_completed=1` or
on successful HAL registration.

## 32-bit libQSEEComAPI fix

The mainline qseecom driver (patch 0192) uses `u64` pointer fields with
`.compat_ioctl = compat_ptr_ioctl`, so ioctl numbers are identical across
architectures. But `QSEEComAPI.c` used `void *` (4 bytes on 32-bit, wrong
size / wrong ioctl number).

Fix: changed the three pointer-carrying ioctl structs
(`qseecom_register_listener_req`, `send_cmd_req`, `set_sb_mem_param_req`)
from `void *` to `uint64_t`, assignments to `(uint64_t)(uintptr_t)ptr`.
Byte-identical on 64-bit (no regression). Android.bp:
`compile_multilib: "both"` → installs `/vendor/lib` (ELF32) +
`/vendor/lib64` (ELF64).

## A9 / A16 ABI isolation

Stock Android 9 (Pie) 32-bit service binaries crash immediately in an A16
process: the A9 `libc.so` from `LD_LIBRARY_PATH` segfaults in
`__libc_init_globals` (second libc).

Fix: stage all stock 32-bit libs, delete only the LLNDK set (`libc.so`,
`libm.so`, `libdl.so`, `ld-android.so`, `libdl_android.so`) — these must
come from the host A16 process. Keep A9 `liblog.so` (A16's needs
`__libcpp_verbose_abort` from A16 `libc++`, which A9 `libc++` lacks).

## qseecomd listeners

Seven listeners register, **only as root**. As `shell` (uid 2000),
`libssd` opens `/dev/block/bootdevice/by-name/ssd` (mmcblk0p34, mode
`brw------- root root`) and fails; qseecomd then aborts all listeners.
Run as root or in the proper vendor init domain. `libspl.so` not-found is
non-fatal.

| Listener | ID | Status |
|---|---|---|
| RPMB | 0x2000 | ok |
| SSD | 0x3000 | ok (root only) |
| FS | 0x000a | ok |
| GPFS | 0x7000 | ok |
| QISL | 0x9000 | ok |
| GPreqcancel | 0x1100 | ok |
| Time | 0x000b | ok |
| SPL | — | libspl.so absent (non-fatal) |

## Prior art

Searched for any public instance of Qualcomm TZ features (keymaster,
Widevine L1, fingerprint TZ HAL) on a fully open vendor with a mainline or
non-OEM kernel:

- **postmarketOS**: software keymaster, Widevine L3, no fingerprint TZ.
- **LineageOS / custom ROMs**: use the OEM's downstream kernel + proprietary vendor. Not open.
- **Halium / Ubuntu Touch**: Android HAL container with stock vendor; no TZ integration of their own.
- **GloDroid / Treble GSI**: generic vendor, software keymaster, L3 Widevine.
- **Qualcomm Landing Team (Linaro)**: mainline enablement focuses on display/GPU/modem/audio.
- **Freedreno / msm-mainline community**: GPU/display focus; no qseecom/TZ HAL work.

**No public prior art found.** The open dma-heap `libQSEEComAPI`
(ION → dma-buf) is the enabler — no existing project had one.

## Verdict

Widevine L1 reporting and fingerprint are within reach — the TA operates
and the HIDL service runs. Keymaster's `generate_key` hits an opaque
TA-internal refusal that needs a downstream trace to diagnose. All three
are parked in favour of deep sleep and usability work.

HD Widevine playback additionally needs the secure video path (secure heaps,
Venus secure mode, secure display) which mainline msm8996 lacks. L3 already
works (Waydroid + Flatpak Firefox from Google's official sources; see
[widevine.md](widevine.md)).
