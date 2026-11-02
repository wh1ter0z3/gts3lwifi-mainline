#!/bin/sh
# Audio path smoke test: UCM HiFi verb, then 3 s of silence to the speakers.
LOG=/home/test/audio-test.log
echo "start $(date +%T) $(uname -v | cut -c1-30) $(grep -o 'clk_ignore_unused' /proc/cmdline)" > $LOG; sync
timeout 20 alsaucm -c hw:0 set _verb HiFi >> $LOG 2>&1; echo "ucm rc=$?" >> $LOG; sync
timeout 15 aplay -D plughw:0,0 -f S16_LE -r 48000 -c 2 -d 3 /dev/zero >> $LOG 2>&1; echo "aplay rc=$?" >> $LOG; sync
echo done >> $LOG; sync
