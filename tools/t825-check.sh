#!/bin/sh
# Check a Galaxy Tab S3 LTE (SM-T825) on the `t825-modem` branch (Linux 7.3.1
# with the modem DSP on): modem and SIM, GNSS service, sensors and rotation,
# touch keys, headphone jack, GPU clocks, video and camera devices, power.
#
#   sudo sh t825-check.sh [--no-backup] [--no-ask] [--video] [--suspend]
#
#   --no-backup  skip the backup of the modem's partitions (done once, into
#                ./t825-backup-<date>/, if no such folder exists yet)
#   --no-ask     skip the hands-on steps (rotation, touch keys, jack)
#   --video      also run tests/venus-stress.sh, shortened (about 25 min)
#   --suspend    also suspend once for 30 s with an RTC wake-up
#
# Writes everything to ./t825-check-<date>.log as well. IMEI, IMSI, ICCID
# and phone numbers are masked in the log. Changes it makes: the backup
# folder and the log. It installs nothing; checks whose tool is missing
# (qmicli, mmcli, qrtr-lookup, monitor-sensor, cam, v4l2-ctl, rtcwake) say so.

DIR=$(cd "$(dirname "$0")" && pwd)
BACKUP=1 ASK=1 VIDEO=0 SUSPEND=0
for a in "$@"; do
	case $a in
	--no-backup) BACKUP=0 ;; --no-ask) ASK=0 ;;
	--video) VIDEO=1 ;; --suspend) SUSPEND=1 ;;
	*) echo "unknown option $a"; exit 1 ;;
	esac
done
[ -t 0 ] || ASK=0

[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
# Run once more with the output going through tee into the log
if [ -z "${T825_LOG:-}" ]; then
	T825_LOG=$PWD/t825-check-$(date +%Y%m%d-%H%M).log
	export T825_LOG
	sh "$0" "$@" 2>&1 | tee "$T825_LOG"
	exit
fi

ok() { echo "  ok    $*"; }
bad() { echo "  FAIL  $*"; }
note() { echo "  --    $*"; }
have() { command -v "$1" >/dev/null 2>&1; }
check() { if eval "$2" >/dev/null 2>&1; then ok "$1"; else bad "$1"; fi; }
# Mask anything that looks like an IMEI, IMSI, ICCID, MEID or phone number
mask() { sed -E 's/[0-9A-Fa-f]{14,}/<masked>/g; s/\+?[0-9][0-9 -]{8,}[0-9]/<masked>/g'; }
ask() {
	[ "$ASK" = 1 ] || return 1
	printf '\n  >>> %s\n      press Enter when ready (s + Enter skips) ' "$1" > /dev/tty
	read -r r < /dev/tty
	[ "$r" != s ]
}
indent() { sed 's/^/   /'; }

echo "== $(date)  $(tr -d '\0' < /proc/device-tree/model)"
echo "   kernel $(uname -r)  $(uname -v)"
have apk && apk info -v 2>/dev/null |
	grep -E '^(linux-postmarketos-qcom-msm8996|device-samsung-gts3lwifi|kwin|modemmanager|libqmi|iio-sensor-proxy)-[0-9]' | indent
echo "   system: $(systemctl is-system-running 2>/dev/null)"
systemctl --failed --no-legend 2>/dev/null | sed 's/^/   failed unit: /'
grep -q 'samsung,gts3llte' /proc/device-tree/compatible 2>/dev/null ||
	bad "this is not the SM-T825 device tree (wrong DTB or not a T825)"

echo "== Backup of the modem's partitions"
if [ "$BACKUP" = 0 ]; then
	note "skipped (--no-backup)"
elif ls -d "$PWD"/t825-backup-* >/dev/null 2>&1; then
	note "already there: $(ls -d "$PWD"/t825-backup-* | head -1)"
else
	B=$PWD/t825-backup-$(date +%Y%m%d-%H%M)
	mkdir -p "$B"
	for p in modemst1 modemst2 fsg fsc efs EFS persist; do
		[ -e /dev/disk/by-partlabel/$p ] || continue
		dd if=/dev/disk/by-partlabel/$p of="$B/$p.img" bs=1M 2>/dev/null &&
			ok "$p ($(du -h "$B/$p.img" | cut -f1))" || bad "$p"
	done
	(cd "$B" && sha256sum *.img > SHA256SUMS)
	note "copy $B off the tablet and keep it (it holds the IMEI and calibration)"
fi

echo "== Modem DSP"
RP=$(ls -d /sys/bus/platform/devices/2080000.remoteproc/remoteproc/remoteproc* 2>/dev/null | head -1)
if [ -n "$RP" ]; then
	ok "MSS remoteproc $(basename "$RP"): $(cat "$RP"/state)"
	[ "$(cat "$RP"/state)" = running ] || bad "the modem DSP is not running"
else
	bad "no MSS remoteproc: the DT has no modem DSP (not the t825-modem build?)"
fi
FW=/lib/firmware/updates/qcom/msm8996/samsung/gts3
check "modem firmware copied from the modem partition" "[ -e $FW/modem.mdt ] && [ -e $FW/mba.mbn ]"
for s in rmtfs tqftpserv gnss-share ModemManager; do
	echo "   $s: $(systemctl is-active $s 2>/dev/null)"
done
dmesg | grep -iE 'remoteproc|q6v5|mss|modem|qrtr|rmtfs' | tail -15 | indent

echo "== QRTR services (modem node 0 should list DMS 2, NAS 3, WDS 1, UIM 11, WMS 5, LOC 16)"
if have qrtr-lookup; then
	timeout 10 qrtr-lookup 2>&1 | head -40 | indent
else
	note "qrtr-lookup missing (apk add qrtr)"
fi

echo "== QMI (IMEI etc. masked)"
if have qmicli; then
	for c in --dms-get-ids --dms-get-revision --dms-get-operating-mode \
		 --uim-get-card-status --nas-get-serving-system \
		 --nas-get-signal-strength --nas-get-system-info; do
		echo "   -- qmicli $c"
		timeout 20 qmicli -d qrtr://0 $c 2>&1 | mask | head -40 | indent
	done
else
	note "qmicli missing (apk add libqmi)"
fi

echo "== ModemManager (numbers masked)"
if have mmcli; then
	timeout 20 mmcli -L 2>&1 | indent
	timeout 20 mmcli -m any 2>&1 | mask | indent
	timeout 20 mmcli -m any --messaging-list-sms 2>&1 | mask | indent
else
	note "ModemManager not installed (apk add modemmanager), skipped"
fi

echo "== GNSS service"
if [ -x "$DIR/gnss-test.sh" ]; then
	sh "$DIR/gnss-test.sh" status 2>&1 | tail -25 | indent
	note "a fix needs the sky: sh $DIR/gnss-test.sh fix 600 (outdoors or at a window)"
else
	note "gnss-test.sh not next to this script, skipped"
fi

echo "== Sensors and rotation"
for d in /sys/bus/iio/devices/iio:device*; do
	[ -e "$d/name" ] && echo "   $(basename "$d"): $(cat "$d/name")"
done
ACC=$(dirname "$(ls /sys/bus/iio/devices/iio:device*/in_accel_x_raw 2>/dev/null | head -1)" 2>/dev/null)
accel() { [ -n "$ACC" ] && echo "x=$(cat "$ACC/in_accel_x_raw") y=$(cat "$ACC/in_accel_y_raw") z=$(cat "$ACC/in_accel_z_raw")"; }
[ -n "$ACC" ] && ok "accelerometer $(basename "$ACC"): $(accel)" || bad "no accelerometer channels"
# iio-sensor-proxy's orientation, read over D-Bus while monitor-sensor holds the claim
orient() {
	if have busctl; then
		busctl get-property net.hadess.SensorProxy /net/hadess/SensorProxy \
			net.hadess.SensorProxy AccelerometerOrientation 2>/dev/null | sed 's/^s //; s/"//g'
	elif have gdbus; then
		gdbus call --system --dest net.hadess.SensorProxy --object-path /net/hadess/SensorProxy \
			--method org.freedesktop.DBus.Properties.Get net.hadess.SensorProxy \
			AccelerometerOrientation 2>/dev/null | tr -d "()<>',"
	fi
}
if have monitor-sensor && [ "$ASK" = 1 ]; then
	monitor-sensor --accel > /dev/null 2>&1 &
	MP=$!
	for pos in "portrait, Home button at the bottom (how you'd hold a phone)" \
		   "landscape, Home button on the RIGHT" \
		   "portrait upside down, Home button at the top" \
		   "landscape, Home button on the LEFT"; do
		if ask "hold the tablet upright: $pos"; then
			sleep 2
			echo "   $pos: $(orient); raw $(accel)"
		fi
	done
	kill $MP 2>/dev/null
	note "also: does the screen turn the right way each time, and is there snow during the turn?"
else
	note "hands-on rotation check skipped (no monitor-sensor or --no-ask)"
fi

echo "== Touch keys"
EV=$(awk '/^N: Name=/{n=$0} /^H: Handlers=/{if (n ~ /touchkey|tc305k|tm2/) {match($0, /event[0-9]+/); print substr($0, RSTART, RLENGTH)}}' /proc/bus/input/devices | head -1)
if [ -n "$EV" ]; then
	ok "touch key input device /dev/input/$EV"
	if ask "touch Back and Recent a few times within 8 seconds after Enter"; then
		T=$(mktemp)
		timeout 8 cat /dev/input/$EV > "$T" 2>/dev/null
		n=$(wc -c < "$T"); rm -f "$T"
		[ "$n" -gt 0 ] && ok "key events seen ($n bytes)" || bad "no key events"
	fi
else
	bad "no touch key input device"
fi

echo "== Headphone jack"
CARD=$(grep -l -i gts3 /proc/asound/card*/id 2>/dev/null | head -1 | sed 's|.*card\([0-9]*\)/id|\1|')
jack() {
	amixer -c "${CARD:-0}" contents 2>/dev/null | awk '
		/^numid=/ { name = ($0 ~ /[Jj]ack/) ? $0 : ""; sub(/.*name=/, "", name) }
		/: values=/ && name != "" { sub(/.*: values=/, ""); print name " = " $0; name = "" }'
}
if have amixer; then
	jack | sed 's/^/   unplugged: /'
	if ask "plug in a headset (with mic) now"; then
		sleep 1; jack | sed 's/^/   plugged:   /'
		ask "press the headset button once, then unplug it" && sleep 1
	fi
else
	note "amixer missing (apk add alsa-utils)"
fi

echo "== GPU, CPU, thermal"
for d in /sys/class/devfreq/*; do
	[ -e "$d/cur_freq" ] && echo "   $(basename "$d"): $(cat "$d/governor") $(($(cat "$d/cur_freq") / 1000000)) MHz (max $(($(cat "$d/max_freq") / 1000000)))"
done
for p in /sys/devices/system/cpu/cpufreq/policy*; do
	echo "   $(basename "$p"): $(cat "$p/scaling_governor") $(($(cat "$p/scaling_cur_freq") / 1000)) MHz"
done
for z in /sys/class/thermal/thermal_zone*; do printf '%s=%s ' "$(cat "$z/type")" "$(($(cat "$z/temp") / 1000))"; done | fold -w 100 | indent; echo

echo "== Video and camera"
have v4l2-ctl && v4l2-ctl --list-devices 2>/dev/null | grep -A3 -iE 'venus|camss' | indent
if have cam; then cam -l 2>&1 | indent; else note "cam missing (apk add libcamera-tools)"; fi
if [ "$VIDEO" = 1 ]; then
	VS=$DIR/../tests/venus-stress.sh
	[ -e "$VS" ] || VS=$DIR/venus-stress.sh
	if [ -e "$VS" ]; then
		SOAK=10 ROUNDS=2 ABORTS=10 sh "$VS" baseline soak parallel abort reschange runtimepm 2>&1 | indent
	else
		bad "venus-stress.sh not found"
	fi
fi

echo "== Power"
for p in /sys/class/power_supply/*; do
	echo "   $(basename "$p"): $(cat "$p/status" 2>/dev/null) $(cat "$p/capacity" 2>/dev/null)% $(cat "$p/type" 2>/dev/null) $(cat "$p/voltage_now" 2>/dev/null)uV $(cat "$p/current_now" 2>/dev/null)uA" | sed 's/  */ /g'
done
check "Bluetooth controller" "[ -e /sys/class/bluetooth/hci0 ]"
check "Wi-Fi up" "ip -br link | grep -qE '^wlan0 +UP'"
check "microSD present" "[ -e /dev/mmcblk1 ]"
if [ "$SUSPEND" = 1 ]; then
	if have rtcwake; then
		echo "   suspending for 30 s ($(cat /sys/power/mem_sleep))"
		t0=$(date +%s)
		rtcwake -m mem -s 30 >/dev/null 2>&1
		echo "   back after $(($(date +%s) - t0)) s"
		sleep 5
		check "Wi-Fi up after resume" "ip -br link | grep -qE '^wlan0 +UP'"
		[ -n "$RP" ] && echo "   modem after resume: $(cat "$RP"/state)"
		dmesg | grep -iE 'PM: suspend|PM: resume|Freezing|error' | tail -8 | indent
	else
		note "rtcwake missing"
	fi
fi

echo "== Kernel errors and warnings (last 40)"
dmesg -l err,warn 2>/dev/null | grep -vE 'pp done time out|callbacks suppressed' | tail -40 | indent
echo "== done, log: $T825_LOG"
