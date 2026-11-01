#!/bin/sh
# GNSS hardware test helper (docs/gnss.md). Run as root on the tablet,
# booted with dts/test/test-gnss.dts (SM-T820) or test-gnss-lte.dts (SM-T825).
#
#   gnss-test.sh backup [dir]  copy modemst1, modemst2, fsg and fsc (the
#                              modem's file system) to dir (default
#                              /root/gnss-backup-<date>) with SHA256SUMS
#   gnss-test.sh status        DT, firmware, modem DSP state, QRTR services,
#                              recent kernel messages
#   gnss-test.sh up            start rmtfs read-only (it boots the modem)
#                              and tqftpserv; wait for the location service
#   gnss-test.sh fix [secs]    run one GNSS session (default 120 s) and
#                              print position reports and satellites
#   gnss-test.sh nmea [secs]   the same, NMEA sentences
#   gnss-test.sh down          stop rmtfs (which stops the modem DSP)
#
# Needs: rmtfs, qrtr (qrtr-lookup), qmi-utils (qmicli; libqmi's qmi-proxy
# keeps the session's client alive between qmicli calls), tqftpserv.
# Never writes to a partition: rmtfs runs with -r (writes from the modem
# stay in RAM) and backup only reads.
set -eu

FW=/lib/firmware/updates/qcom/msm8996/samsung/gts3
LOG=/var/log/gnss-rmtfs.log

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
	echo "QRTR services:"
	qrtr-lookup 2>/dev/null || echo "  (qrtr-lookup failed)"
	echo "kernel:"
	dmesg | grep -i -E 'remoteproc|q6v5|mss|modem|mba|qrtr|rmtfs|sysmon|fatal|pdr' | tail -n 40
}

up() {
	r=$(mss_rproc) || die "no MSS remoteproc: not booted with a GNSS test DT?"
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

session() { # what secs
	node=${NODE:-$(loc_node)}
	[ -n "$node" ] || die "no location service on QRTR: run 'gnss-test.sh up'"
	dev=qrtr://$node
	qmicli -p -d "$dev" --loc-get-engine-lock || true
	qmicli -p -d "$dev" --loc-get-operation-mode || true
	out=$(qmicli -p -d "$dev" --client-no-release-cid --loc-start) || die "--loc-start failed: $out"
	echo "$out"
	cid=$(echo "$out" | sed -n "s/.*CID: '\([0-9]*\)'.*/\1/p")
	[ -n "$cid" ] || die "no client ID in the --loc-start output"
	trap 'qmicli -p -d "$dev" --client-cid="$cid" --loc-stop >/dev/null 2>&1 || true' EXIT
	case $1 in
	fix) follow="--loc-follow-position-report --loc-follow-gnss-sv-info" ;;
	nmea) follow="--loc-follow-nmea" ;;
	esac
	echo "gnss-test: session on CID $cid for $2 s (Ctrl+C ends it early)"
	# shellcheck disable=SC2086
	timeout -s INT "$2" qmicli -p -d "$dev" --client-cid="$cid" \
		--client-no-release-cid $follow || true
}

down() {
	pkill -x rmtfs 2>/dev/null && echo "gnss-test: rmtfs stopped (modem DSP stops with it)" || true
	pkill -x tqftpserv 2>/dev/null || true
}

case ${1:-} in
backup) shift; backup "$@" ;;
status) status ;;
up) up ;;
fix|nmea) session "$1" "${2:-120}" ;;
down) down ;;
*) sed -n '2,21p' "$0"; exit 1 ;;
esac
