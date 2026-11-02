# Video codec: Venus under mainline

## Status (2026-10-28)

With `dts/test/test-venus.dts` (merged into the main DTS on 2026-10-28) and kernel patches 0008 and 0009:

- **Decoding works and is bit-exact.** H.264 (High, with B-frames), VP8 and
  VP9 at 720p decode to the last frame, byte-identical to ffmpeg's software
  decoders. Tested through GStreamer's V4L2 decoders (`v4l2h264dec`,
  `v4l2vp8dec`, `v4l2vp9dec`) and `tests/venus-dectest.sh`.
- **The decoder offers** H.264, HEVC, VP8, VP9, MPEG-2, MPEG-4, H.263, VC-1
  and Xvid. **The encoder offers** H.264, HEVC, VP8, MPEG-4 and H.263.
- **H.264 encoding works through `v4l2-ctl`**: 60 frames, valid High profile
  stream, clean stop.
- **Update 2026-10-31: encoding through GStreamer works** (`v4l2h264enc`,
  `v4l2h265enc`) with patches 0099-0102, up to 3840x2160 from the camera
  ([camera](camera.md)): HEVC at 29.8 fps, H.264 at about 27 fps.

## Encoder fixes (2026-10-31, patches 0099-0102)

- **0099:** GStreamer asks for more capture buffers (CREATE_BUFS) once the
  session runs; Venus works in static buffer mode on HFI v3, and a late
  SET_BUFFERS made the firmware stop with `Err_Fatal ...
  vbuffer.c:1219` after a few frames. The encoder now refuses new capture
  buffers while streaming (-EBUSY), and GStreamer goes on with the ones it
  has. This is the GStreamer crash below.
- **0100:** the firmware gets timestamps at the frame rate. It derives the
  bitrate from the timestamps it is given, so with GStreamer's the bitrate
  setting was ignored.
- **0101:** RGBA input on HFI v3: Venus converts RGBA to YUV itself, so
  the camera's RGBA dma-bufs (libcamera's software ISP on the GPU) go to
  the encoder without a copy.
- **0102:** CAVLC with the Baseline profiles: CABAC with Baseline (what
  `v4l2h264enc` negotiates without a profile in the caps) crashed the
  firmware (`venus_venc_host_driver.c:5844`).

Open:

- **HEVC decoding:** the firmware never reports the stream resolution, so
  GStreamer fails to negotiate ("Video device returned invalid dimensions").
  Seen with libx265 Main profile streams at 720p and 1080p.
  Works on Linux 7.3.1 (a 1080p clip matches a software decode), commit not
  identified: see [kernel-7.3](kernel-7.3.md).
  A wider check is ready but hasn't run: `tests/venus-hevc-clips.py` makes the
  clips with the md5 of a software decode, `tests/venus-hevc.sh` runs them on the
  tablet. 10-bit (Main 10) is untested and stays out of the default run.
- ~~**Encoding through GStreamer (`v4l2h264enc`):** the firmware stops with
  `Err_Fatal ... venus\utils\src\vbuffer.c:1219` after about 5 frames.~~
  Fixed by patch 0099 (above).
- H.264 at 3840x2160 tops out at about 27 fps: about 35 ms per frame at
  490 MHz with camera content. The bootloader leaves MMPLL3 at 980 MHz (the
  vendor runs 1040 MHz), nothing votes for CX, and the vendor's 512 KB of
  VMEM for Venus is not described.
- With `v4l2-ctl` defaults the encoder writes level 1.0 and no SPS cropping
  (1280x736 for 720p). Userspace normally sets the level; check the cropping.
- `ffmpeg -c:v h264_v4l2m2m` aborts in ffmpeg itself (`Assertion pkt failed`).
  Use GStreamer to test.

## Firmware and DT

Samsung's signed firmware is on the tablet's `apnhlos` partition
(`image/venus.mdt`, `.b00`-`.b04`, version `VIDEO.VE.4.4-00060`). Copy it to
`/lib/firmware/qcom/msm8996/samsung/gts3/`; it is not in this repo.
TrustZone authenticates it (PAS id 9).

The segments are relocatable and span 5 MB, so they fit msm8996.dtsi's
5 MB `venus_mem`. The main DT places it at 0x92000000, where the stock
kernel loads Venus, at the end of the stock `peripheral_region`
(0x8f400000, 49 MB); see docs/gnss.md for the layout.

```dts
&venus {
	firmware-name = "qcom/msm8996/samsung/gts3/venus.mdt";
	status = "okay";
};
```

## What was broken (patches 0008, 0009)

1. **End of stream killed the session.** The driver signals EOS with an empty
   input buffer at the dummy address 0xdeadb000. This firmware checks every
   input buffer against its content-protection map. The firmware's own log
   (dynamic debug, `VenusFW`) says:

       IsBufferProtected: CP_UNKNOWN, buffer straddles CP & non-CP regions
       vDec_EmptyBuffer: Invalidate buffer in ETB

   The session dies (event 1001), every decode loses its last 7-12 frames,
   and the encoder crashes the firmware (SMMU faults, exception).

   A NULL address passes the check. But then the firmware returns the EOS on a
   NULL capture buffer ahead of frames still waiting for one, and the stream
   ends early whenever the consumer is slow. The vendor stack always uses a
   real buffer, so patch 0009 allocates a 4 KB DMA buffer on HFI v3 and
   signals EOS on it.

2. **LAST flag lost.** When the firmware reports EOS on a buffer tag the
   driver no longer holds, `vdec_buf_done()` dropped the flag, and userspace
   waited forever. Patch 0009 completes the drain anyway.

3. **H.264 encoding failed at REQBUFS.** The encoder always sets
   `HFI_PROPERTY_PARAM_VENC_H264_TRANSFORM_8X8`, but only the HFI v4 packet
   builder knew it. On v3 (msm8996) it returned -EINVAL. Patch 0008 adds it to
   the v3 builder. The downstream msm8996 driver sends the same property, so
   the firmware supports it. This affects every msm8996 device on 6.19, not
   only this tablet.

## How it was tested without rebuilding the kernel

The modules were rebuilt on the tablet itself, against the running kernel's
`/proc/config.gz` and with Alpine's clang 23 (the kernel uses kCFI, so the
compiler has to match). The host only sends a trimmed source tree: Kbuild
files, headers, arm64, `kernel/`, `lib/vdso` and the driver. The modules go
to `/lib/modules/$(uname -r)/updates/`, followed by `depmod`.

Run `sync` after installing modules. A reset shortly after an unsynced copy
corrupted both the `.ko` and the `.o` files.

To get the log from a boot that ended in a reset: boot with
`lk2nd.pass-ramoops=zap`. After the reset, in lk2nd, run
`fastboot oem ramoops console` and `fastboot get_staged console.txt`.

See also docs/thermal.md: without TSENS, long builds on the tablet overheated
it.
