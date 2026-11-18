#!/sbin/sh
#
# ADDOND_VERSION=3
#
# /system/addon.d/60-gts3-data.sh (gts3-lineage-data.zip, gts3lwifi-mainline
# docs/dual-boot.md): keeps LineageOS's /data on the dual boot layout's
# android_data partition across LineageOS updates. An update reinstalls
# /system: the init script comes back from the backup, and the fstab line is
# changed again.
#

. /tmp/backuptool.functions

list_files() {
	echo etc/init/gts3-data.rc
}

case "$1" in
backup)
	list_files | while read -r FILE DUMMY; do
		backup_file "$S/$FILE"
	done
	;;
restore)
	list_files | while read -r FILE REPLACEMENT; do
		R=""
		[ -n "$REPLACEMENT" ] && R="$S/$REPLACEMENT"
		[ -f "$C/$S/$FILE" ] && restore_file "$S/$FILE" "$R"
	done
	;;
post-restore)
	sed -i 's|^/dev/block/bootdevice/by-name/userdata\([[:space:]][[:space:]]*/data[[:space:]]\)|/dev/block/mapper/gts3_data\1|' \
		"$S/vendor/etc/fstab.qcom"
	;;
esac
