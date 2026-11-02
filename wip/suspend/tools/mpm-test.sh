#!/bin/sh
# MPM deep-sleep test: waits for USB unplug, then two 40 s RTC-woken suspends;
# logs RPM vlow/vmin, the system domain, MPM and RTC interrupt counts.
LOG=/home/test/mpm.log
C=/sys/class/power_supply/sm5705-charger
snap() {
	echo "== $1 $(date +%T) up=$(cut -d' ' -f1 /proc/uptime)"
	for s in vlow vmin; do echo "   $s $(head -4 /sys/kernel/debug/qcom_stats/$s | tr '\n' ' ')"; done
	echo "   system $(sed -n 3p /sys/kernel/debug/pm_genpd/power-domain-system/idle_states)"
	echo "   irq $(grep -E 'qcom_mpm|rtc_alarm' /proc/interrupts | awk '{print $NF"="$2+$3+$4+$5}' | tr '\n' ' ')"
	echo "   suspends ok=$(cat /sys/power/suspend_stats/success) fail=$(cat /sys/power/suspend_stats/fail)"
}
echo "waiting for USB unplug $(date +%T)" > $LOG; sync
while [ "$(cat $C/online)" != 0 ]; do sleep 2; done
sleep 20
snap start >> $LOG; sync
for k in 1 2; do
	echo "suspend $k at $(date +%T)" >> $LOG; sync
	rtcwake -m mem -s 40 >/dev/null 2>&1
	snap "after suspend $k" >> $LOG; sync
	sleep 15
done
echo done >> $LOG; sync
