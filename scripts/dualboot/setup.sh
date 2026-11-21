#!/usr/bin/env bash
# Galaxy Tab S3 (SM-T820, SM-T825): installs Nura (postmarketOS), our Android
# (a GSI on our open vendor), or both, from a Linux PC, and sets Nura up with
# your account, Wi-Fi, time zone and keyboard. Needs only adb and fastboot.
#
# The tablet does the work: this script starts our OrangeFox on it from RAM
# (nothing is flashed for that), hands it the release's installer zip and
# streams Nura's root and the GSI to it over USB (docs/dual-boot.md, "Staged
# install"). setup.ps1 does the same on Windows.
#
# usage: setup.sh [--gsi <GSI .img or .img.xz>] [--ssh-key <public key file>]
#                 [<folder with the release files>]
#   --ssh-key: turn on Nura's SSH server with that key (key logins only);
#   without it, setup asks whether to turn SSH on
#   the folder (default: this script's): gts3-dualboot-*.zip,
#   OrangeFox-mainline-<model>-fastboot-boot.img, and nura-root.img.xz (+ its
#   .sha256) for Nura; a GSI (*system*.img(.xz) or *gsi*.img(.xz)) for Android
# The tablet: in lk2nd's fastboot mode (Volume Down at power-on, then
# fastboot), or already in our OrangeFox.
# ERASES the userdata partition (internal storage). The microSD card, the
# tablet's firmware partitions and its recovery partition are not touched.
set -euo pipefail

T=/tmp/gts3-setup
DIR= GSI= SSHKEYFILE=
while [ $# -gt 0 ]; do
	case "$1" in
	--gsi) GSI=$2; shift 2 ;;
	--ssh-key) SSHKEYFILE=$2; shift 2 ;;
	-h | --help) sed -n '2,23p' "$0"; exit 0 ;;
	*) DIR=$1; shift ;;
	esac
done
DIR=${DIR:-$(cd "$(dirname "$0")" && pwd)}

say() { printf '%s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
die() { printf '\nerror: %s\n' "$*" >&2; exit 1; }
ask() {	# <prompt> <default>: the answer, or the default
	local a
	read -r -p "$1${2:+ [$2]}: " a
	printf '%s' "${a:-$2}"
}
yes_no() {	# <prompt>: true for yes
	local a
	read -r -p "$1 [y/N]: " a
	case "$a" in [yY]*) return 0 ;; *) return 1 ;; esac
}

for t in adb fastboot sha256sum; do
	command -v "$t" > /dev/null || die "$t not found (adb and fastboot: Android platform tools, e.g. the android-tools package)"
done

# The release files
ZIP=$(ls -t "$DIR"/gts3-dualboot-*.zip 2> /dev/null | head -n 1) || true
[ -n "$ZIP" ] || die "no gts3-dualboot-*.zip in $DIR"
ROOT=$DIR/nura-root.img.xz
[ -f "$ROOT" ] || ROOT=
if [ -z "$GSI" ]; then
	GSI=$(ls "$DIR"/*system*.img "$DIR"/*system*.img.xz "$DIR"/*gsi*.img "$DIR"/*gsi*.img.xz 2> /dev/null |
		grep -v nura-root | head -n 1) || true
fi
[ -z "$GSI" ] || [ -f "$GSI" ] || die "no such GSI: $GSI"
[ -n "$ROOT$GSI" ] || die "put nura-root.img.xz (Nura) and/or a GSI (Android) in $DIR"

step "What to install"
if [ -n "$ROOT" ] && [ -n "$GSI" ]; then
	say "1) Nura only   2) Android only   3) both side by side"
	case "$(ask "Choose" 3)" in
	1) SETUP=nura GSI= ;;
	2) SETUP=android ROOT= ;;
	3) SETUP=both ;;
	*) die "1, 2 or 3" ;;
	esac
elif [ -n "$ROOT" ]; then
	SETUP=nura
	say "Nura only (put a GSI in $DIR for Android too)"
else
	SETUP=android
	say "Android only (no nura-root.img.xz in $DIR)"
fi
[ -z "$ROOT" ] || say "Nura: $(basename "$ROOT")"
[ -z "$GSI" ] || say "Android: $(basename "$GSI")"

# What the GSI is: xz (unpacked on the tablet) or a raw image
GSIFMT=
if [ -n "$GSI" ]; then
	case "$GSI" in
	*.xz) GSIFMT=xz ;;
	*.img)
		[ "$(od -An -tx1 -N4 "$GSI" | tr -d ' ')" != 3aff26ed ] ||
			die "$(basename "$GSI") is a sparse image: unpack it first (simg2img $(basename "$GSI") system-raw.img)"
		GSIFMT=raw ;;
	*) die "the GSI must be a .img or .img.xz (unzip a zip first)" ;;
	esac
fi

# Nura's settings, checked here first (gts3-firstsetup checks them again)
A=$(mktemp -d)
trap 'rm -rf "$A"' EXIT
chmod 700 "$A"
put() { printf '%s' "$2" > "$A/$1"; }
if [ -n "$ROOT" ]; then
	step "Nura: your account and settings"
	while :; do
		v=$(ask "User name (lowercase)" "$(printf '%s' "${USER:-user}" | tr 'A-Z' 'a-z')")
		printf '%s' "$v" | grep -Eq '^[a-z_][a-z0-9_-]{0,30}$' && break
		say "  lowercase letters, digits, - and _, starting with a letter"
	done
	put username "$v"
	v=$(ask "Full name" "")
	[ -z "$v" ] || put fullname "$(printf '%s' "$v" | tr -d ':,')"
	while :; do
		read -r -s -p "Password: " p1; echo
		read -r -s -p "Password again: " p2; echo
		[ -n "$p1" ] && [ "$p1" = "$p2" ] && break
		say "  empty, or not the same; again"
	done
	put password "$p1"
	unset p1 p2
	while :; do
		v=$(ask "Tablet name on the network" "tab-s3")
		printf '%s' "$v" | grep -Eq '^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?$' && break
		say "  letters, digits and -"
	done
	put hostname "$v"
	tz=$(timedatectl show -p Timezone --value 2> /dev/null || readlink /etc/localtime 2> /dev/null | sed 's|.*/zoneinfo/||') || true
	put timezone "$(ask "Time zone" "${tz:-UTC}")"
	loc=${LANG:-en_US.UTF-8}
	case "$loc" in C* | POSIX) loc=en_US.UTF-8 ;; esac
	put locale "$(ask "Language (locale)" "$loc")"
	kb=$(localectl status 2> /dev/null | sed -n 's/.*X11 Layout: *//p' | head -n 1) || true
	put keyboard "$(ask "Keyboard layout(s)" "${kb:-us}")"
	cc=$(printf '%s' "$loc" | sed -n 's/^[a-z]*_\([A-Z][A-Z]\).*/\1/p')
	put country "$(ask "Country, for Wi-Fi (two letters)" "${cc:-US}" | tr 'a-z' 'A-Z')"
	v=$(ask "Wi-Fi network to join (empty: none)" "")
	if [ -n "$v" ]; then
		put wifi_ssid "$v"
		while :; do
			read -r -s -p "Wi-Fi password (empty: an open network): " p1; echo
			[ -z "$p1" ] || [ ${#p1} -ge 8 ] && [ ${#p1} -le 63 ] && break
			say "  8 to 63 characters"
		done
		[ -z "$p1" ] || put wifi_psk "$p1"
		unset p1
	fi
	# SSH: log in from a PC (USB: 172.16.42.1, Wi-Fi: <tablet name>.local)
	if [ -z "$SSHKEYFILE" ] && yes_no "Turn on SSH (log in to the tablet from a PC)?"; then
		k=
		for f in "$HOME/.ssh/id_ed25519.pub" "$HOME/.ssh/id_ecdsa.pub" "$HOME/.ssh/id_rsa.pub"; do
			[ -f "$f" ] && { k=$f; break; }
		done
		SSHKEYFILE=$(ask "Your public key file (empty: log in with the password)" "$k")
		[ -n "$SSHKEYFILE" ] || put ssh yes
	fi
	if [ -n "$SSHKEYFILE" ]; then
		[ -f "$SSHKEYFILE" ] || die "no such key file: $SSHKEYFILE"
		case "$SSHKEYFILE" in *.pub) ;; *) die "$SSHKEYFILE: give the public key (.pub), not the private one" ;; esac
		put ssh yes
		put ssh_key "$(grep -E '^(ssh-|ecdsa-|sk-)' "$SSHKEYFILE")"
		[ -s "$A/ssh_key" ] || die "$SSHKEYFILE has no public key line"
	fi
fi

# The tablet: in lk2nd (start our OrangeFox) or already in it
step "The tablet"
state() {
	adb devices 2> /dev/null | grep -q 'recovery$' && { echo recovery; return; }
	fastboot devices 2> /dev/null | grep -q . && { echo fastboot; return; }
	echo none
}
say "Connect it by USB, in lk2nd's fastboot mode (Volume Down at power-on, then"
say "fastboot) or in our OrangeFox. Waiting..."
until [ "$(state)" != none ]; do sleep 2; done
if [ "$(state)" = fastboot ]; then
	gv() { fastboot getvar "$1" 2>&1 | sed -n "s/^$1:[[:space:]]*//p" | head -n 1; }
	case "$(gv lk2nd:compatible)" in
	samsung,gts3lwifi) MODEL=gts3lwifi ;;
	samsung,gts3llte) MODEL=gts3llte ;;
	*) die "not a Galaxy Tab S3 in lk2nd" ;;
	esac
	FOX=$DIR/OrangeFox-mainline-$MODEL-fastboot-boot.img
	[ -f "$FOX" ] || die "no $(basename "$FOX") in $DIR"
	say "SM-$([ $MODEL = gts3lwifi ] && echo T820 || echo T825) in lk2nd: starting our OrangeFox (from RAM)..."
	fastboot boot "$FOX" > /dev/null 2>&1 || die "fastboot boot failed"
	n=0
	until [ "$(state)" = recovery ]; do
		sleep 2; n=$((n + 1))
		[ $n -lt 60 ] || die "OrangeFox didn't show up over adb"
	done
fi
# OrangeFox restarts USB once while it starts (MTP, which our kernel doesn't
# have): wait until its main page is up, then a little more
n=0
until adb shell "grep -q \"Set page: 'main'\" /tmp/recovery.log" < /dev/null > /dev/null 2>&1; do
	sleep 2; n=$((n + 1))
	[ $n -lt 60 ] || die "OrangeFox didn't finish starting"
done
sleep 5
adb wait-for-recovery
MODEL=$(adb shell cat /proc/device-tree/compatible < /dev/null | tr '\0' ' ' | grep -o 'samsung,gts3l[a-z]*' | head -n 1) || true
case "$MODEL" in
samsung,gts3lwifi) say "SM-T820 in OrangeFox" ;;
samsung,gts3llte) say "SM-T825 in OrangeFox" ;;
*) die "the recovery isn't our OrangeFox on a Galaxy Tab S3 ($MODEL)" ;;
esac

step "Last check"
say "This ERASES the tablet's internal storage (userdata): Android's apps and"
say "data, a postmarketOS install. The microSD card is not touched."
say "Installing: $SETUP"
read -r -p "Type yes to go on: " a
[ "$a" = yes ] || die "stopped, nothing changed"

# The installer on the tablet, in RAM; the test overrides go along
ENV="MODEL="
[ -z "${GTS3_DATA_DEV:-}" ] || ENV="$ENV DATA_DEV=$GTS3_DATA_DEV"
[ -z "${GTS3_BOOT_DEV:-}" ] || ENV="$ENV BOOT_DEV=$GTS3_BOOT_DEV"
stage() {	# <stage> [env...] [< stdin]: the installer's messages, its status
	local s=$1
	shift
	adb shell -T "cd $T && env $ENV GTS3_STAGE=$s $* sh $T/ub 3 1 $T/installer.zip" |
		sed -n 's/^ui_print \(..*\)/  \1/p'
	return "${PIPESTATUS[0]}"
}
step "Copying the installer"
adb shell "rm -rf $T && mkdir -p $T" < /dev/null > /dev/null
push() {	# <file or dir> <tablet path>: once more if the USB link drops
	adb push "$1" "$2" > /dev/null 2>&1 && return 0
	sleep 3
	adb wait-for-recovery
	adb push "$1" "$2" > /dev/null
}
push "$ZIP" "$T/installer.zip" || die "adb push of the installer"
adb shell "cd $T && unzip -o -q installer.zip META-INF/com/google/android/update-binary && mv META-INF/com/google/android/update-binary ub" < /dev/null ||
	die "can't unpack the installer on the tablet"

step "Layout"
stage layout "GTS3_SETUP=$SETUP" < /dev/null || die "the layout failed (see above)"

send() {	# <partition> <file> <format> <sha256 of the file>
	local sz
	sz=$(($(stat -c %s "$2") / 1048576))
	step "Sending $(basename "$2") ($sz MiB)"
	if command -v pv > /dev/null; then
		pv "$2" | stage recv "GTS3_PART=$1 GTS3_FORMAT=$3 GTS3_SHA256=$4"
	else
		say "  (a few minutes; install pv for a progress bar)"
		stage recv "GTS3_PART=$1 GTS3_FORMAT=$3 GTS3_SHA256=$4" < "$2"
	fi
}
if [ -n "$ROOT" ]; then
	if [ -f "$ROOT.sha256" ]; then
		SUM=$(cut -d' ' -f1 "$ROOT.sha256")
		say "Checking $(basename "$ROOT")..."
		[ "$(sha256sum "$ROOT" | cut -d' ' -f1)" = "$SUM" ] || die "$(basename "$ROOT") is damaged, download it again"
	else
		SUM=$(sha256sum "$ROOT" | cut -d' ' -f1)
	fi
	send nura "$ROOT" xz "$SUM" || die "sending Nura's root failed: run setup again"
fi
if [ -n "$GSI" ]; then
	say "Checking $(basename "$GSI")..."
	send android_system "$GSI" "$GSIFMT" "$(sha256sum "$GSI" | cut -d' ' -f1)" ||
		die "sending the GSI failed: run setup again"
fi

step "Finishing"
FENV=
if [ -n "$ROOT" ]; then
	push "$A" "$T/answers" || die "adb push of the settings"
	FENV="GTS3_ANSWERS=$T/answers GTS3_UTC=$(date -u +%s)"
fi
rc=0
stage finish $FENV < /dev/null || rc=$?
adb shell "rm -rf $T/answers" < /dev/null > /dev/null 2>&1 || :
[ $rc = 0 ] || die "finishing failed (see above)"

step "Done"
if yes_no "Restart the tablet now"; then
	adb reboot > /dev/null 2>&1 || :
	say "It restarts into $([ -n "$ROOT" ] && echo Nura || echo Android). Volume Down at power-on: lk2nd's menu."
else
	say "Restart it when you like: it starts $([ -n "$ROOT" ] && echo Nura || echo Android)."
fi
