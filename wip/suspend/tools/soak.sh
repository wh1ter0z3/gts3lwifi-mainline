#!/bin/sh
# Unattended soak of the deep idle states + RPM clock fix: suspend/resume
# cycles, then Wi-Fi, audio and a CPU burst after each; ~18 minutes.
LOG=/home/test/soak.log
END=$(( $(cut -d. -f1 /proc/uptime) + 1080 ))
echo "start $(date +%T) $(uname -v | cut -c1-30)" > $LOG; sync
n=0; fail=0
while [ $(cut -d. -f1 /proc/uptime) -lt $END ]; do
	n=$((n + 1))
	rtcwake -m mem -s 15 >/dev/null 2>&1; r1=$?
	sleep 8
	gw=$(ip route | awk '/default.*wlan0/ {print $3; exit}')
	ping -c1 -W3 ${gw:-10.46.11.1} >/dev/null 2>&1; r2=$?
	timeout 10 aplay -q -D plughw:0,0 -f S16_LE -r 48000 -c 2 -d 1 /dev/zero >/dev/null 2>&1; r3=$?
	for c in 1 2 3 4; do timeout 5 sh -c 'while :; do :; done' & done; wait
	[ $r1$r2$r3 = 000 ] || fail=$((fail + 1))
	echo "$(date +%T) cycle $n suspend=$r1 wifi=$r2 audio=$r3 ok=$((n - fail))/$n" >> $LOG; sync
	sleep 20
done
echo "done $(date +%T) cycles=$n failures=$fail suspends=$(cat /sys/power/suspend_stats/success) $(dmesg | grep -ciE 'oops|BUG:|stall|lockup') kernel-warnings" >> $LOG; sync
