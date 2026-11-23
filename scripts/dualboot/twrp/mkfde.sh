#!/bin/sh
# Collects what the installer zip needs for an encrypted Nura (setup's
# "Encrypt all of Nura", docs/encryption.md):
#   <out>/cs/       cryptsetup with its libraries and musl's loader, from a
#                   running Nura of the same image (over SSH): the installer
#                   runs it in the recovery to make the LUKS2 container
#   <out>/aarch64/  an offline repository with the unlock screen (unl0kr,
#                   postmarketos-buffybox-unl0kr) and postmarketOS's signed
#                   index (APKINDEX.tar.gz): the installer adds it to Nura's
#                   root, which trusts the index (postmarketOS signs the
#                   index; each package only carries its builder's own key)
# usage: mkfde.sh <user@nura> <out dir>
set -eu
[ $# -eq 2 ] || { sed -n '2,13p' "$0" >&2; exit 1; }
H=$1
OUT=$2
M=https://mirror.postmarketos.org/postmarketos/main/aarch64
mkdir -p "$OUT/cs" "$OUT/aarch64"
ssh "$H" 'set -e; rm -rf /tmp/gts3-cs && mkdir -p /tmp/gts3-cs && cd /tmp/gts3-cs
	c=$(command -v cryptsetup)
	cp -L "$c" .
	for l in $(ldd "$c" | awk "/=>/ { print \$3 }") /lib/ld-musl-aarch64.so.1; do cp -L "$l" .; done
	tar -czf ../gts3-cs.tgz .'
scp -q "$H:/tmp/gts3-cs.tgz" "$OUT/cs.tgz"
ssh "$H" 'rm -rf /tmp/gts3-cs /tmp/gts3-cs.tgz'
tar -xzf "$OUT/cs.tgz" -C "$OUT/cs"
rm "$OUT/cs.tgz"
# the index and the two packages it lists, from the same mirror
curl -sSfL -o "$OUT/aarch64/APKINDEX.tar.gz" "$M/APKINDEX.tar.gz"
for p in unl0kr postmarketos-buffybox-unl0kr; do
	v=$(tar -xzOf "$OUT/aarch64/APKINDEX.tar.gz" APKINDEX | awk -v p="$p" -v RS= '$0 ~ "\nP:" p "\n" { if (match($0, /\nV:[^\n]*/)) print substr($0, RSTART + 3, RLENGTH - 3) }')
	[ -n "$v" ] || { echo "$p isn't in the index" >&2; exit 1; }
	curl -sSfL -o "$OUT/aarch64/$p-$v.apk" "$M/$p-$v.apk"
done
ls -l "$OUT/cs" "$OUT/aarch64"
