#!/bin/sh
# USB-plugged suspend test: only the RTC may wake the tablet.
LOG=/home/test/susp-usb.log
echo "start $(date +%T)" > $LOG
echo 0 > /sys/power/pm_async
n=0
for f in $(find /sys/devices -path '*/power/wakeup' 2>/dev/null); do
	[ "$(cat $f 2>/dev/null)" = enabled ] || continue
	case $f in
	*rtc*) echo "keep $f" >> $LOG ;;
	*) echo disabled > $f 2>/dev/null && n=$((n + 1)) ;;
	esac
done
echo "disabled $n wakeup sources" >> $LOG
sync
rtcwake -m mem -s ${1:-20} >> $LOG 2>&1
echo "resumed $(date +%T)" >> $LOG
