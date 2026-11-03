#!/bin/sh
# HEVC decode through Venus, for each clip in clips.txt from venus-hevc-clips.py:
# first decode speed with nothing reading the frames, then the md5 of the frames
# against a software decode made on the host. Stops at the first kernel complaint.
#
# usage: venus-hevc.sh [-m] [DIR]    DIR holds clips.txt, default the current directory
#        -m also runs main10/, which isn't known to be safe: see docs/venus.md
main10=0
[ "$1" = "-m" ] && { main10=1; shift; }
cd "${1:-.}" || exit 1
tmp=/var/tmp/venus-hevc.$$
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT
plugin() { gst-inspect-1.0 v4l2h265dec >/dev/null 2>&1; }
plugin || { echo "no v4l2h265dec"; exit 1; }

# kernel lines since mark that mean we should stop
bad() {
	dmesg | tail -n +"$1" | grep -E -i \
		'arm-smmu|context fault|venus.*(fatal|sfr|error|timeout)|Err_Fatal|Internal error|BUG:|hung_task'
}

src() { # file demux
	case "$2" in
	mp4) echo "filesrc location=$1 ! qtdemux ! h265parse" ;;
	raw) echo "filesrc location=$1 ! h265parse" ;;
	esac
}

check() { # name file demux frames size md5
	mark=$(($(dmesg | wc -l) + 1))
	t0=$(date +%s.%N)
	# shellcheck disable=SC2046
	timeout -s KILL 300 gst-launch-1.0 -q $(src "$2" "$3") ! v4l2h265dec ! fakesink sync=false \
		>"$tmp/gst.log" 2>&1
	rc1=$?
	t1=$(date +%s.%N)
	if [ "$rc1" -ne 0 ]; then
		echo "$1: decode rc=$rc1"
		tail -3 "$tmp/gst.log"
	fi
	if [ "$4" -gt 0 ]; then
		speed=$(awk "BEGIN{printf \"%.1f fps\", $4/($t1-$t0)}")
	else
		speed=$(awk "BEGIN{printf \"%.1f s\", $t1-$t0}")
	fi
	b=$(bad "$mark")
	if [ -n "$b" ]; then
		echo "$1: STOP, kernel says:"
		echo "$b" | head -8
		exit 2
	fi

	rm -f "$tmp/fifo" "$tmp/rc"
	mkfifo "$tmp/fifo"
	wc -c <"$tmp/fifo" >"$tmp/bytes" &
	# shellcheck disable=SC2046
	sum=$( { timeout -s KILL 600 gst-launch-1.0 -q $(src "$2" "$3") ! v4l2h265dec ! \
		videoconvert ! video/x-raw,format=I420 ! fdsink fd=1 2>"$tmp/gst2.log"
		echo $? >"$tmp/rc"; } | tee "$tmp/fifo" | md5sum | cut -d' ' -f1)
	wait
	rc2=$(cat "$tmp/rc")
	bytes=$(cat "$tmp/bytes")
	if [ "$sum" = "$6" ]; then
		r=IDENTICAL
	else
		r="DIFFERENT, $bytes bytes of $5"
	fi
	echo "$1: $r, $speed, rc $rc1/$rc2"
	[ "$rc2" -ne 0 ] && tail -3 "$tmp/gst2.log"
	b=$(bad "$mark")
	if [ -n "$b" ]; then
		echo "$1: STOP, kernel says:"
		echo "$b" | head -8
		exit 2
	fi
	return 0
}

while read -r name file demux w h frames size md5; do
	cap=$(cat /sys/class/power_supply/sm5705-battery/capacity 2>/dev/null || echo 100)
	if [ "$cap" -lt 15 ]; then
		echo "battery at $cap %, stopping"
		exit 3
	fi
	check "$name" "$file" "$demux" "$frames" "$size" "$md5"
done <clips.txt

if [ "$main10" = 1 ]; then
	echo "main10: decode only, no comparison"
	mark=$(($(dmesg | wc -l) + 1))
	timeout -s KILL 120 gst-launch-1.0 filesrc location=main10/hevc-1080p30-main10.mp4 ! \
		qtdemux ! h265parse ! v4l2h265dec ! fakesink sync=false 2>&1 | tail -5
	bad "$mark"
fi
