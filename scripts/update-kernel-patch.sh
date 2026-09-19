#!/bin/sh
# Rebuild 0001-add-samsung-gts3lwifi-dts.patch from the DTS and install it into
# the linux-postmarketos-qcom-msm8996 package in pmaports (source list,
# checksum and pkgrel are updated too).
#
# Usage:  ~/samsung/pmos/update-kernel-patch.sh
# Then:   pmbootstrap build --force linux-postmarketos-qcom-msm8996
#         pmbootstrap install --zap     (a same-version rebuild is not
#                                        picked up by an existing rootfs)
#         pmbootstrap flasher flash_kernel
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
DTS=${DTS:-$HOME/samsung/mainline/apq8096-samsung-gts3lwifi.dts}
PMAPORTS=${PMAPORTS:-$HOME/.local/var/pmbootstrap/cache_git/pmaports}
KDIR=$PMAPORTS/device/testing/linux-postmarketos-qcom-msm8996
BASE_PKGVER=6.19.5
BASE_MAKEFILE=$HERE/base/qcom-Makefile.v$BASE_PKGVER-msm8996
UPSTREAM_DTS=${UPSTREAM_DTS:-$HOME/mocha/u-boot/dts/upstream}
PATCH=0001-add-samsung-gts3lwifi-dts.patch
NAME=psigod
EMAIL=psigod@example.com

die() { echo "error: $*" >&2; exit 1; }

[ -f "$DTS" ] || die "DTS not found: $DTS"
[ -f "$KDIR/APKBUILD" ] || die "kernel package not found: $KDIR"
[ -f "$BASE_MAKEFILE" ] || die "base Makefile not found: $BASE_MAKEFILE"
grep -qx "pkgver=$BASE_PKGVER" "$KDIR/APKBUILD" ||
	die "kernel pkgver is no longer $BASE_PKGVER; refresh the base Makefile for the new tag"

# 1. Syntax check against a mainline dts tree, when one is available
if [ -d "$UPSTREAM_DTS/src/arm64/qcom" ] && [ "${SKIP_DTC:-0}" != 1 ]; then
	cpp -nostdinc -I "$UPSTREAM_DTS/include" -I "$UPSTREAM_DTS/src/arm64/qcom" \
		-undef -D__DTS__ -x assembler-with-cpp "$DTS" |
		dtc -q -I dts -O dtb -i "$UPSTREAM_DTS/src/arm64/qcom" -o /dev/null - ||
		die "DTS does not compile (set SKIP_DTC=1 to bypass)"
	echo "dtc check: OK"
fi

# 2. Build the patch in a throwaway git repo. Dates come from the DTS mtime,
#    so an unchanged DTS always produces a byte-identical patch.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
stamp="@$(stat -c %Y "$DTS") +0000"
export GIT_AUTHOR_NAME="$NAME" GIT_AUTHOR_EMAIL="$EMAIL" GIT_AUTHOR_DATE="$stamp"
export GIT_COMMITTER_NAME="$NAME" GIT_COMMITTER_EMAIL="$EMAIL" GIT_COMMITTER_DATE="$stamp"
git init -q "$work/linux"
cd "$work/linux"
mkdir -p arch/arm64/boot/dts/qcom
cp "$BASE_MAKEFILE" arch/arm64/boot/dts/qcom/Makefile
git add -A
git -c commit.gpgsign=false commit -q -m "base: v$BASE_PKGVER-msm8996"

cp "$DTS" arch/arm64/boot/dts/qcom/apq8096-samsung-gts3lwifi.dts
tab=$(printf '\t')
sed -i "/^dtb-\$(CONFIG_ARCH_QCOM)${tab}+= apq8096-ifc6640\.dtb\$/a dtb-\$(CONFIG_ARCH_QCOM)${tab}+= apq8096-samsung-gts3lwifi.dtb" \
	arch/arm64/boot/dts/qcom/Makefile
grep -q "apq8096-samsung-gts3lwifi.dtb" arch/arm64/boot/dts/qcom/Makefile ||
	die "Makefile anchor line not found"
git add -A
git -c commit.gpgsign=false commit -q -F - <<MSG
arm64: dts: qcom: Add Samsung Galaxy Tab S3 Wi-Fi (gts3lwifi)

Minimal bring-up device tree for the Samsung Galaxy Tab S3 Wi-Fi
(SM-T820), an APQ8096 tablet with PM8994 + PM8004 and eMMC storage:
reserved memory matching the Samsung firmware layout, PM8994 RPM
regulators, eMMC on SDHC1 and dwc3 USB in peripheral mode. Booted via
lk2nd.

Signed-off-by: $NAME <$EMAIL>
Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
git format-patch -1 --stdout --no-signature > "$work/$PATCH"
git checkout -q HEAD~1
git apply --check "$work/$PATCH" || die "patch does not apply to the v$BASE_PKGVER base"

# 3. Install into pmaports and refresh the APKBUILD
install -m 644 "$work/$PATCH" "$KDIR/$PATCH"
sum=$(sha512sum "$KDIR/$PATCH" | cut -d' ' -f1)
python3 - "$KDIR/APKBUILD" "$PATCH" "$sum" <<'PY'
import re, sys
path, patch, digest = sys.argv[1:]
s = open(path).read()

def block(name):
    m = re.search(rf'^{name}="\n(.*?)^"', s, re.S | re.M)
    if not m:
        sys.exit(f"{name} block not found in {path}")
    return m

m = block("source")
if patch not in m.group(1):
    s = s[:m.end(1)] + f"\t{patch}\n" + s[m.end(1):]

m = block("sha512sums")
lines = [l for l in m.group(1).splitlines() if l and not l.endswith("  " + patch)]
lines.append(f"{digest}  {patch}")
s = s[:m.start(1)] + "\n".join(lines) + "\n" + s[m.end(1):]

# Must stay newer than the binary repository's 6.19.5-r0, otherwise
# pmbootstrap installs the unpatched binary kernel instead of this build.
s = re.sub(r"^pkgrel=0$", "pkgrel=1", s, flags=re.M)
open(path, "w").write(s)
PY

echo "patch installed: $KDIR/$PATCH"
grep -E '^(pkgver|pkgrel)=' "$KDIR/APKBUILD" | tr '\n' ' '; echo
echo "next: pmbootstrap build --force linux-postmarketos-qcom-msm8996"
echo "      pmbootstrap install --zap"
echo "      pmbootstrap flasher flash_kernel"
