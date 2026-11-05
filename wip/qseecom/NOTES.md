# QSEE (TrustZone) apps on mainline: work in progress (2026-11-05)

Samsung's fingerprint, keymaster (hardware-backed keystore), gatekeeper and
Widevine run as apps inside TrustZone (QSEE). Stock loads them through the
downstream `qseecom` driver; mainline's `qcom_qseecom` only talks to apps the
firmware has already started (the UEFI secure app on recent laptops). This
branch adds the downstream-style path on top of mainline's own SCM code.

## Results on an SM-T820 (kernel r11 + `suspend-wip` + these patches)

- QSEE version 0x1001000.
- `cmnlib` (the services library) loads; `skeymast` (Samsung's keymaster,
  ELF32) starts as app 6; `widevine` (ELF64) starts as app 7; lookups by
  name find them afterwards.
- The four standard listener services register (rpmb 0x2000, ssd 0x3000,
  fs 0x4000, time 0x5000, with the buffer sizes of the public qseecom trees).
- Samsung's keymaster answers get_supported_algorithms (1) and SelfTest
  (0x12) with status 0: the status is word 3 of the response (the stock
  libkeymaster_helper reads [rsp + 0xc]), the body a packed structure (the
  HAL's packing library, format tag 0xa000). Both app builds (the tablet's
  and T820XXU1AQB6's) behave the same; app shutdown and reload work.

## Patches (`patches/`, on top of `wip/suspend/patches`)


1. `qcom_scm_qseecom_app_start()` (app manager command 1, args mdt_len,
   img_len, phys) and `qcom_scm_qseecom_load_services_image()` (command 7);
   the Tab S3 added to `qcom_scm_qseecom_allowlist`;
   `qcom_qseecom_tabs3_test` (`CONFIG_QCOM_QSEECOM_TABS3_TEST=m`), a test
   module that loads cmnlib and one app from
   `qcom/msm8996/samsung/gts3/tz/` and logs the app ID (parameters
   `fw_dir`, `app`, `load_cmnlib`).
2. Listener register/deregister (listener service, commands 1/2, the shared
   buffer passed as RW), a raw app send that returns the QSEE response, and
   the listener response handler (command 3); the response struct and result
   codes in the public header. The test module registers the four listeners
   with stub handlers (log the request, answer with a failure) and sends
   keymaster a command (parameters `listeners`, `km_version`).

3. Full-size app sends: `qseecom_mem` as a reusable CMA pool and the SCM
   node's `memory-region` (`dts/test/test-qseecom.dts`), a physical-address
   app send, the test module's buffers from `dma_alloc_coherent()` on the SCM
   device. QSEE then accepts an 82 KB request. Samsung's keymaster protocol
   (header `{cmd, 0xa000, 0xa000}`, command IDs) from the LineageOS vendor
   `libkeymaster_helper.so`; the app answers every command with -1000
   (rejected): to be compared with the stock HAL or a traced exchange on 3.18.
4. App shutdown (command 2); test module parameters `shutdown_first`,
   `skm_req_len`/`skm_rsp_len` (64 KB + 64 KB for the 2017 HAL, 0x14080 +
   0x40 for the later one), hex dumps, the status from the response.

5. `/dev/qseecom` (`CONFIG_QCOM_QSEECOM_DEV=m`): the downstream ioctl ABI
   (listener register/unregister, send_cmd, receive_req, send_resp,
   load/unload app, set_mem_param, app_loaded_query, version, perf, bus
   scaling) with DMA-BUF fds for the shared buffers. Needs
   `CONFIG_DMABUF_HEAPS`, `_CMA`, `_SYSTEM`; `test-qseecom.dts` marks the
   pool `linux,cma-default`, exported as `/dev/dma_heap/qseecom@fec00000`.
   `tools/qseecom-test.c` (build on the tablet: `gcc -O2 -o qseecom-test
   qseecom-test.c`) loads an app and sends it a command, or runs a stub
   listener. Keymaster answers through the device as through the kernel test.

Config: `CONFIG_QCOM_QSEECOM=y` (bool, depends on `QCOM_SCM=y`).

## Firmware (never in the repo)

The apps are in the `apnhlos` partition, `image/`: `cmnlib`, `cmnlib64`,
`skeymast`, `skm`, `skmm_ta`, `fingerpr` (20 MiB, bigger than the 20 MiB
`qseecom_mem` reservation), `dualfp`, `widevine`, plus Samsung's `tima_*`.
Put them at `/lib/firmware/updates/qcom/msm8996/samsung/gts3/tz/`
(`gts3-firmware` should copy them on first boot, like the DSP firmware).
The stock `vendor/bin/qseecomd`, `libQSEEComAPI.so` and the listener
libraries (`librpmb`, `libssd`, `libdrmfs`, `libdrmtime`) on the system
partition are the reference for the listener protocol and the IDs.

## How the image is built

`.mdt` followed by every `.bNN` segment file, back to back, in one
physically contiguous buffer (`dma_alloc_coherent`, 32-bit mask). The
image is loaded by the secure world from that buffer.

## Next steps

1. Samsung keymaster command numbers from its HAL; a real GET_VERSION.
2. Real listener handlers: time (trivial), fs (files under a directory,
   downstream `libdrmfs` protocol), rpmb (the eMMC RPMB partition through
   the block layer), ssd (secure storage).
3. DONE: `tools/libQSEEComAPI/`, an open `libQSEEComAPI.so` (BSD-3-Clause):
   the vendor API (handle with `ion_sbuffer` first, same call order:
   open, version, set_mem_param, load_app) on `/dev/qseecom`, buffers from
   the DMA-BUF heap (`QSEECOM_HEAP` to override, `QSEECOM_DEBUG=1` to log).
   `qseecom-km` uses it like the stock keymaster HAL (start_app skeymast,
   64 KB request + 64 KB response): keymaster answers as before. The
   fd-carrying variants (send_modified_cmd with ION fds), external ELFs,
   service commands and FDE keys are not implemented. Next: run the stock
   HALs / qseecomd against it (Android, or libhybris on Linux).
4. Then: hardware-backed KeyMint/Gatekeeper for Android-on-mainline, the
   fingerprint HAL (own buffer for the 20 MiB app), and the Widevine L1
   chain (secure DMA-BUF heap via `qcom_scm_assign_mem`, Venus secure
   sessions, MDP secure pipes).

Downstream references (Samsung GPL kernel): `drivers/misc/qseecom.c`,
`include/soc/qcom/qseecomi.h` (commands, SMC IDs, structs),
`include/uapi/linux/qseecom.h` (ioctls).
