#!/vendor/bin/sh
# Copyright (C) 2026 The gts3lwifi-mainline project
# SPDX-License-Identifier: Apache-2.0
#
# Mixer routes of the q6 card "S3" (WCD9335 behind the ADSP, four RT5508
# speaker amplifiers), from postmarketOS's UCM profile
# (Samsung/gts3lwifi/HiFi.conf), as the tinyhal setup had them:
#   playback PCM 0: MultiMedia1 -> PRIMARY_TDM_RX_0 (bottom pair) and
#                   QUAT_TDM_RX_0 (top pair); DAC Volume 255 is 0 dB in
#                   0.5 dB steps, 15 dB below full scale (no speaker
#                   protection in use), the rest in software
#   capture PCM 2:  MultiMedia3 <- SLIMBUS_0_TX <- AIF1_CAP <- SLIM TX0/TX1
#                   <- DEC0/DEC1 <- AMIC3 (main mic) / AMIC4 (sub mic)
# The SM-T825 has no top pair: those controls fail there, harmlessly.

# The card appears once the ADSP has booted
n=0
until [ -e /dev/snd/controlC0 ] || [ $n -ge 60 ]; do sleep 1; n=$((n + 1)); done

set_ctl() { /vendor/bin/tinymix -D 0 "$1" "$2" > /dev/null 2>&1; }

set_ctl "PRIMARY_TDM_RX_0 Audio Mixer MultiMedia1" 1
set_ctl "QUAT_TDM_RX_0 Audio Mixer MultiMedia1" 1
set_ctl "Top 1 DAC Volume" 225
set_ctl "Top 2 DAC Volume" 225
set_ctl "Bottom 1 DAC Volume" 225
set_ctl "Bottom 2 DAC Volume" 225

set_ctl "MultiMedia3 Mixer SLIMBUS_0_TX" 1
set_ctl "AIF1_CAP Mixer SLIM TX0" 1
set_ctl "AIF1_CAP Mixer SLIM TX1" 1
set_ctl "SLIM TX0 MUX" DEC0
set_ctl "SLIM TX1 MUX" DEC1
set_ctl "ADC MUX0" AMIC
set_ctl "ADC MUX1" AMIC
set_ctl "AMIC MUX0" ADC3
set_ctl "AMIC MUX1" ADC4
set_ctl "ADC3 Volume" 16
set_ctl "ADC4 Volume" 16
