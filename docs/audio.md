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

Not done yet:

- Headset mic (AMIC2), jack detection (WCD9335 MBHC; `apq8096.c` sets up no
  jack), and mic gain tuning.

## Hardware and firmware

| | |
|---|---|
| ADSP firmware | Samsung-signed `image/adsp.mdt` + `.b00`-`.b09` from the **modem** partition (`ADSP.8996.2.7.1.C1-00013`; the `dsp` partition only holds ADSP modules). Copy to `/lib/firmware/qcom/msm8996/samsung/gts3/`; not in this repo |
| ADSP memory | the image spans 0x2100000 bytes, more than msm8996.dtsi's 27 MB `adsp_mem`. The test DT grows it to 0x8ea00000-0x90f00000 and moves the unused `slpi_mem` to 0x91700000, inside the stock `peripheral_region` |
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
  DEC0/DEC1 <- AMIC3/AMIC4.

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
   stayed dead until reboot. It also never reset `queue_ptr`. Patch 0011 fixes
   both. PipeWire hits this constantly, `aplay`/`arecord` never.
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
   timeout`) while ping worked, and shutdown hung. Patches 0105-0112
   backport the mainline (7.1) fixes by Bjorn Andersson and Bartosz
   Golaszewski; 0109 is the one that registers the callbacks after
   creating the NGD. 11 boots in a row were clean with them.
7. **Closing a stream in the DSP timed out** (`q6asm-dai ... CMD 10bcd
   timeout`, 5 s): at the PipeWire start in 6 of 24 boots, while
   WirePlumber probed the PCMs, and before the sound of about one camera
   recording in ten. PipeWire prepares a capture PCM twice, and the second
   prepare closes the DSP stream that the first one opened, with reads
   still queued. The DSP rejects those reads, and `q6asm_stream_callback()`
   stored the rejections in `ac->result` like any other response. One that
   landed after the close's own response hid it from
   `q6asm_ac_send_cmd_sync()`, which waited out its 5 s, or which read the
   rejection's status instead (`DSP returned error[a]`, seen once). Patch
   0113 backports the upstream fix, 8a066a81ee0c ("ASoC: qcom: q6asm: drop
   DSP responses for closed data streams", in v7.0 and the 6.12, 6.18 and
   6.19 stable branches, after our v6.19.5 base), which drops the `EOS`,
   `READ_V2` and `WRITE_V2` responses without touching `ac->result`. We had
   written the same change for `EOS` and `READ_V2` before finding it. With
   a debug print in that version, a rejection arrived after the close's
   response in 8 of 20 recordings at 120 fps, and all 20 started their
   sound on time; 10 more did on its final build and 10 of 10 on the
   backport, and no boot with either had the timeout.

Also seen: unbinding and rebinding the sound card fails because wcd9335 does
not free its SLIM slave interrupt (`request_irq ... -EBUSY`). Reboot instead
of reloading the machine driver.

## Speakers

All four speakers play: checked by ear with `tests/audio/spk.sh` (left,
right and both channels) and on every amplifier with the probe below. PipeWire offers a "Speakers"
sink (UCM device `Speaker`, priority above the headphones, as there is no jack
detection yet).

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
