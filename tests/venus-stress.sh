#!/bin/sh
# Venus stability tests: every hardware decode is compared, frame for frame
# (md5 of the I420 frames), with libavcodec's software decoder (avdec_*) in
# the same GStreamer pipeline, so both sides pad rows alike; every test
# checks the kernel log for new Venus complaints. Clips are made locally
# with ffmpeg (libx264, libx265, libvpx) on the first run.
#
# usage: venus-stress.sh [test ...]     default: baseline soak parallel encdec
#                                        abort seek reschange runtimepm
#        venus-stress.sh main10         10-bit HEVC, not known to be safe:
#                                        run it on its own, last
#   SOAK=n (default 60) loops in soak, ROUNDS=n (default 5) in parallel,
#   ABORTS=n (default 30) in abort.
# Needs root for the kernel log (dmesg).

D=${VENUS_TEST_DIR:-/var/tmp/venus-stress}
mkdir -p "$D" && cd "$D" || exit 1
pass=0; fail=0; failed=""
klog_mark() { klog_n=$(dmesg | wc -l); }
klog_new() { dmesg | tail -n +$((klog_n + 1)) | grep -iE 'venus|vidc|v4l2|iommu|smmu|qcom-venus|hfi' ; }
result() { # name ok(0/1) detail
	if [ "$2" = 0 ]; then pass=$((pass + 1)); echo "  ok    $1 $3"
	else fail=$((fail + 1)); failed="$failed $1"; echo "  FAIL  $1 $3"; fi
}

# name file demux parser decoder
# (854x480 and 1080p are coded larger than shown: they check the crop)
CLIPS="h264-720p h264-720p.mp4 qtdemux h264parse v4l2h264dec
h264-1080p h264-1080p.mp4 qtdemux h264parse v4l2h264dec
h264-480p h264-480p.mp4 qtdemux h264parse v4l2h264dec
hevc-1080p hevc-1080p.mp4 qtdemux h265parse v4l2h265dec
hevc-480p hevc-480p.mp4 qtdemux h265parse v4l2h265dec
vp8-720p vp8-720p.webm matroskademux - v4l2vp8dec
vp9-1080p vp9-1080p.webm matroskademux - v4l2vp9dec"
NCLIPS=$(echo "$CLIPS" | wc -l)

make_clips() {
	src() { echo "testsrc2=size=$1:rate=30,format=yuv420p"; }
	# two encoder threads: four at full load overheat the bare board
	mk() { [ -s "$1" ] || ffmpeg -hide_banner -loglevel error -y -f lavfi -i "$(src $2)" -t ${4:-6} $3 -threads 2 "$1"; }
	mk h264-720p.mp4 1280x720 "-c:v libx264 -profile:v high -bf 3 -g 60"
	mk h264-1080p.mp4 1920x1080 "-c:v libx264 -profile:v high -bf 3 -g 60"
	mk h264-480p.mp4 854x480 "-c:v libx264 -profile:v high -bf 3 -g 60"
	mk hevc-1080p.mp4 1920x1080 "-c:v libx265 -x265-params log-level=error -tag:v hvc1"
	mk hevc-480p.mp4 854x480 "-c:v libx265 -x265-params log-level=error -tag:v hvc1"
	mk vp8-720p.webm 1280x720 "-c:v libvpx -b:v 2M"
	mk vp9-1080p.webm 1920x1080 "-c:v libvpx-vp9 -b:v 2M -row-mt 1"
	mk hevc10-1080p.mp4 1920x1080 "-c:v libx265 -pix_fmt yuv420p10le -x265-params log-level=error -tag:v hvc1" 3
	# one H.264 elementary stream switching 480p -> 720p -> 480p (no
	# container: concatenated MPEG-TS restarts its timestamps and tsdemux
	# drops frames; sizes a multiple of 16, so no crop is involved)
	if [ ! -s h264-switch.h264 ]; then
		for s in 640x480 1280x720 640x480; do
			ffmpeg -hide_banner -loglevel error -y -f lavfi -i "$(src $s)" -t 2 \
				-c:v libx264 -bf 0 -g 30 -threads 2 -f h264 "part-$s-$$.h264"
			echo "part-$s-$$.h264"
		done > parts.txt
		cat $(cat parts.txt) > h264-switch.h264; rm -f $(cat parts.txt) parts.txt
	fi
	echo "$CLIPS" | while read n f dm pa de; do
		[ -s "$f.ref.md5" ] || hw_md5 "$f" $dm $pa $(swdec $de) > "$f.ref.md5"
	done
	[ -s h264-switch.h264.ref.md5 ] || hw_md5 h264-switch.h264 - h264parse avdec_h264 > h264-switch.h264.ref.md5
	# md5 of no output: the software pipeline failed
	! grep -l d41d8cd98f00b204e9800998ecf8427e *.ref.md5 2>/dev/null | sed 's/^/  no reference: /' | grep .
}

swdec() { echo "$1" | sed 's/^v4l2\(.*\)dec$/avdec_\1/'; } # v4l2h264dec -> avdec_h264
hw_md5() { # file demux parser decoder ("-": none)
	m=$2; [ "$m" = - ] && m= || m="! $m"
	p=$3; [ "$p" = - ] && p= || p="! $p"
	timeout 120 gst-launch-1.0 -q filesrc location="$1" $m $p ! $4 ! videoconvert ! \
		video/x-raw,format=I420 ! fdsink 2>/dev/null | md5sum | cut -d' ' -f1
}
dec_ok() { # name file demux parser decoder -> 0 if bit-exact
	[ "$(hw_md5 "$2" "$3" "$4" "$5")" = "$(cat "$2.ref.md5")" ]
}

t_baseline() {
	echo "== baseline: each codec once, bit-exact"
	echo "$CLIPS" | while read n f dm pa de; do klog_mark
		dec_ok $n $f $dm $pa $de; r=$?; k=$(klog_new)
		[ -z "$k" ] || { r=1; echo "$k" | sed 's/^/        kernel: /'; }
		echo "$n $r"; done > res.txt
	while read n r; do result "baseline/$n" $r; done < res.txt
}

t_soak() {
	n=${SOAK:-60}; echo "== soak: $n decodes, random codec order"
	m0=$(awk '/MemAvailable/{print $2}' /proc/meminfo); klog_mark; bad=0; i=0
	while [ $i -lt $n ]; do
		l=$(echo "$CLIPS" | awk -v r=$(( $(od -An -N2 -tu2 /dev/urandom) % NCLIPS + 1 )) 'NR==r')
		set -- $l; dec_ok "$@" || { bad=$((bad + 1)); echo "        mismatch: $1 (run $i)"; }
		i=$((i + 1))
	done
	m1=$(awk '/MemAvailable/{print $2}' /proc/meminfo); k=$(klog_new)
	[ -z "$k" ] || { bad=$((bad + 1)); echo "$k" | sed 's/^/        kernel: /' | head -5; }
	result soak $([ $bad = 0 ] && echo 0 || echo 1) "($n runs, $bad bad, MemAvailable $((m0 / 1024)) -> $((m1 / 1024)) MB)"
}

t_parallel() {
	r=${ROUNDS:-5}; echo "== parallel: 4 decoders at once, $r rounds"
	klog_mark; bad=0; i=0
	while [ $i -lt $r ]; do
		echo "$CLIPS" | head -4 | {
			while read n f dm pa de; do
				( dec_ok $n $f $dm $pa $de && echo "ok $n" || echo "bad $n" ) &
			done; wait; } > par.txt; sleep 1
		b=$(grep -c '^bad' par.txt); [ "$b" = 0 ] || { bad=$((bad + b)); grep '^bad' par.txt | sed "s/^/        round $i: /"; }
		i=$((i + 1))
	done
	k=$(klog_new); [ -z "$k" ] || { bad=$((bad + 1)); echo "$k" | sed 's/^/        kernel: /' | head -5; }
	result parallel $([ $bad = 0 ] && echo 0 || echo 1) "($r rounds x 4)"
}

t_encdec() {
	echo "== encode + decode at the same time"
	klog_mark
	timeout 120 gst-launch-1.0 -q videotestsrc num-buffers=300 ! video/x-raw,width=1920,height=1080,framerate=30/1 ! \
		v4l2h264enc ! h264parse ! mp4mux ! filesink location=enc-out.mp4 > /dev/null 2>&1 &
	e=$!
	dec_ok hevc-1080p hevc-1080p.mp4 qtdemux h265parse v4l2h265dec; d=$?
	wait $e; er=$?
	n=$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 enc-out.mp4 2>/dev/null)
	k=$(klog_new); [ -z "$k" ] || echo "$k" | sed 's/^/        kernel: /' | head -5
	result encdec $([ $d = 0 ] && [ $er = 0 ] && [ "$n" = 300 ] && [ -z "$k" ] && echo 0 || echo 1) "(decode bit-exact: $([ $d = 0 ] && echo yes || echo no), encoded $n/300 frames)"
}

t_abort() {
	n=${ABORTS:-30}; echo "== abort: kill a decode mid-stream $n times, then decode cleanly"
	klog_mark; i=0
	while [ $i -lt $n ]; do
		l=$(echo "$CLIPS" | awk -v r=$((i % NCLIPS + 1)) 'NR==r'); set -- $l
		p=$4; [ "$p" = - ] && p= || p="! $p"
		gst-launch-1.0 -q filesrc location="$2" ! $3 $p ! $5 ! fakesink > /dev/null 2>&1 &
		pid=$!; sleep 0.$(( $(od -An -N1 -tu1 /dev/urandom) % 9 + 1 )); kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null
		i=$((i + 1))
	done
	sleep 1; ok=0
	echo "$CLIPS" | while read nm f dm pa de; do dec_ok $nm $f $dm $pa $de || echo bad; done > ab.txt
	k=$(klog_new); [ -z "$k" ] || echo "$k" | sed 's/^/        kernel: /' | head -5
	result abort $([ ! -s ab.txt ] && [ -z "$k" ] && echo 0 || echo 1) "($n kills, then $(grep -c bad ab.txt) of $NCLIPS clips wrong)"
}

t_seek() {
	echo "== seek: 20 random seeks in H.264 and HEVC"
	klog_mark
	cat > seek.py <<'EOF'
import sys, random, gi
gi.require_version("Gst", "1.0"); from gi.repository import Gst
Gst.init(None); bad = 0
for f, parse, dec in (("h264-1080p.mp4", "h264parse", "v4l2h264dec"), ("hevc-1080p.mp4", "h265parse", "v4l2h265dec")):
    p = Gst.parse_launch(f"filesrc location={f} ! qtdemux ! {parse} ! {dec} ! fakesink sync=false")
    p.set_state(Gst.State.PAUSED); p.get_state(10 * Gst.SECOND)
    bus = p.get_bus()
    for i in range(10):
        t = random.uniform(0, 5.5) * Gst.SECOND
        if not p.seek_simple(Gst.Format.TIME, Gst.SeekFlags.FLUSH | Gst.SeekFlags.KEY_UNIT, int(t)):
            bad += 1; print(f"        seek refused: {f} {t / Gst.SECOND:.2f}s")
        p.set_state(Gst.State.PLAYING)
        m = bus.timed_pop_filtered(3 * Gst.SECOND, Gst.MessageType.ERROR | Gst.MessageType.EOS)
        if m and m.type == Gst.MessageType.ERROR:
            bad += 1; print("        error:", m.parse_error()[0].message); break
        p.set_state(Gst.State.PAUSED); p.get_state(10 * Gst.SECOND)
    p.set_state(Gst.State.NULL)
sys.exit(1 if bad else 0)
EOF
	timeout 300 python3 seek.py; r=$?
	k=$(klog_new); [ -z "$k" ] || { r=1; echo "$k" | sed 's/^/        kernel: /' | head -5; }
	result seek $([ $r = 0 ] && echo 0 || echo 1)
}

t_reschange() {
	echo "== resolution change inside one H.264 stream (480p -> 720p -> 480p)"
	klog_mark
	# frame counts: every frame scaled to 320x180 I420 (86400 bytes)
	nfr() { timeout 120 gst-launch-1.0 -q filesrc location=h264-switch.h264 ! h264parse ! $1 ! \
		videoconvert ! videoscale ! video/x-raw,format=I420,width=320,height=180 ! fdsink 2>/dev/null |
		wc -c | awk '{print $1 / 86400}'; }
	hw=$(nfr v4l2h264dec); sw=$(nfr avdec_h264)
	same=$([ "$(hw_md5 h264-switch.h264 - h264parse v4l2h264dec)" = "$(cat h264-switch.h264.ref.md5)" ] && echo yes || echo no)
	k=$(klog_new); [ -z "$k" ] || echo "$k" | sed 's/^/        kernel: /' | head -5
	result reschange $([ "$hw" = "$sw" ] && [ $same = yes ] && [ -z "$k" ] && echo 0 || echo 1) "(frames: hardware $hw, software $sw; bit-exact: $same)"
}

t_runtimepm() {
	echo "== runtime PM: Venus idles off between sessions, 10 cycles"
	v=$(ls -d /sys/bus/platform/devices/*.video-codec 2>/dev/null | head -1)
	[ -n "$v" ] || { result runtimepm 1 "(no video-codec device)"; return; }
	klog_mark; bad=0; i=0; st=""
	while [ $i -lt 10 ]; do
		sleep 6; st="$st $(cat $v/power/runtime_status)"
		dec_ok h264-720p h264-720p.mp4 qtdemux h264parse v4l2h264dec || bad=$((bad + 1))
		i=$((i + 1))
	done
	k=$(klog_new); [ -z "$k" ] || { bad=$((bad + 1)); echo "$k" | sed 's/^/        kernel: /' | head -5; }
	result runtimepm $([ $bad = 0 ] && echo 0 || echo 1) "(state while idle:$(echo $st | tr ' ' '\n' | sort | uniq -c | tr '\n' ' '))"
}

t_main10() {
	echo "== 10-bit HEVC (Main 10): not known to be safe"
	klog_mark
	timeout 60 gst-launch-1.0 -q filesrc location=hevc10-1080p.mp4 ! qtdemux ! h265parse ! v4l2h265dec ! fakesink > m10.log 2>&1; r=$?
	k=$(klog_new); [ -z "$k" ] || echo "$k" | sed 's/^/        kernel: /' | head -8
	sed 's/^/        gst: /' m10.log | head -4
	dec_ok h264-720p h264-720p.mp4 qtdemux h264parse v4l2h264dec; after=$?
	result main10 $([ $r = 0 ] && [ -z "$k" ] && echo 0 || echo 1) "(gst exit $r; Venus afterwards: $([ $after = 0 ] && echo works || echo BROKEN))"
}

[ "$(id -u)" = 0 ] || { echo "run as root (kernel log)"; exit 1; }
echo "venus-stress on $(uname -r), clips in $D"; make_clips || exit 1
tests=${*:-baseline soak parallel encdec abort seek reschange runtimepm}
for t in $tests; do t_$t; done
echo "== $pass passed, $fail failed${failed:+:$failed}"
[ $fail = 0 ]
