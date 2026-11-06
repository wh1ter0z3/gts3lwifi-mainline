# Audio: ADSP + WCD9335 (headphones, microphones), RT5508 (speakers)

## Status (2026-10-28): headphones, microphones and speakers work

Speakers: see [Speakers](#speakers) below (kernel patches 0022-0027, main DTS,
UCM "Speaker" device).

With `dts/test/test-audio.dts` (merged into the main DTS on 2026-10-28), kernel patches 0010-0011 and the device
package's UCM profile and WirePlumber config:

- **Headphones:** clean playback. Checked by ear: a test tone, then speech
  through an echo loop.
- **Microphones:** the main mic (AMIC3) and sub mic (AMIC4) record cleanly,
  also checked by ear through the echo loop.
- **Both work through plain ALSA and through PipeWire.** WirePlumber picks the
  UCM "HiFi" profile by itself and offers a "Headphones" sink and a
  "Built-in microphones (main, sub)" source. Repeated play/record cycles, node
  suspend/resume and WirePlumber restarts run without DSP errors.

**Jack detection and the headset mic work on a T820:** [Jack and headset
mic](#jack-and-headset-mic), kernel patches 0162-0169.

Not done yet:

- Mic gain tuning.

## Hardware and firmware

| | |
|---|---|
| ADSP firmware | Samsung-signed `image/adsp.mdt` + `.b00`-`.b09` from the **modem** partition (`ADSP.8996.2.7.1.C1-00013`; the `dsp` partition only holds ADSP modules). Copy to `/lib/firmware/qcom/msm8996/samsung/gts3/`; not in this repo |
| ADSP memory | the image spans 0x2100000 bytes, more than msm8996.dtsi's 27 MB `adsp_mem`. The main DT gives it 0x8fe00000-0x91f00000, where the stock kernel loads it, inside the stock `peripheral_region` (layout in docs/gnss.md) |
| Codec | WCD9335 v2.0 on SLIMbus (NGD at 0x91c0000), PGD `1,0`, IFD `0,0` |
| Codec reset | TLMM 74 (stock `wcd_gpio_ctrl`). The Xiaomi boards use 64 |
| Codec interrupt | TLMM 79, one line (`intr1`). The Xiaomi boards use 54/53 |
| MCLK | 9.6 MHz, PM8994 GPIO 15 function 1 (RPM `div_clk1`), as on db820c |
| Supplies | PM8994 S4 1.8 V for buck, sido, tx, rx and io |
| Mics (stock routing) | AMIC1 <- MIC BIAS4 (third), AMIC2 <- MIC BIAS2 (headset), AMIC3 <- MIC BIAS1 (main), AMIC4 <- MIC BIAS3 (sub) |

Routing in the UCM profile (`pmaports/device-samsung-gts3lwifi/ucm2-*`):

- playback: MultiMedia2 -> SLIMBUS_6_RX -> AIF4_PB -> SLIM RX5/RX6 ->
  RX INT1/INT2 mix -> HPHL/HPHR;
- capture: MultiMedia3 <- SLIMBUS_0_TX <- AIF1_CAP <- SLIM TX0/TX1 <-
  DEC0/DEC1 <- AMIC3/AMIC4;
- headset mic, not yet tested on hardware: MultiMedia4 <- SLIMBUS_1_TX <-
  AIF2_CAP <- SLIM TX2/TX3 <- DEC2/DEC3 <- AMIC2.

The headphone settings follow Samsung's `mixer_paths_tasha.xml` ("headset"
path): compander on (`COMP1/COMP2 Switch`), `RX HPH Mode` class-H low power,
`HPHL/HPHR Volume` 20, `RX1/RX2 Mix Digital Volume` 74.

The db820c UCM sets `RX5/RX6 Digital Volume`, which is not on this path. With
the mix volume left at 0 dB and the compander off, the tone was distorted.

## What had to be fixed

1. **Capture recorded digital silence** unless something was playing. In the
   wcd9335 driver only the RX DACs (through `RX_BIAS`) pull in the `MCLK`
   supply. The test DT routes `MIC BIAS1-4` to `MCLK` as well.
2. **The ADSP refused to start the SLIMbus port** for PipeWire
   (`AFE enable for port 0x4001 failed`). `apq8096_be_hw_params_fixup()` fixed
   rate and channels but passed S24_LE through. Patch 0010 pins the back end
   to S16_LE, like sdm845.c and sm8250.c do.
3. **Re-prepare left a stream open in the DSP.** `q6asm_dai_prepare()` only
   closed RUNNING streams. After a STOP the next prepare opened the stream a
   second time (DSP error 9, EALREADY), then freed the audio client, and audio
   stayed dead until reboot. PipeWire hits this constantly, `aplay`/`arecord`
   never. Mainline fixed it in cee3e63e71. It also never reset `queue_ptr`,
   which patch 0011 still does.
4. **Capture periods.** q6asm-dai allows at most 4096-byte capture periods
   (1024 frames of S16 stereo). postmarketOS' generic `soc-qcom` 51-qcom.conf
   asks for 4096-frame periods, so every capture open fails with EINVAL. The
   device package ships its own `51-gts3lwifi-audio.conf` (playback 4096 x 6,
   capture 1024 x 4, S16LE, 48 kHz). The same probably affects other q6asm
   devices that use 51-qcom.conf.
5. **UCM verb disable sequence.** PipeWire opens each PCM through its own UCM
   context. Tearing the shared ADSP routes down when one context closed cut
   the other direction off mid-stream (an intermittent beep), so the verb has
   no disable sequence.
6. **An oops in the SLIMbus controller at boot** (1 boot in 5):
   `qcom_slim_ngd_runtime_resume()`, run by `qcom_slim_ngd_up_worker()`,
   dereferenced a NULL pointer. The PDR and SSR callbacks that queue the
   worker were registered before the NGD device existed. The oops left
   logind hanging, so ssh logins timed out (`CreateSession ... Device
   timeout`) while ping worked, and shutdown hung. Fixed in mainline 7.1 by
   Bjorn Andersson and Bartosz Golaszewski; 2a9d50e9ea is the commit that
   registers the callbacks after creating the NGD. 11 boots in a row were
   clean with these backported to 6.19.
7. **Closing a stream in the DSP timed out** (`q6asm-dai ... CMD 10bcd
   timeout`, 5 s): at the PipeWire start in 6 of 24 boots, while
   WirePlumber probed the PCMs, and before the sound of about one camera
   recording in ten. PipeWire prepares a capture PCM twice, and the second
   prepare closes the DSP stream that the first one opened, with reads
   still queued. The DSP rejects those reads, and `q6asm_stream_callback()`
   stored the rejections in `ac->result` like any other response. One that
   landed after the close's own response hid it from
   `q6asm_ac_send_cmd_sync()`, which waited out its 5 s, or which read the
   rejection's status instead (`DSP returned error[a]`, seen once). The
   upstream fix is 8a066a81ee0c ("ASoC: qcom: q6asm: drop DSP responses for
   closed data streams", in v7.0), which drops the `EOS`,
   `READ_V2` and `WRITE_V2` responses without touching `ac->result`. We had
   written the same change for `EOS` and `READ_V2` before finding it. With
   a debug print in that version, a rejection arrived after the close's
   response in 8 of 20 recordings at 120 fps, and all 20 started their
   sound on time; 10 more did on its final build and 10 of 10 on the
   backport, and no boot with either had the timeout.

Also seen: unbinding and rebinding the sound card fails because wcd9335 does
not free its SLIM slave interrupt (`request_irq ... -EBUSY`). Reboot instead
of reloading the machine driver.

## Jack and headset mic

| | |
|---|---|
| Plug in and out | the codec's mechanical switch, MBHC SW_DET, both switches normally open |
| Headphones or headset | the button FSM and HS_COMP against HS_VREF, with MIC BIAS2 at 1.8 V: first look 500 ms after plug in, final answer after about 3 s |
| Buttons | 8 thresholds, `<88 138 225 450 470 470 470 470>` mV: play/pause, voice command, volume up, volume down |
| Mic | AMIC2 on `hw:N,3`, one mic on two channels, UCM device `Headset`, `ADC2 Volume` 20, MIC BIAS2 at 2.7 V while recording |
| ALSA | `Headphone Jack` and `Mic Jack`, UCM's JackControls; input device `Headset Jack` for the buttons |
| Priorities | headphones above speakers, headset mic above built-in mics, only while plugged in (WirePlumber route availability) |

Mainline's `wcd-mbhc-v2` could only classify a plug with the MBHC ADC, which
the WCD9335 doesn't have, so patch 0162 adds the path without one. It follows
the non-ADC path in Qualcomm's downstream driver.

**Stock doesn't do this on this tablet.** It reads the mic line through PM8994
MPP4 on the PMIC's VADC: at most 690 mV is headphones, more is a headset. It uses
MBHC only for the switch, buttons and removal. We use the comparators, which needs
no PMIC and is the upstream form. During bring-up, MPP4 was the ground truth for
the numbers below.

### Tested

SM-T820 on 7.3.1, 2026-11-02, by hand, with a plain 3-pole headset and Apple EarPods.

| Check | Result |
|---|---|
| The switch | 5 of 5 moves, one interrupt each, no bounce |
| 3-pole | headphones, 13 of 13. The last 6, with the final timing, never briefly read as a headset |
| EarPods | headset, 8 of 8, including across a boot and pushed in slowly |
| Centre button | `KEY_PLAYPAUSE`, press and release paired, no ghost keys: idle, held for 2 s, and during a recording |
| Mic | speech peaks at 10 to 40 %, no tone |
| PipeWire | the right default sink and source for nothing, 3-pole and EarPods, within 2 s of each move |
| 5 plug cycles in 20 s, during playback and a recording | both ran to the end, no DSP errors, clocks released after. Sound moves to the speakers on a pull |
| Sleep with nothing, 3-pole, EarPods | 2 of 2 each. Nothing held: MCLK, master bias and MIC BIAS2 users 0 |
| Plug, pull or button during sleep | doesn't wake it, and shows up on resume |

Not tested:

- anything on a T825, whose device tree includes this one;
- a built package: RAM boots, with the UCM and WirePlumber files copied in by hand. The series applies and matches the kernel branch;
- the volume buttons. EarPods raise no interrupt at all for theirs, and nothing else with buttons was tried;
- any other 4-pole headset, or an OMTP one;
- Plasma's audio panel, by eye;
- sleep current, or the deep sleep work.

### What the hardware had to teach us

- **HS_VREF isn't linear.** With EarPods the mic line sits at 1066 mV on a 1.8 V bias,
  about 3.2 kohm on a 2.2 kohm pull up. Codes 0, 1 and 2, including the 1600 every
  other codec here uses, read "above" at every level down to 592 mV, so EarPods read
  as line out. Code 3 switches between 1126 and 1156 mV. With 470 mV as the top button
  threshold, a headset is about 0.77 to 3.8 kohm.
- **1.8 V is for detection, not for recording.** At 1.8 V, EarPods put out a steady 972 Hz
  tone with harmonics, and no speech. At 2.7 V, stock's voltage, the tone is gone. So patch
  0168 records at the board's voltage, and detection goes back to 1.8 V for as long
  as it needs the line. Other headsets weren't tried at 1.8 V.
- **`ADC2 Volume` 20, its top.** About 1.5 dB per step. Stock's 7 is too quiet here.
- **A press can be late.** 150 to 470 ms after the switch with plain headphones. A first
  look at 50 ms made 4 of 7 of them read as a headset for 3 s, hence 500 ms.
- **An event during sleep killed the jack until a reboot**, before patch 0164. The level
  interrupt fired on resume before SLIMbus was back, the status read failed, and it fired
  again until the kernel disabled it, at 100000 interrupts. Register reads then failed with
  -ENOSPC. That looks like SLIMbus running out of transaction IDs: `slim_do_transfer()`
  only frees one on a timeout. Not chased further, since this no longer reaches it.

Seen, harmless so far: `wcd9335_slimbus_irq: overflow error on TX port N` when a
capture ends, and one `overrun!!!` from `arecord`.

Left out on purpose:

- an OMTP headset (ground and mic swapped) reads as headphones;
- a headset outside the window above reads as line out or headphones;
- extension cables, impedance detection, micbias ramp on press.

Stock wakes the tablet on jack events. We don't, so a plug during suspend is seen
at the next wake.

## Speakers

All four speakers play: checked by ear with `tests/audio/spk.sh` (left,
right and both channels) and on every amplifier with the probe below. PipeWire offers a "Speakers"
sink (UCM device `Speaker`, below the headphones while those are plugged in).

| | |
|---|---|
| Amplifiers | 4x Richtek RT5508 (mono class-D, boost, I/V sense), revision I, on BLSP1 QUP3 I2C (TLMM 47/48, downstream `i2c_3`, mainline `blsp1_i2c3`) at 0x34-0x37 |
| Bottom pair (USB end) | 0x36, 0x37 on the **primary** TDM bus: TLMM 65 SCK, 66 WS, 68 data (SD1), `pri_mi2s` |
| Top pair | 0x34, 0x35 on the **quaternary** TDM bus: TLMM 58 SCK, 59 WS, 61 data (SD1), `qua_mi2s` |
| Frame | 4 slots of 16 bit, 48 kHz, 3.072 MHz bit clock from the SoC; long frame sync, inverted, one bit data delay (I2S framing), MSB first; from stock `msm8996-sec-gts3lwifi-r04.dtsi` |
| ADSP ports | PRIMARY_TDM_RX_0 and QUAT_TDM_RX_0, each alone in its group, internal bit clocks `PRI_TDM_IBIT` / `QUAD_TDM_IBIT` |
| Routing | MultiMedia1 (`hw:0,0`) -> both ports at once |
| Channels | landscape with the home button on the right (keyboard cover): top pair left, bottom pair right |
| Volume | per amplifier `<name> DAC Volume`, 255 = 0 dB, 0.5 dB per step (measured); the UCM profile sets 225 (-15 dB) and leaves the rest to software volume, because the amplifiers' speaker protection is not used |

New in the kernel:

- **RT5508 driver** (patches 0024/0025), ported from Samsung's vendor driver:
  regmap with per-register widths, the vendor init per chip revision, OTP
  temperature coefficient, boost bypass above 4.7 V VBAT, the vendor power
  sequences as DAPM events, I2S and 4-slot TDM. Playback only; the factory
  calibration and the speaker protection/EQ/DRC parameter sets are left out.
- **apq8096 machine driver** (0027): TDM back ends on the primary and
  quaternary interfaces (4 channels, I2S framing to the codecs, IBIT clock on
  while streaming).
- **q6routing** (0026): a stream can feed several ports. Mainline kept one
  port per stream, so turning on a second route silently replaced the first.
- **q6afe** (0022/0023): the TDM sync inversion, data delay and data-out
  settings from the DT never reached the DSP; and the slot mapping is only
  sent when the machine driver sets a channel map.

### What had to be found

1. **The ADSP rejects a TDM slot mapping** (`AFE_PARAM_ID_PORT_SLOT_MAPPING_CONFIG`)
   for a port alone in its group, even a correct one (`ADSP_EBADPARAM` at port
   start). Stock never sends one for a single group either.
2. **Without a slot mapping, channels land 32 bits apart.** Channel 0 goes to
   slot 0, channel 1 to slot 2, channels 2 and 3 fall outside the 64-bit
   frame. So every amplifier plays the first slot of a pair: pair 0 (slot 0)
   for the left channel, pair 1 (slot 2) for the right. Stock's
   `rt,chan_sel`/`rt,tdm_dac_slot` values, which pick slots 1 and 3 for two
   amplifiers, left those two silent here.
3. **The ADSP only fills a frame whose 4 slots are all active.** With 2
   channels and slot mask 0x3 it sends zeros; the back end is fixed to 4
   channels and the stereo stream is mapped up by the ADM.
4. **The data comes out on SD1** (TLMM 68 / 61), not on SD0 (TLMM 67) as
   first assumed; found with `tools/pinsample.c` during playback.
5. **The top pair is on another bus.** Only 0x36/0x37 locked to the primary
   bus' clock. Stock's DAI link lists all four amplifiers on PRI_TDM, but
   0x34/0x35 are wired to the quaternary pins.
6. **TLMM 58-61 are not TrustZone-owned on the Wi-Fi model.** The board DTS
   reserved them as the eSE SPI (as on the LTE model). Stock's Wi-Fi DT has the
   eSE node commented out; muxed for audio, they work and the tablet stays up.
   Only 25-28 (fingerprint) remain reserved.

### Checking without listening

`tools/rt5508-probe.sh` sets each amplifier's report selector (register 0xa2)
to 3, so the 24-bit report register 0xa3 returns the sample it is playing,
after channel/slot selection and DAC volume. During a -20 dBFS tone at DAC
volume 255 a playing amplifier reads about 52000, a silent one 0 to 20, and
a powered-down one 0x5d5d5d. That replaced most listening rounds: it showed
the empty slot 1 and then the slot each channel lands in.

`tools/pinsample.c` samples TLMM inputs through `/dev/mem` and counts ones
and edges; with a tone playing, the clock pins read ~50 % and a data pin
~24 % ones, an idle data pin 0 %.

## Testing headless

`tests/audio/` has the scripts that were used:

- `hp.sh` sets up the headphone routing;
- `mic2.sh` records two mics;
- `mclktest.sh` captures with and without playback;
- `echo.sh` and `pwecho.sh` record 5 s and play it back (ALSA, PipeWire);
- `pwstress.sh` runs play/record cycles with suspend and a WirePlumber
  restart;
- `spk.sh` plays left, right and both channels on the speakers at three
  amplifier volumes.

Notes:

- Stop `pw-record` with SIGINT (`timeout -s INT`), or the WAV keeps only its
  header.
- Background jobs over ssh: use `systemd-run` (or `systemd-run --user` with
  lingering enabled); `sudo` jobs die with the session.
