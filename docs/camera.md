# Cameras: Sony IMX258 (rear) and Samsung S5K5E3 (front)

## Status (2026-10-31): both cameras work through libcamera

Kernel patches 0089-0103, the main DT and `CONFIG_VIDEO_IMX258`,
`CONFIG_VIDEO_DW9807_VCM` and `CONFIG_VIDEO_S5K5E3` (=m, set in the
kernel config by the pmaports commit). libcamera's simple pipeline drives
both through the CAMSS RDI path and its software ISP (GPU).

| | Rear | Front |
|---|---|---|
| Sensor | Sony IMX258, 13 MP (4208x3120) | Samsung S5K5E3, 5 MP (2576x1932) |
| Lens | Dongwoon DW9807 voice coil, contrast autofocus | fixed focus |
| Control bus | CCI master 0, 0x1a; lens on BLSP2 QUP1 I2C, 0x0c | CCI master 1, 0x10 |
| CSI-2 | CSIPHY0, 4 lanes | CSIPHY2, 2 lanes |
| Modes | 4208x3120, 2104x1560 (2x2), 4:3; 4208x2368, 2104x1184 (2x2), 1048x592 (4x), 16:9 (patch 0091) | 2576x1932 (30 fps), 1280x960 (2x2, 60 fps), 640x480 (4x4, 120 fps) |
| Supplies | L17, L23, L29, LVS1 from the PM8994; two switched core supplies from S3, enabled by TLMM 9 (rear) and 8 (front) | |

The camera drivers probe at boot; they keep the sensors off until a
stream starts.

Both sensors read out upright with the tablet in landscape, the
orientation the board DT describes (the panel has `rotation = <270>`), so
both cameras have `rotation = <0>`. Samsung's mount angles, 90 and 270,
are measured against the portrait panel. With those values, PipeWire
clients such as Firefox and Chromium turned the picture a quarter turn.

## libcamera

postmarketOS's libcamera (temp/libcamera, 0.7.2) lists both cameras, but
has no autofocus and no sensor helper for the S5K5E3. `pmaports/libcamera/`
is that package plus our series (patches 0007-0016, pkgrel 106):

- S5K5E3 sensor helper and properties;
- focus statistics from the software ISP, lens controls passed to the
  IPA, a contrast detection autofocus algorithm in libipa (with a test)
  and its use in the simple IPA: `AfMode` Manual (default, as on the
  Raspberry Pi: the lens stays at `LensPosition`), Auto with
  `AfTrigger`, and Continuous;
- IMX258 and S5K5E3 tuning files;
- output buffers padded to 32-line groups, so that 3840x2160 frames can go
  to the Venus encoder as dma-bufs;
- an IMX258 lens settle wait that covers the software ISP's buffer lag
  (without it, continuous AF rescanned every 1.6 s at 4K).

Applications that don't set `AfMode` (Plasma Camera, and PipeWire clients
such as Snapshot) get a fixed lens. GStreamer:

```
gst-launch-1.0 libcamerasrc af-mode=continuous \
	! video/x-raw,width=1920,height=1080 ! queue ! waylandsink fullscreen=true
```

## 4K recording

With the software ISP on the GPU (`LIBCAMERA_SOFTISP_MODE=gpu`), RGBA
dma-bufs go straight into the Venus encoder, which converts them to YUV
(output-io-mode dmabuf-import):

```
LIBCAMERA_SOFTISP_MODE=gpu gst-launch-1.0 -e libcamerasrc af-mode=continuous \
	! video/x-raw,width=3840,height=2160,format=RGBA \
	! v4l2h265enc output-io-mode=dmabuf-import \
	  extra-controls=encode,video_bitrate=40000000 \
	! video/x-h265,level=(string)5.1 ! h265parse ! queue ! mp4mux \
	! filesink location=uhd.mp4
```

The level goes in the caps after the encoder: v4l2h265enc and
v4l2h264enc set the level control from the negotiated caps, over a level
in `extra-controls` (with `hevc_level=8` there, the stream said level 1).

- HEVC 3840x2160: 29.8 fps with the GPU held at 624 MHz, 29.5 fps
  without; about 1.5 CPU cores.
- H.264 3840x2160: about 27 fps. Venus takes about 35 ms per frame at
  490 MHz with camera content whatever the profile, entropy coding or
  bitrate; MMPLL3 is left at 980 MHz by the bootloader, so the 520 MHz
  table entry gives 490 MHz, and nothing votes for CX.

## Applications (2026-10-31)

- [Ocellus](https://github.com/jaydens-tasty-slop/ocellus), a camera app
  written for this tablet: photos, and video with sound in the modes in
  the table below, encoded by Venus from the ISP's RGBA buffers without a
  copy.
- Firefox 154 and Chromium 155 reach both cameras through PipeWire's
  libcamera nodes and the camera portal. Chromium needs
  `--enable-features=WebRtcPipeWireCamera`, and its first `getUserMedia()`
  right after start can fail with NotFoundError: the cameras turn up about
  10 ms after it calls its PipeWire enumeration complete. Chromium ran
  30 fps at 640x480, 1280x720 and 1920x1080; Firefox painted 14 to 21 fps
  (its window was in the background).
- PipeWire clients (browsers, Snapshot) never set `AfMode`, so the rear
  lens stays where it is.

### Video modes in Ocellus

Recorded for 8 s in the Plasma session with the viewfinder on and sound,
with the frame rate measured from the frame timestamps after the first
second:

| Camera | Size | Codec | Frame rates measured |
|---|---|---|---|
| Rear | 3840x2160 | H.265 | 24.0 |
| Rear | 1920x1080 | H.264 | 30.0; 59.6 to 60.0 |
| Rear | 1280x720 | H.264 | 59.6 |
| Rear | 1024x576 | H.264 | 59.6; 119.3 to 120.0 |
| Front | 1920x1080 | H.264 | 29.9 |
| Front | 1152x864 | H.264 | 59.8 to 59.9 |
| Front | 512x384 | H.264 | 59.8 to 60.7; 119.6 to 120.1 |

Ocellus also offers 30 fps at every size; the 30 fps modes missing from
the table weren't measured. Photos are viewfinder frames: 4200x3120
(rear, the full sensor less the ISP's 8-pixel border; the viewfinder runs
at about 17 fps there), 2568x1932 (front).

- The simple pipeline has no `FrameDurationLimits`, so libcamera can't set
  the frame rate. For 60 and 120 fps, Ocellus shortens the sensor's
  `vertical_blanking` with `v4l2-ctl` once the stream runs (frame length =
  pixel rate / (line length x fps)).
- With sound, a GStreamer pipeline runs on the microphone's clock, and
  libcamerasrc maps the frame timestamps onto it. When the microphone
  started late (see the q6asm item under Open), the video froze after a
  second or two, which looked like a 120 fps problem. Ocellus records on
  the monotonic system clock: since then, 18 of 20 test recordings at
  120 fps were clean. In the other two, the video kept running but
  dropped frames until the sound started.

## Kernel fixes found on the way

- 0089-0091 IMX258: a wait after power-up and the reset GPIO (Muzaffer
  Kadir's patches), three 16:9 modes cut from a centred window, as the
  tablet's Android camera stack does.
- 0092-0093 DW9807: its vdd and vio supplies, switched with runtime PM.
- 0094-0095 S5K5E3: binding and driver; the analog setup table and the
  1280x960 binning are Samsung's (fimc-is-cis-5e3-setA.h, GPL kernel for
  Exynos devices), the PLL and timing values the ones the tablet
  programs with its 24 MHz clock.
- 0096 CAMSS ISPIF: power all VFEs for the ISPIF reset; using VFE1 broke
  the next VFE0 stream.
- 0097 CAMSS: stop the subdevs already started when a stream start fails.
- 0098 CCI: retry a message that timed out. The CCI pull-ups ride the
  sensor rails, and the first transfer after a sensor power cycle hung
  in about a third of the cycles.
- 0099-0102 Venus encoder: no new capture buffers once the session runs
  (the firmware crashed, vbuffer.c:1219), firmware timestamps at the
  frame rate (otherwise the bitrate is ignored), RGBA input on HFI v3,
  CAVLC with the Baseline profiles (CABAC there crashed the firmware).
- 0103 CAMSS VFE: toggle the frame drop period once per register update;
  two toggles in one frame stopped the stream for 1028 ms whenever the
  VFE ran down to its last buffer.

## Open

- **Idle power of the lens.** libcamera keeps a camera's subdevs open as
  long as its camera manager lives, and WirePlumber's libcamera monitor
  runs in every session (also the login greeter's). The DW9807 powers up
  when its subdev is opened, so the lens and its supplies (L17, and LVS1,
  which the sensors' I/O shares) stay on while the system is up; the
  sensors themselves are runtime suspended. In system suspend the driver
  parks the lens and turns them off. To be measured: the screen-off idle
  drain with WirePlumber stopped and running.
- The rear camera's EEPROM (0x58, calibration and AF data) is not
  described: there is no binding for it. Focus positions are therefore
  uncalibrated (about `LensPosition` 5 at monitor distance).
- The AF settle wait counts processed frames; counting from the sensor
  frame sequence at the time of the move would be exact.
- Venus at its full clock (MMPLL3 at 1040 MHz, CX votes, the 512 KB VMEM
  the vendor gives it) for 4K H.264 at 30 fps.
- Venus with imported RGBA dma-bufs refuses a buffer whose width is not a
  multiple of 128 pixels ("Failed to process frame" when it is queued;
  seen at 1216x912 and 576x432): it sizes the input for the aligned width.
  The software ISP's output is 8 pixels narrower than the sensor mode,
  so Ocellus's video modes use widths that are multiples of 128 and fit
  in a sensor mode: 3840, 1920, 1280, 1152, 1024 and 512.
- The encoder refused 25 fps at every size tried; 24 and 30 work.
- **q6asm: the microphone sometimes starts 5 s late.** In 2 of 20
  recordings the capture stream started 5 s late, with
  `q6asm-dai ...: CMD 10bcd timeout`. This is the known issue in
  [audio.md](audio.md); a fix is written and waiting for a test.
