#
# Copyright (C) 2026 The gts3lwifi-mainline project
# SPDX-License-Identifier: Apache-2.0
#
# OrangeFox settings its packing script (vendor/recovery/OrangeFox_*.sh) reads
# from the environment, not from BoardConfig: exported here, for our two
# models only (build/envsetup.sh sources every device's vendorsetup.sh).
# Export FOX_BUILD_DEVICE before sourcing build/envsetup.sh.

case "$FOX_BUILD_DEVICE" in
gts3lwifi|gts3llte)
	# Never patch or flash boot images: lk2nd is at the start of the boot
	# partition, a Magisk-patched or dm-verity-patched image would destroy it
	export FOX_DELETE_MAGISK_ADDON=1
	export OF_SKIP_ORANGEFOX_PROCESS=1
	# Samsung: S-Boot wants "SEANDROIDENFORCE" at the end of the image
	export FOX_SAMSUNG_DEVICE=1
	export FOX_USE_SAMSUNG_SPECIAL=1
	export FOX_VANILLA_BUILD=1
	# LZMA ramdisk, and no bash or nano: S-Boot's 12 MiB ramdisk limit
	# (see BoardConfigCommon.mk)
	export OF_USE_LZMA_COMPRESSION=1
	export FOX_REMOVE_BASH=1
	export FOX_EXCLUDE_NANO_EDITOR=1
	# Extras
	export FOX_USE_TAR_BINARY=1
	export FOX_USE_SED_BINARY=1
	export FOX_USE_XZ_UTILS=1
	export OF_USE_GREEN_LED=0
	export OF_MAINTAINER=psigod
	;;
esac
