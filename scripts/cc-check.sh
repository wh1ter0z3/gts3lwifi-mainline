#!/bin/sh
# Replay the chroot's recorded clang command for kernel objects with the host clang (compile check only).
cd ~/Projects/samsung-tab-s3/linux/.output || exit 1
T=$(mktemp -d); rc=0
for o in "$@"; do
	d=$(dirname "$o"); b=$(basename "$o")
	cmd=$(sed -n '1s/^savedcmd_[^:]*:= //p' "$d/.$b.cmd")
	[ -n "$cmd" ] || { echo "no cmd for $o"; rc=1; continue; }
	cmd=$(printf '%s' "$cmd" | sed -e "s| -Wp,-MMD,[^ ]*||" -e "s| -o $o | -o $T/check.o |" -e "s| -Werror=unknown-warning-option| -Wno-unknown-warning-option|")
	printf '== %s\n' "$o"
	sh -c "$cmd" 2>&1 | grep -v "^$"; [ "${PIPESTATUS:-0}" = 0 ] || rc=1
done
rm -rf "$T"; exit $rc
