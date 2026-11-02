# Egis ET510 protocol facts

Facts for a future Linux/libfprint driver, should the SPI bus ever become
the application processor's (see `fingerprint.md`). Sources:

- **GPL**: Samsung's msm-3.18 kernel (awesometic/android_kernel_samsung_msm8996,
  at 53683713fd1f), `drivers/fingerprint/et5xx.h` and
  `drivers/fingerprint/et5xx-spi{,_data_transfer}.c`. This is the vendor's
  own GPL-2.0 driver, so the wire format below is authoritative, not
  reverse-engineered.
- **Observed**: stock boot/HAL logs from this SM-T820.
- **Blob (facts only)**: the `dualfp` trustlet and `libbauthserver.so`,
  inspected with a headless Ghidra 12.1.2 decompile pass for structure,
  addresses, constants and sizes only - no code was copied or ported. Blobs
  and the Ghidra project live under `out/pmos/fingerprint/` (gitignored),
  never in git.

The register *values* for init and calibration, the detect-mode
configuration and the exact image geometry are **not** in the GPL driver:
in Samsung's shipping build every SPI transfer helper is compiled out
(`#ifdef ENABLE_SENSORS_FPRINT_SECURE return 0;`), and the real sensor I/O
runs inside the `dualfp` trustlet. The image geometry, the frame-ready poll
and the capture sequence have now been recovered from the trustlet (see
"Facts recovered from the trustlet"); the exhaustive per-mode init/calibration
register script is the remaining gap (see "Still unknown").

## Transport

- SPI mode 0 (CPOL 0, CPHA 0), 8 bits/word, MSB first, CS active low
  (`et5xx-spi.c:1144-1146`: `spi->mode = SPI_MODE_0; bits_per_word = 8`).
- Clock: 12.5 MHz nominal (`SLOW_BAUD_RATE`, `et5xx.h:73`); the sensor node
  allows up to 16 MHz, the controller up to 19.2 MHz. The stock HAL drives
  12.5 MHz for register work and briefly 4 MHz (observed
  `FP_SET_SPI_CLOCK, clock = 12500000 / 4000000`).
- Every transaction is opcode byte first, then payload, full-duplex; read
  data comes back in the same `spi_sync` buffer, offset by the opcode (and
  address) bytes already clocked out.

## Opcode map

From `et5xx.h:57-70` (the `OP_*` constants) and the framing in
`et5xx-spi_data_transfer.c`:

| Opcode | Name | Direction | Wire format |
|---|---|---|---|
| 0x20 | `OP_REG_R` | read 1 reg | tx `[0x20, addr, 0x00]`; value is rx byte 2 (`etspi_io_read_register`) |
| 0x24 | `OP_REG_W` | write 1 reg | tx `[0x24, addr, val]` (`etspi_io_write_register`) |
| 0x22 | `OP_REG_R_C` | burst read | tx `[0x22, addr]` + N dummy bytes; data at rx+2 (`etspi_io_burst_read_register`) |
| 0x26 | `OP_REG_W_C` | burst write | tx `[0x26, val0, val1, ...]`; auto-incrementing from a start addr (`etspi_io_burst_write_register`) |
| 0x23 | `OP_REG_R_C_BW` | burst read, addr decrementing | "backward" variant |
| 0x27 | `OP_REG_W_C_BW` | burst write, addr decrementing | "backward" variant |
| 0x50 | `OP_IMG_R` | read frame | tx `[0x50]` + `size` clocked bytes; frame at rx+1, 8-bit/pixel (`etspi_io_get_frame`) |
| 0x40 | `OP_NVM_ON_R` | NVM read-enable | NVM access |
| 0x42 | `OP_NVM_ON_W` | **NVM write-enable** | **never send** |
| 0x44 | `OP_NVM_RE` | NVM read | NVM access |
| 0x46 | `OP_NVM_WE` | **NVM write** | **never send** |
| 0x48 | `OP_NVM_OFF` | NVM disable | NVM access |
| 0x60 | `OP_VDM_R` | VDM read | vendor data memory |
| 0x62 | `OP_VDM_W` | VDM write | vendor data memory |

**Hazard.** Opcodes 0x42 and 0x46 write the sensor's NVM, which holds its
factory calibration, and 0x62 writes its VDM (vendor data memory - the
calibration/EEPROM store the trustlet reads back as "eeprom"). A driver must
never issue any of them. The trustlet builds a VDM **write** frame (opcode
0x62, in `FUN_00153c40` @ 0x153c40, see below), confirming it is
a real command; the read side it uses is VDM read 0x60. For bring-up,
read-only opcodes (0x20, 0x22, 0x50, 0x40/0x44/0x48 read side, 0x60) are
enough.

The frame read pads its length up to a multiple of 4 when it is 64 bytes or
more (`LARGE_SPI_TRANSFER_BUFFER=64`, `DIVISION_OF_IMAGE=4`,
`et5xx-spi.h:126-128`), a DMA alignment detail of the controller, not the
sensor.

## Known registers

From `et5xx.h:122-125` and the ID check in `et5xx-spi.c:872-928`:

| Reg | Name | Meaning |
|---|---|---|
| 0x00 | `FDATA_ET5XX_ADDR` | status/data; reads `0xAA` when the sensor is ready |
| 0x01 | `FSTATUS_ET5XX_ADDR` | bit 0 (`FRAME_READY_MASK`) = frame ready |
| 0xFD, 0xFE, 0xFF | chip-ID registers | identify the ET510 variant |
| 0x20, 0x21, 0x23, 0x24 | ID registers (ET510C) | alternate ID path |

Variant detection (`et5xx-spi.c:872-928`), after power-on and a 20 ms wait,
reading reg 0x00 first (must be 0xAA):

| Variant | 0xFD / 0xFE / 0xFF (or 0x20/0x21/0x23/0x24) |
|---|---|
| ET510C | 0x20=0x00, 0x21=0x66, 0x23=0x00, 0x24=0x33 |
| ET510D | 0xFD=0x03, 0xFE=0x0A, 0xFF=0x05 |
| ET516A | 0xFD=0x00, 0xFE=0x10, 0xFF=0x05 |

This tablet's node declares `etspi-chipid = "ET510"`; the live variant
(C or D) should be read in a hardware session (`fingerprint.md`, step 1c).

Interrupt/detect constants (`et5xx.h:76-77`): `ET5XX_INT_DETECTION_PERIOD`
10, `ET5XX_DETECTION_THRESHOLD` 10. Finger presence raises DRDY (TLMM 131),
handled as a wake IRQ (`et5xx-spi.c:119-127`).

## Power, reset, interrupt (GPL, Linux already owns these pins)

- Power on (`et5xx-spi.c:191-211`): LDO enable (PM8994 MPP 8) high, wait
  50-100 us, reset (`BTP_RST_N`, PM8994 MPP 2) high, select the active
  pinctrl, wait 10 ms.
- Reset pulse (`et5xx-spi.c:181-189`): `BTP_RST_N` low for ~1 ms, then high.
- Power off: reset low, then LDO low.
- DRDY: TLMM 131, pull-up active / pull-down in suspend; `enable_irq_wake`.

## Capture pipeline

`dualfp` strings show the normal-world-visible stages: `[TIME_ANA] image
capture`, `[TIME_ANA] image quality check`, `average_10_frames`,
`fp_calibrate_image`, `get_calibrate_image_session`, `OutputImageType`. So a
capture averages ~10 frames and runs a quality/calibration pass before
matching. Enrolment and matching (`TZ_CMD_ENROLL_DO`, `TZ_CMD_IDENTIFY_INIT`)
and template storage (HMAC-wrapped) stay inside TrustZone and are out of
scope for a Linux capture driver.

The sensor is the front **home-button** sensor, i.e. a press/area capacitive
sensor (not a swipe sensor). A libfprint driver would use the press/image
path, with NBIS minutiae matching in the host.

### Facts recovered from the trustlet

A headless Ghidra 12.1.2 pass (`ghidra-analyzeHeadless`, its own JVM/project,
never the other session's GUI instance) over the reconstructed
`out/pmos/fingerprint/blobs/dualfp_full.elf` (AArch64, confirmed against the
`dualfp.mdt` e_machine; 763 functions, all decompiled) recovered the capture
internals facts-only. Addresses below are offsets in that ELF (image base 0),
kept as evidence; no function was transcribed or ported.

**SPI primitives (confirm the GPL wire format, from the shipped binary).**

| Function | Opcode frame built | Meaning |
|---|---|---|
| `FUN_0015333c` @ 0x15333c | tx `[0x20, addr, 0x00]`, value = rx byte 2 | read one register |
| `FUN_001536bc` @ 0x1536bc | tx `[0x24, addr, val]` | write one register |
| `FUN_00153730` @ 0x153730 | tx `[0x26, start_addr, d0, d1, ...]` | burst write (auto-inc) |
| `FUN_00153940` @ 0x153940 | tx `[0x50]` + `w*h*bpp` dummy, image at rx+1 | read one frame |
| `FUN_00153c40` @ 0x153c40 | tx head `0x60` (read) / `0x62` (write) | VDM read / **VDM write - never send** |

Both the burst write and the frame read size the SPI buffer as
`len = payload + header`, and round it **up to a multiple of 4 only when it
exceeds 64 bytes** (`if (len > 0x40 && len % 4) len = roundup4(len)`), matching
the GPL `LARGE_SPI_TRANSFER_BUFFER=64` / `DIVISION_OF_IMAGE=4` note. The frame
read's payload is exactly `width*height*bpp`, copied out from rx offset 1.

**Image geometry.** The frame read is called with explicit `(width, height,
bpp)`. For the normal scan / detect path, `bpp = 1` (8-bit grey) and the
dimensions are **103 x 52** (`0x67 x 0x34`) - hardcoded at the capture site
(`FUN_00153940(buf, 0x67, 0x34, bpp)`) and mirrored in the mode-geometry
selector (modes 1-2 select `0x67 x 0x34`). A third mode uses **137 x 40**
(`0x89 x 0x28`). Most runtime callers read the live width/height from a config
struct (`+0xf0`/`+0xf4`), which is loaded with those same values. So the
working assumption for a Linux driver: **103 x 52, 8 bpp**, one byte/pixel, no
per-line padding beyond the single leading opcode byte and the >64-byte
round-to-4 on the whole transfer. The TA comms shared buffer (`QSApp SB Size
2008576`) is unrelated to the frame size.

**Frame-ready poll.** `FUN_00148df4(reg, want, mask)` @ 0x148df4 is a generic
register poll: read `reg`, loop until `(value & mask) == want`, ~1 ms per
iteration, giving up after ~200 tries / ~500 ms. The capture path calls it as
`(0, 1, 1)` - **poll register 0x00, bit 0, until set**. (Note: the GPL driver
comments reg 0x01 bit0 as `FRAME_READY`; the shipped trustlet polls reg 0x00
bit0. Reg 0x00 is also the `0xAA` power-on ready byte. Treat reg 0x00 bit0 as
the per-frame ready flag and verify live.)

**Capture sequence** (single press, from the frame-read callers):

1. Program the operating/detect mode registers (per-mode; see init tables
   below).
2. Start a frame: write reg `0x2d = 0x13` (`FUN_001536bc(0x2d,0x13)`), or the
   burst form `FUN_00153730(0x2c, {gain-1, 0x13}, 2)` which also sets a gain
   byte at reg 0x2c.
3. Poll reg 0x00 bit0 == 1 (`FUN_00148df4(0,1,1)`).
4. Read the frame: opcode `0x50`, `103*52` bytes, image at rx+1.
5. Stop/idle: write reg `0x2d = 0x20` (`FUN_001536bc(0x2d,0x20)`).

**Individual register writes seen** (constants, as facts): `0x0c = 0x22` or
`0x44` (clock/gain mode), `0x13 = 0x72`, `0x49 = 0x0b`, `0x51 = 0xc4`/`0xc6`,
`0x2d = 0x13`/`0x20` (frame start/stop), `0x2c = gain`.

**Register map extent.** The reset/default validation routine
(`FUN_0015f1d4` @ 0x15f1d4) walks a 129-entry table (register indices
`0x00`..`0x80`), 12 bytes/entry `{flags, read-scratch, default, mask, ...}`
where flag bit0 = "check default after reset" and bit1 = "writable (write
test)". So the sensor has registers `0x00`..`0x80`, each with a known default
and a writable mask - but the table's default/mask *values* are populated by
its caller at run time, not stored as a static array.

## Still unknown (needs a deeper, still facts-only, pass)

The capture-critical internals above are recovered. What remains is the
**exhaustive power-on init / calibration register script** as a clean
`addr,value` list. The trustlet does not hold one static table: it assembles
each operating/detect mode at run time from a set of named config cells
(the `DAT_0079xxxx` globals written per mode in `FUN_00113b3c`'s mode switch,
cases 1-8), then flushes them to the sensor via the single/burst writers. To
reconstruct it, map each config cell to the register index it is written to
through `FUN_001536bc` / `FUN_00153730`, per mode - a larger follow-up, still
facts-only. The detect-mode threshold/period and the calibration (offset/gain)
search also live in that config-cell layer.

A Linux capture path can be brought up now to: power/reset the sensor, read
the chip ID (reg 0x00 == 0xAA, then the variant registers), run the register
read/write self-test, arm a frame (`0x2d=0x13`), poll reg 0x00 bit0, and clock
out a 103x52x8 image. Turning that raw image into an enrol/match template is
the host NBIS job and is the real quality unknown; the per-mode calibration
script is what is missing to match the stock image quality exactly.
