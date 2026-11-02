#!/bin/sh
# GNSS hardware test helper (docs/gnss.md). Run as root on the tablet,
# booted with a DT that enables the modem DSP (the main DT, both models).
#
#   gnss-test.sh backup [dir]  copy modemst1, modemst2, fsg and fsc (the
#                              modem's file system) to dir (default
#                              /root/gnss-backup-<date>) with SHA256SUMS
#   gnss-test.sh status        DT, firmware, modem DSP state, QRTR services,
#                              gnss-share, clock sync, kernel messages
#   gnss-test.sh up            start rmtfs read-only (it boots the modem)
#                              and tqftpserv; wait for the location service
#   gnss-test.sh share         run gnss-share with the qmi_loc driver in the
#                              foreground (GNSS_SHARE=<binary> to test a build)
#   gnss-test.sh fix [secs]    read NMEA for secs (default 120) and print a
#                              summary every 15 s: satellites, signal, fix;
#                              never the position
#   gnss-test.sh nmea [secs]   the NMEA itself, position fields masked
#   gnss-test.sh watch [secs]  report the GNSS engine turning on and off
#   gnss-test.sh down          stop rmtfs (which stops the modem DSP)
#
# fix and nmea read gnss-share's socket ($SOCK, default /run/gnss-share.sock),
# so a session runs while they run; RAW=1 runs the session with qmi-loc.py
# instead (no gnss-share), with $LOC_ARGS (e.g. LOC_ARGS="--events 0x3ff7")
# added to its command line. Needs: rmtfs, tqftpserv, qrtr (qrtr-lookup),
# socat (fix and nmea), python3 (watch and RAW=1; qmi-loc.py next to this
# script or at $QMI_LOC).
# Never writes to a partition: rmtfs runs with -r (writes from the modem
# stay in RAM) and backup only reads.
set -eu

FW=/lib/firmware/updates/qcom/msm8996/samsung/gts3
LOG=/var/log/gnss-rmtfs.log
SOCK=${SOCK:-/run/gnss-share.sock}
QMI_LOC=${QMI_LOC:-$(dirname "$0")/qmi-loc.py}

die() { echo "gnss-test: $*" >&2; exit 1; }

mss_rproc() {
	for r in /sys/class/remoteproc/remoteproc*; do
		case "$(readlink "$r/device/driver" 2>/dev/null)" in
		*/qcom-q6v5-mss) echo "$r"; return 0 ;;
		esac
	done
	return 1
}

# QRTR node that offers the location service (QMI LOC, service 16)
loc_node() {
	qrtr-lookup 2>/dev/null | awk '$1 == 16 { print $4; exit }'
}

backup() {
	dir=${1:-/root/gnss-backup-$(date +%Y%m%d-%H%M%S)}
	[ ! -e "$dir" ] || die "$dir exists, not overwriting"
	mkdir -p "$dir"
	for p in modemst1 modemst2 fsg fsc; do
		dev=/dev/disk/by-partlabel/$p
		[ -e "$dev" ] || die "no $p partition"
		dd if="$dev" of="$dir/$p.img" bs=64k 2>/dev/null
	done
	(cd "$dir" && sha256sum ./*.img > SHA256SUMS && cat SHA256SUMS)
	echo "gnss-test: backup in $dir - copy it off the tablet"
}

status() {
	n=$(find /proc/device-tree -maxdepth 2 -name 'remoteproc@2080000' | head -n1)
	echo "DT:       ${n:-no MSS node} $( [ -n "$n" ] && tr -d '\0' < "$n/status")"
	for f in mba.mbn modem.mdt; do
		[ -f "$FW/$f" ] && echo "firmware: $FW/$f" || echo "firmware: $FW/$f MISSING (run gts3-firmware)"
	done
	if r=$(mss_rproc); then
		echo "modem:    ${r##*/} state $(cat "$r/state"), recovery $(cat "$r/recovery" 2>/dev/null), coredump $(cat "$r/coredump" 2>/dev/null)"
	else
		echo "modem:    no MSS remoteproc (qcom_q6v5_mss not bound)"
	fi
	echo "rmtfs:    $(pgrep -l rmtfs || echo not running)"
	echo "tqftpserv: $(pgrep -l tqftpserv || echo not running)"
	echo "rmtfs mem: $(ls /dev/qcom_rmtfs_mem* 2>/dev/null || echo none)"
	echo "gnss-share: $(pgrep -l -x gnss-share || echo not running), socket $SOCK $( [ -S "$SOCK" ] && echo present || echo missing)"
	echo "clock:    $(timedatectl show -p NTPSynchronized --value 2>/dev/null | sed 's/^yes$/synchronized (time is injected)/; s/^no$/NOT synchronized (no time injection)/')"
	echo "QRTR services:"
	qrtr-lookup 2>/dev/null || echo "  (qrtr-lookup failed)"
	echo "kernel:"
	dmesg | grep -i -E 'remoteproc|q6v5|mss|modem|mba|qrtr|rmtfs|sysmon|fatal|pdr' | tail -n 40
}

up() {
	r=$(mss_rproc) || die "no MSS remoteproc: is the MSS node enabled in this DT?"
	[ -f "$FW/modem.mdt" ] || die "no $FW/modem.mdt: run gts3-firmware first"
	# Keep the first crash readable: no restart loop, no 100 MB core dump
	if [ "${KEEP_RECOVERY:-0}" != 1 ]; then
		echo disabled > "$r/recovery" 2>/dev/null || true
		echo disabled > "$r/coredump" 2>/dev/null || true
	fi
	if ! pgrep tqftpserv >/dev/null && command -v tqftpserv >/dev/null; then
		tqftpserv >/var/log/gnss-tqftpserv.log 2>&1 &
	fi
	if pgrep rmtfs >/dev/null; then
		echo "gnss-test: rmtfs already running ($(pgrep -l rmtfs))"
	else
		# -r: no writes to modemst1/2; -P: partitions by label;
		# -s: start the modem DSP once the service is up
		rmtfs -r -P -s -v >"$LOG" 2>&1 &
		echo "gnss-test: rmtfs started, log $LOG"
	fi
	i=0
	while [ $i -lt 60 ]; do
		[ "$(cat "$r/state")" = crashed ] && die "modem crashed: see 'gnss-test.sh status'"
		node=$(loc_node)
		if [ -n "$node" ]; then
			echo "gnss-test: modem $(cat "$r/state"), location service on QRTR node $node"
			return 0
		fi
		sleep 1
		i=$((i + 1))
	done
	die "no location service after 60 s (modem $(cat "$r/state")): see 'gnss-test.sh status'"
}

share() {
	bin=${GNSS_SHARE:-gnss-share}
	command -v "$bin" >/dev/null || die "no $bin"
	! pgrep -x gnss-share >/dev/null || die "gnss-share runs already: systemctl stop gnss-share"
	conf=/run/gnss-test-share.conf
	group=geoclue
	grep -q "^$group:" /etc/group || group=root
	printf 'socket="%s"\ngroup="%s"\ndevice_driver="qmi_loc"\nagps_directory="/var/cache/gnss-share"\n' \
		"$SOCK" "$group" > "$conf"
	echo "gnss-test: $bin -c $conf -d (Ctrl+C stops it)"
	exec "$bin" -c "$conf" -d
}

# NMEA for $1 seconds on stdout: from gnss-share, or from qmi-loc.py (RAW=1)
nmea_source() {
	if [ "${RAW:-0}" = 1 ]; then
		[ -f "$QMI_LOC" ] || die "no $QMI_LOC"
		# shellcheck disable=SC2086 # LOC_ARGS is a list of options
		python3 "$QMI_LOC" nmea "$1" ${LOC_ARGS:-}
	else
		[ -S "$SOCK" ] || die "no $SOCK: is gnss-share running? (or RAW=1)"
		command -v socat >/dev/null || die "needs socat (apk add socat)"
		timeout "$1" socat -u "UNIX-CONNECT:$SOCK" STDOUT || true
	fi
}

# Satellites, signal and fix state; never the position
summary() {
	awk -F, '
	function show(t,   k, n, talk, line, v, i, j, m, s) {
		split("", n); line = ""
		for (k in snr) {
			talk = substr(k, 1, 2); n[talk]++
			if (snr[k] > 0) s[++m] = snr[k]
		}
		for (i = 1; i <= m; i++)
			for (j = i + 1; j <= m; j++)
				if (s[j] > s[i]) { v = s[i]; s[i] = s[j]; s[j] = v }
		for (talk in n) line = line " " talk "=" n[talk]
		v = ""
		for (i = 1; i <= m; i++) v = v " " s[i]
		printf "%5ds in view:%s | C/N0 >0:%s | GSA fix %s | GGA quality %s, used %s, HDOP %s | RMC %s\n",
			t, (line == "" ? " none" : line), (v == "" ? " none" : v),
			(fix == "" ? "-" : fix), q, used, (hdop == "" ? "-" : hdop), (rmc == "" ? "-" : rmc)
		split("", snr)
	}
	{ sub(/\r$/, ""); sub(/\*[0-9A-Fa-f][0-9A-Fa-f]$/, ""); lines++ }
	/^\$..GSV,/ {
		for (i = 5; i + 3 <= NF; i += 4)
			if ($i != "") snr[substr($1, 2, 2) "," $i] = $(i + 3) + 0
		next
	}
	/^\$..GSA,/ { fix = $3; next }
	/^\$..RMC,/ { rmc = $3; next }
	/^\$..GGA,/ {
		gga++; q = $7 + 0; used = $8 + 0; hdop = $9
		if (q > 0) { fixes++; if (!first) { first = gga; printf "%5ds first fix\n", gga } }
		if (gga % 15 == 0) show(gga)
	}
	END {
		show(gga)
		printf "gnss-test: %d sentences, %d GGA, %d with a fix%s\n", lines, gga, fixes,
			(first ? sprintf(", first after about %d s", first) : "")
	}'
}

# NMEA with the position masked: latitude, longitude, altitude, geoid
# separation, magnetic variation, satellite elevation and azimuth;
# proprietary sentences ($P...) dropped
mask() {
	awk -F, -v OFS=, '
	function x(i) { if ($i != "") $i = "x" }
	{ sub(/\r$/, "") }
	/^\$P/ { next }
	/^\$..GGA,/ { x(3); x(5); x(10); x(12) }
	/^\$..GNS,/ { x(3); x(5); x(10); x(11) }
	/^\$..RMC,/ { x(4); x(6); x(11) }
	/^\$..GLL,/ { x(2); x(4) }
	/^\$..GSV,/ { for (i = 5; i + 3 <= NF; i += 4) { x(i + 1); x(i + 2) } }
	{ sub(/\*[0-9A-Fa-f][0-9A-Fa-f]$/, ""); print; fflush() }'
}

down() {
	if pkill -x rmtfs 2>/dev/null; then
		echo "gnss-test: rmtfs stopped (modem DSP stops with it)"
	fi
	pkill -x tqftpserv 2>/dev/null || true
}

case ${1:-} in
backup) shift; backup "$@" ;;
status) status ;;
up) up ;;
share) share ;;
fix) nmea_source "${2:-120}" | summary ;;
nmea) nmea_source "${2:-120}" | mask ;;
watch)
	[ -f "$QMI_LOC" ] || die "no $QMI_LOC"
	python3 "$QMI_LOC" watch "${2:-120}"
	;;
down) down ;;
*) sed -n '2,28p' "$0"; exit 1 ;;
esac
