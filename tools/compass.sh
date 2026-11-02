#!/bin/sh
# A flat-only compass from the SLPI magnetometer (qcom-smgr-mag), to check
# its calibration and axes by hand. No iio-sensor-proxy compass exists yet
# (see docs/sensors.md).
#
# usage: compass.sh [SECONDS] [OFFSET] [mirror]
#   SECONDS  how long to run after calibration (default 180)
#   OFFSET   degrees added to the heading (default 0)
#   mirror   negate the heading before the offset (if east reads as west)
#
# 1. Calibration, 20 s: lay the tablet flat and turn it slowly through a
#    full circle. The cover magnets and the tablet's own steel move with
#    it, so calibrate with the cover the way it will be during the test.
#    Away from the PC case and speakers is better.
# 2. Then it prints the direction the top edge points (the edge opposite
#    the USB port, portrait). Keep the tablet flat: there is no tilt
#    compensation, because KWin holds the accelerometer buffer.
#
# The heading is magnetic. A phone compass set to true north differs by the
# local declination.
#
# Needs kernel patch 0145 (the magnetometer's mount matrix); before it the
# heading is 180 degrees off: pass OFFSET 180.
#
# Reads only the magnetometer buffer, which nothing else uses; it does not
# need iio-sensor-proxy stopped. Root. Needs od and awk (busybox has both).
name=qcom-smgr-mag
run=${1:-180}
offset=${2:-0}
mirror=0
[ "$3" = mirror ] && mirror=1
rate=20
cal=20

dev=
for d in /sys/bus/iio/devices/iio:device*; do
	[ "$(cat "$d/name" 2>/dev/null)" = "$name" ] && dev=$d && break
done
[ -n "$dev" ] || { echo "no IIO device named $name" >&2; exit 1; }
node=/dev/$(basename "$dev")

for e in "$dev"/scan_elements/*_en; do
	case $e in
	*timestamp_en) echo 0 > "$e" ;;
	*) echo 1 > "$e" ;;
	esac
done
old_rate=$(cat "$dev/in_magn_sampling_frequency")
echo $rate > "$dev/in_magn_sampling_frequency" || exit 1
scale=$(cat "$dev/in_magn_scale")
matrix=$(cat "$dev/in_mount_matrix" 2>/dev/null)
[ -n "$matrix" ] || matrix="1, 0, 0; 0, 1, 0; 0, 0, 1"

echo 64 > "$dev/buffer/length"
echo 1 > "$dev/buffer/enable" || exit 1
trap 'echo 0 > "$dev/buffer/enable"; echo "$old_rate" > "$dev/in_magn_sampling_frequency"' EXIT
trap 'exit 130' INT TERM

total=$(((cal + run) * rate))
i=0
# od in batches of 5 samples, so its output reaches awk every 250 ms
while [ $i -lt $total ]; do
	od -An -v -t d4 -w12 -N 60 "$node" || break
	i=$((i + 5))
done |
awk -v s="$scale" -v m="$matrix" -v n=$((cal * rate)) -v rate=$rate \
    -v off="$offset" -v mirror=$mirror '
function card(h) {
	return substr("N NEE SES SWW NWN ", int((h + 22.5) / 45) * 2 + 1, 2)
}
BEGIN {
	gsub(/[;,]/, " ", m)
	split(m, M, " ")
	pi = atan2(0, -1)
	printf "Calibrating: lay it flat and turn it slowly through a full circle.\n"
}
NF == 3 {
	rx = $1 * s; ry = $2 * s; rz = $3 * s
	# device frame: x right, y to the top edge, z out of the screen
	x = M[1] * rx + M[2] * ry + M[3] * rz
	y = M[4] * rx + M[5] * ry + M[6] * rz
	if (NR <= n) {
		X[NR] = x; Y[NR] = y
		if (NR == 1 || x < x0) x0 = x
		if (NR == 1 || x > x1) x1 = x
		if (NR == 1 || y < y0) y0 = y
		if (NR == 1 || y > y1) y1 = y
		if (NR % rate == 0) {
			printf "\r  %2d s left ", (n - NR) / rate
			fflush()
		}
		if (NR < n)
			next
		cx = (x0 + x1) / 2; cy = (y0 + y1) / 2
		ax = (x1 - x0) / 2; ay = (y1 - y0) / 2
		if (ax <= 0 || ay <= 0) {
			print "\nno field change: is the magnetometer streaming?"
			exit 1
		}
		for (k = 1; k <= n; k++) {
			b = int((atan2((Y[k] - cy) / ay, (X[k] - cx) / ax) + pi) / (2 * pi) * 12) % 12
			bin[b] = 1
		}
		c = 0
		for (b in bin)
			c++
		printf "\r  done: %d of 12 directions covered, centre %+.3f %+.3f gauss, radius ratio %.2f\n",
		       c, cx, cy, ax / ay
		if (c < 10)
			print "  (too few directions: run it again and turn a full circle)"
		print "Point the top edge (opposite the USB port) and read the heading:"
		fflush()
		next
	}
	u = (x - cx) / ax; v = (y - cy) / ay
	su = 0.7 * su + 0.3 * u; sv = 0.7 * sv + 0.3 * v
	h = atan2(-su, sv) * 180 / pi
	if (mirror)
		h = -h
	h += off
	while (h < 0) h += 360
	while (h >= 360) h -= 360
	printf "\r  heading %3d deg  %s   ", int(h + 0.5) % 360, card(h)
	fflush()
}
END { print "" }'
