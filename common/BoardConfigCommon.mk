#
# Copyright (C) 2021 Deokgyu Yang (awesometic), TWRP device trees for gts3lwifi/gts3llte
# Copyright (C) 2026 The gts3lwifi-mainline project
# SPDX-License-Identifier: Apache-2.0
#
# OrangeFox (fox_12.1) for the Galaxy Tab S3, SM-T820 and SM-T825, common to
# both. From awesometic's TWRP trees (android-9.0), with:
#  - the Treble layout's vendor partition and flashing by image (docs/ of the
#    vendor tree, partitions.md); still works on the stock layout;
#  - no decryption: his Samsung/Qualcomm libraries and qseecomd stay out
#    (our /data isn't encrypted), so the tree is open source only;
#  - nothing that rewrites the boot partition: lk2nd lives at its start, and a
#    patched boot image (Magisk, dm-verity removal) would destroy it.
#

COMMON_PATH := device/samsung/gts3l-recovery/common

# Minimal manifest (twrp-12.1 based)
ALLOW_MISSING_DEPENDENCIES := true

# --- Architecture ---
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_ABI2 :=
TARGET_CPU_VARIANT := generic
TARGET_CPU_VARIANT_RUNTIME := kryo

TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv8-a
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi
TARGET_2ND_CPU_VARIANT := generic
TARGET_2ND_CPU_VARIANT_RUNTIME := kryo

TARGET_SUPPORTS_64_BIT_APPS := true

# --- Platform ---
TARGET_BOARD_PLATFORM := msm8996
TARGET_BOOTLOADER_BOARD_NAME := msm8996
TARGET_NO_BOOTLOADER := true
TARGET_NO_RECOVERY := false
BOARD_USES_RECOVERY_AS_BOOT := false
BOARD_BUILD_SYSTEM_ROOT_IMAGE := false

# --- Kernel: his downstream 3.18, prebuilt per model (each device's
# BoardConfig.mk sets TARGET_PREBUILT_KERNEL). S-Boot starts the recovery
# partition itself, so it's a classic Samsung image: header v0, DTB appended.
BOARD_KERNEL_BASE := 0x80000000
BOARD_KERNEL_CMDLINE := androidboot.hardware=qcom msm_rtb.filter=0x237 ehci-hcd.park=3 \
    androidboot.bootdevice=7464900.sdhci lpm_levels.sleep_disabled=1 \
    rcupdate.rcu_expedited=1 cma=32M@0-0xffffffff androidboot.selinux=permissive
BOARD_KERNEL_IMAGE_NAME := Image.gz-dtb
BOARD_KERNEL_PAGESIZE := 4096
BOARD_RAMDISK_OFFSET := 0x02200000
BOARD_KERNEL_TAGS_OFFSET := 0x02000000
BOARD_MKBOOTIMG_ARGS += --ramdisk_offset $(BOARD_RAMDISK_OFFSET)
BOARD_MKBOOTIMG_ARGS += --tags_offset $(BOARD_KERNEL_TAGS_OFFSET)

# --- Partitions ---
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 79691776
BOARD_FLASH_BLOCK_SIZE := 131072
BOARD_HAS_LARGE_FILESYSTEM := true
BOARD_SUPPRESS_SECURE_ERASE := true
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true

# --- Recovery ---
TARGET_RECOVERY_FSTAB := $(COMMON_PATH)/recovery.fstab
TARGET_RECOVERY_DEVICE_DIRS += $(COMMON_PATH)
TARGET_RECOVERY_PIXEL_FORMAT := BGRA_8888
TARGET_RECOVERY_QCOM_RTC_FIX := true
BOARD_HAS_NO_SELECT_BUTTON := true
RECOVERY_SDCARD_ON_DATA := true

# --- TWRP ---
TW_THEME := portrait_hdpi
# The panel is landscape in a portrait tablet
TW_ROTATION := 270
TW_BRIGHTNESS_PATH := "/sys/class/leds/lcd-backlight/brightness"
TW_MAX_BRIGHTNESS := 255
TW_DEFAULT_BRIGHTNESS := 127
TW_SCREEN_BLANK_ON_BOOT := true
TW_INPUT_BLACKLIST := "hbtp_vm"
TW_CUSTOM_CPU_TEMP_PATH := "/sys/devices/virtual/thermal/thermal_zone1/temp"
TW_EXTRA_LANGUAGES := true
TW_INCLUDE_NTFS_3G := true
TW_USE_TOOLBOX := true
TW_EXCLUDE_DEFAULT_USB_INIT := true
TW_MTP_DEVICE := "/dev/mtp_usb"
TW_EXCLUDE_TWRPAPP := true
TW_EXCLUDE_APEX := true
# Screenshots from adb (fb2png): the only way to see it on a board without
# a panel
TW_INCLUDE_FB2PNG := true
# Samsung's bootloader: "Download" is S-Boot's Odin mode; "Bootloader" would
# land there too (lk2nd's own menu is Volume Down at power-on), so not offered
TW_HAS_DOWNLOAD_MODE := true
TW_NO_REBOOT_BOOTLOADER := true
# No decryption (see the top of this file)
TW_INCLUDE_CRYPTO := false

# --- OrangeFox ---
OF_MAINTAINER := psigod
FOX_VANILLA_BUILD := 1
# 3:4 screen: 12 x 120 (OrangeFox lays out at a 9-unit width)
OF_SCREEN_H := 1440
OF_USE_GREEN_LED := 0
OF_FLASHLIGHT_ENABLE := 0
OF_SKIP_FBE_DECRYPTION := 1
OF_NO_TREBLE_COMPATIBILITY_CHECK := 1
# Never patch boot images: lk2nd is at the start of the boot partition
OF_SKIP_ORANGEFOX_PROCESS := 1
FOX_DELETE_MAGISK_ADDON := 1
FOX_RECOVERY_SYSTEM_PARTITION := /dev/block/bootdevice/by-name/system
FOX_RECOVERY_VENDOR_PARTITION := /dev/block/bootdevice/by-name/vendor
# The "SEANDROIDENFORCE" tail S-Boot expects on a recovery image
FOX_USE_SAMSUNG_SPECIAL := 1
FOX_USE_BASH_SHELL := 1
FOX_USE_NANO_EDITOR := 1
FOX_USE_TAR_BINARY := 1
FOX_USE_SED_BINARY := 1
FOX_USE_XZ_UTILS := 1
