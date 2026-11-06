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

## HEVC decode on Linux 7.3.1 (2026-11-02)

`tests/venus-hevc.sh` on an SM-T820, x265 8-bit clips from `tests/venus-hevc-clips.py`,
each compared with a software decode. Speed is with nothing reading the frames. The
kernel was 7.3.1 plus the touch key patches, which don't touch Venus.

| Clip | Frames match | fps |
|---|---|---|
| 1280x720 | yes | 153 |
| 1920x1080, 30 and 60 fps | yes | 79 |
| 1080p with wavefront off, many references, no B-frames, open GOP, closed GOP | yes | |
| 2048x1536, the panel's size | yes | 99 |
| 3840x2160 | yes | 20 |
| 720p, 1080p, 720p in one stream | yes | |
| 720p with parameter sets in the stream (`hev1`) | yes | |
| 854x480, 1366x768 | yes, see below | |

Also on the T820:

- A 1080p clip from our own `v4l2h265enc` decodes, and all 300 frames match once the
  extra lines below are removed.
- 80 runs of the 1080p clip over 10 minutes: available memory went from 1656 MB to
  1673 MB, with no kernel messages.
- 1080p through `waylandsink`, on the panel: looked right by eye.

Limits:

- **4K isn't real time.** The clip takes 7.7 s for 150 frames, with Venus already at
  490 MHz. An extra 2 or 6 GB/s vote on its memory path changed nothing. Same
  suspects as H.264 encode at 4K below.
- ~~**The stream's crop isn't applied.**~~ Fixed by patch 0182 (below): 854x480 came
  out 856 wide, 1366x768 1368 wide, and 1080p from our encoder 1088 high.
- The driver offers 16x16 to 4096x4096.

Not tried: an SM-T825, 10-bit (Main 10), and which commit fixed it on 7.3.

## Crop and stability (2026-11-06, patch 0182)

The firmware sends no crop rectangle with its sequence-changed event, for
H.264 as well as HEVC, so the driver reported the coded size: libx264's
1920x1080 came out 1920x1088 (every H.264 1080p clip, which is coded as
1088 lines and cropped), 854x480 came out 856 or 864 wide. **0182** keeps
the size userspace sets on the OUTPUT queue (GStreamer takes it from the
container) and, when the firmware gives no crop and the coded size is that
size rounded up by less than a 64-pixel coding block, crops to it. Other
cases keep the coded size, as before.

`tests/venus-stress.sh` on an SM-T820 (Linux 7.3.1, android branch with
0182), every decode compared frame for frame with libavcodec's decoder in
the same GStreamer pipeline, and the kernel log checked after each test:

| Test | Result |
|---|---|
| baseline: H.264 720p, 1080p, 854x480; HEVC 1080p, 854x480; VP8 720p; VP9 1080p | all bit-exact |
| soak: 30 decodes, random clip order | 0 bad; MemAvailable 2928 -> 2886 MB |
| parallel: 4 decoders at once, 5 rounds | all bit-exact |
| H.264 1080p encode during an HEVC 1080p decode | decode bit-exact, 300/300 frames encoded |
| abort: 30 decodes killed mid-stream, then every clip once more | all bit-exact |
| seek: 20 random seeks each in H.264 and HEVC | ok |
| resolution change 480p -> 720p -> 480p in one H.264 stream | 180/180 frames, bit-exact |
| runtime PM: Venus idles off between sessions, 10 cycles | suspended all 10 times |

No kernel messages from Venus in any test. With the same modules minus
0182, the baseline fails H.264 1080p, H.264 854x480 and HEVC 854x480.

**Limit: a resolution change in mid-stream to a size that needs a crop**
(an adaptive stream going to 1080p) still shows the coded size (1088
lines): userspace reads the crop at the resolution-change event, and then
only the firmware knows the new picture's size. It does tell: every
fill-buffer-done carries the visible size (`frame_width`/`frame_height`
in `hfi_msg_session_fbd_uncompressed_plane0_pkt`, 1920x1080 for those
frames, 854x480 for the HEVC clip; seen with a debug print). Using that
would mean signalling a crop change to userspace from the first frame of
the new size, which the driver can't do yet. Plain files are not affected. The script makes its clips with
ffmpeg on the tablet (two threads: four at full load take the bare board's
big cores past 90 C) and keeps them in `/var/tmp/venus-stress`.

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

- **HEVC decoding on 6.19:** the firmware never reports the stream resolution, so
  GStreamer fails to negotiate ("Video device returned invalid dimensions"), or the
  clip hangs. Linux 7.3.1 fixes it: see below. Commit not identified.
- **10-bit HEVC (Main 10)** is untested and stays out of the default run.
- **4K HEVC decode** runs at about 20 fps, below real time: see above.
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
