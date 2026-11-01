#!/bin/sh
# Test the GNSS tools on a PC against a fake QMI LOC service (fake-loc.py)
# on the PC's own QRTR node. Needs AF_QIPCRTR (the qrtr module), python3,
# socat; GNSS_SHARE=<gnss-share binary with the qmi_loc driver> adds the
# gnss-share tests. Refuses to run where a real location service exists.
#
#   tests/gnss/run.sh [workdir]
set -eu

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
work=${1:-$(mktemp -d)}
mkdir -p "$work"
fails=0
pids=

cleanup() {
	for p in $pids; do kill "$p" 2>/dev/null || true; done
	wait 2>/dev/null || true
}
trap cleanup EXIT

ok() { echo "ok   $*"; }
bad() { echo "FAIL $*"; fails=$((fails + 1)); }
check() { # description command...
	d=$1; shift
	if "$@"; then ok "$d"; else bad "$d"; fi
}

fake() { # log args...
	log=$1; shift
	python3 "$here/fake-loc.py" "$@" 2>"$log" &
	pids="$pids $!"
	sleep 0.5
	grep -q published "$log" || { cat "$log"; exit 1; }
}

stop_fake() {
	for p in $pids; do kill "$p" 2>/dev/null || true; done
	wait 2>/dev/null || true
	pids=
}

t=$top/tools/gnss-test.sh

# 1. Raw session (qmi-loc.py), engine watcher alongside
fake "$work/fake1.log"
python3 "$top/tools/qmi-loc.py" watch 9 2>"$work/watch.log" &
pids="$pids $!"
sleep 0.5
RAW=1 sh "$t" fix 5 >"$work/fix.out" 2>"$work/fix.err"
RAW=1 sh "$t" nmea 3 >"$work/nmea.out" 2>"$work/nmea.err"
sleep 1.5
stop_fake
check "fix: summary lines" grep -q 'in view: GP=4' "$work/fix.out"
check "fix: first fix found" grep -q 'first fix' "$work/fix.out"
check "fix: no position" sh -c "! grep -q '0000\.0000' '$work/fix.out'"
check "nmea: sentences" grep -q '^[$]GPGGA,120000.00,x,N,x,E,' "$work/nmea.out"
check "nmea: no position" sh -c "! grep -q '0000\.0000' '$work/nmea.out'"
check "nmea: GSV elevation/azimuth masked" grep -q '^[$]GPGSV,1,1,04,01,x,x,' "$work/nmea.out"
check "nmea: proprietary dropped" sh -c "! grep -q PQXFI '$work/nmea.out'"
check "raw session stopped" grep -q 'session stopped' "$work/nmea.err"
check "raw session: time injected or skipped" grep -q -E 'injected the system time|not injecting' "$work/nmea.err"
check "watch: engine on" grep -q 'engine on' "$work/watch.log"
check "watch: engine off" grep -q 'engine off' "$work/watch.log"

# 2. The modem restarts during a raw session
fake "$work/fake2.log" --restart-after 2
RAW=1 sh "$t" nmea 6 >"$work/restart.out" 2>"$work/restart.err"
stop_fake
check "restart: service gone" grep -q 'location service gone' "$work/restart.err"
check "restart: two START requests" test "$(grep -c 'request 0x22' "$work/fake2.log")" -eq 2
check "restart: NMEA after the restart" test "$(grep -c GPGGA "$work/restart.out")" -ge 4

# 3. gnss-share with the qmi_loc driver
if [ -n "${GNSS_SHARE:-}" ]; then
	sock=$work/gnss-share.sock
	printf 'socket="%s"\ngroup="%s"\ndevice_driver="qmi_loc"\n' "$sock" "$(id -gn)" >"$work/gnss-share.conf"
	fake "$work/fake3.log" --restart-after 11
	"$GNSS_SHARE" -c "$work/gnss-share.conf" -d >"$work/gnss-share.log" 2>&1 &
	pids="$pids $!"
	sleep 1
	check "gnss-share: no session without clients" sh -c "! grep -q 'request 0x22' '$work/fake3.log'"
	SOCK=$sock sh "$t" fix 4 >"$work/share-fix.out" 2>&1
	sleep 1
	check "gnss-share: fix summary" grep -q 'first fix' "$work/share-fix.out"
	check "gnss-share: session stopped with the last client" grep -q 'request 0x23' "$work/fake3.log"
	n_start=$(grep -c 'request 0x22' "$work/fake3.log")
	sleep 2
	check "gnss-share: no session while idle" test "$(grep -c 'request 0x22' "$work/fake3.log")" -eq "$n_start"
	# Two clients, and the fake restarts during their session (at 11 s)
	SOCK=$sock sh "$t" nmea 6 >"$work/share-a.out" 2>&1 &
	a=$!
	SOCK=$sock sh "$t" fix 3 >"$work/share-b.out" 2>&1
	wait "$a"
	sleep 1
	stop_fake
	check "gnss-share: second client" grep -q 'in view' "$work/share-b.out"
	check "gnss-share: followed the restart" grep -q 'location service gone' "$work/gnss-share.log"
	check "gnss-share: no position in its log" sh -c "! grep -q '0000\.0000' '$work/gnss-share.log'"
	check "gnss-share: stopped after the restart too" test "$(grep -c 'request 0x23' "$work/fake3.log")" -ge 2
fi

echo "logs in $work"
[ "$fails" -eq 0 ] || { echo "$fails failed"; exit 1; }
