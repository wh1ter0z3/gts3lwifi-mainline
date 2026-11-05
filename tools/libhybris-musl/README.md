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
- The HAL module opens (`HMI`: "Demo Fingerprint HAL", API 2.1), powers
  the sensor and its SPI clocks through `/dev/esfp0`, reads the `dualfp`
  images and opens `/dev/qseecom`, then kills itself. Cause found but not
  re-tested: the open libQSEEComAPI looked for the DMA-BUF heap with a bad
  `open()` of `/dev/dma_heap` (fixed since in `../libQSEEComAPI`), fell
  back to the old pool address and could not allocate.

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
