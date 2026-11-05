# libhybris on musl (postmarketOS): parked

Goal was to run Samsung's Android fingerprint HAL (`fingerprint.default.so`
→ `libbauthserver.so` → the `dualfp` QSEE app) on Linux, to test the
fingerprint chain before Android-on-mainline exists. Parked 2026-11-05: the
kernel side is complete, and the HAL test is simpler on Android, where the
libraries run natively.

## Where it stopped

- libhybris (upstream commit in `UPSTREAM_COMMIT`) builds on musl with
  `hybris-musl.patch`, `musl_compat.c`, `hooks_musl_extra.inc` and the
  hand-written `android-headers/`. Built: `include common properties
  hardware` only.
- Every Android 9 library of the fingerprint stack loads through it
  (`hybris-probe.c`: liblog … libbinder, libbauthtzcommon, libbauthserver,
  fingerprint.default.so).
- The HAL module opens (`HMI`: "Demo Fingerprint HAL", API 2.1); see
  "Second try" for how far it gets.

## Second try (2026-11-05 evening)

With the heap lookup fixed, the test program's `fingerprint_device_t`
order corrected (`notify` comes after `set_notify`), hybris given the
stock properties through `/system/build.prop` on the host
(`ro.build.type=user`, `ro.hardware.chipname=MSM8996`,
`ro.hardware.keystore=mdfpp`) and CTD5's `authhat` app installed:

- The HAL loads three TrustZone apps through `/dev/qseecom`: `dualfp`
  (2 MB buffer), `authhat` and `keymaster`, and drives the sensor's power
  and clocks through `/dev/esfp0`. No listener requests occur.
- The kernel hands the HAL what the stock kernel does: `name` "ET510",
  `adm` "1", `type_check` "3" (the stock kernel is built without
  `CONFIG_SENSORS_FPRINT_SECURE`, so it reports a detected sensor, not
  "unknown"; the control driver's `type_check` must be set to 3, see
  `../esfp0-test.c`).
- Still, the HAL's sensor-type logic caches "ET320" (the first entry of its
  name table: ET320 ET510 ET516 …) and opens the type-check session with TA
  index 0 ("unknown ta name: 0"); `BAuth_Prepare` builds every command for
  that index, and all three apps answer 42. The fault is in Samsung's
  user-space logic under hybris, not in the kernel interface.
- Android log capture: bionic liblog writes to `/dev/socket/logdw`; a small
  datagram listener there (see `logdw.py`) shows the HAL's own messages.

## What it took (for whoever resumes)

1. musl lacks glibc headers/APIs libhybris uses: `android-headers/sys/cdefs.h`
   (no `__unused`: musl's `struct stat` has a field of that name) and
   `hybris-musl.h`, force-included; build with
   `CFLAGS="-O2 -D_GNU_SOURCE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -include sys/cdefs.h -include hybris-musl.h"`
   (same for `CXXFLAGS`), `CPPFLAGS="-D_GNU_SOURCE -U_FORTIFY_SOURCE -I<android-headers>"`,
   `./configure --with-android-headers=<android-headers> --enable-arch=arm64 --with-default-egl-platform=null --disable-glvnd --disable-mesa`
   (needs `wayland-dev` installed for configure to finish).
2. The Android library tree needs bionic `libc.so` and `libm.so`, and
   **empty stub** `libdl.so` and `ld-android.so` (hybris provides those
   itself; the real ones crash), plus a stub
   `libclang_rt.ubsan_standalone-aarch64-android.so`.
3. This libhybris tree has no hooks for ~190 libc symbols that Android 9
   libraries import; unhooked, they bind to bionic's libc and crash on its
   TLS. `gen-hooks.py` builds `hooks_musl_extra.inc` from the real import
   list: direct hooks to musl, shims for the bionic-only ones (fortify
   `_chk` family, `__open_2`, `__register_atfork`, …).
4. `linker_soinfo.cpp`: skip bionic's property init when
   `/dev/__properties__` is absent (`patch-props.py`).
5. The HAL expects the apps under `/vendor/firmware_mnt/image/` and writes
   `/data/vendor/biometrics/{fp/User_0,ta,meta,bk}`.

`../fp-hybris-test.c` drives the HAL (open, enumerate, `enroll`, `auth`).
The Android libraries themselves come from the stock firmware and are not
in this repo.
