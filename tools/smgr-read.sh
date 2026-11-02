#!/bin/sh
# Read samples from one of the SLPI's IIO devices (qcom_smgr) and print them
# in physical units: m/s^2, rad/s, gauss or lux.
#
# usage: smgr-read.sh NAME [COUNT] [RATE]
#   NAME   qcom-smgr-accel, -gyro, -mag or -light
#   COUNT  samples to read (default 20)
#   RATE   sampling_frequency to set first, in Hz (default: leave it)
#
# For the magnetometer it also prints |B| and, at the end, the centre and
# radii of the X/Y readings: turn the tablet slowly through 360 degrees while
# it lies flat (COUNT 600 at 10 Hz is a minute). A centre far from 0 (more
# than a few hundredths of a gauss) means no hard-iron correction; X and Y
# radii that differ much mean no soft-iron one.
#
# The buffer can have one reader only: stop iio-sensor-proxy first
# (systemctl stop iio-sensor-proxy; start it again afterwards, and with
# KWin running reboot instead: see docs/sensors.md). Needs od and
# awk (busybox has both). Root.
name=${1:?usage: smgr-read.sh NAME [COUNT] [RATE]}
count=${2:-20}
rate=$3

dev=
for d in /sys/bus/iio/devices/iio:device*; do
	[ "$(cat "$d/name" 2>/dev/null)" = "$name" ] && dev=$d && break
done
[ -n "$dev" ] || { echo "no IIO device named $name" >&2; exit 1; }
node=/dev/$(basename "$dev")

# Every channel but the timestamp: the 3-axis devices give 12 bytes per
# sample, the light sensor 4.
words=0
for e in "$dev"/scan_elements/*_en; do
	case $e in
	*timestamp_en) echo 0 > "$e" ;;
	*) echo 1 > "$e"; words=$((words + 1)) ;;
	esac
done
scale=$(cat "$dev"/in_*_scale 2>/dev/null | head -n 1)
[ -n "$scale" ] || scale=1
[ -n "$rate" ] && for f in "$dev"/in_*sampling_frequency; do
	echo "$rate" > "$f" || exit 1
done
echo "$name: $words channel(s), scale $scale, $(cat "$dev"/in_*sampling_frequency | head -n 1) Hz"

echo 64 > "$dev/buffer/length"
echo 1 > "$dev/buffer/enable" || exit 1
trap 'echo 0 > "$dev/buffer/enable"' EXIT INT TERM

od -An -v -t d4 -w$((words * 4)) -N $((words * 4 * count)) "$node" |
awk -v s="$scale" -v mag="$([ "$name" = qcom-smgr-mag ] && echo 1)" '
{
	line = ""
	for (i = 1; i <= NF; i++)
		line = line sprintf(" %12.5f", $i * s)
	if (mag) {
		x = $1 * s; y = $2 * s; z = $3 * s
		line = line sprintf("   |B| %.3f", sqrt(x * x + y * y + z * z))
		if (NR == 1 || x < xmin) xmin = x
		if (NR == 1 || x > xmax) xmax = x
		if (NR == 1 || y < ymin) ymin = y
		if (NR == 1 || y > ymax) ymax = y
	}
	print line
}
END {
	if (mag && NR > 1)
		printf "X/Y centre %.3f %.3f, radii %.3f %.3f gauss\n",
			(xmax + xmin) / 2, (ymax + ymin) / 2,
			(xmax - xmin) / 2, (ymax - ymin) / 2
}'
