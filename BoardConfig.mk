#
# Copyright (C) 2026 The gts3lwifi-mainline project
# SPDX-License-Identifier: Apache-2.0
#
# Galaxy Tab S3 (SM-T820 gts3lwifi, SM-T825 gts3llte), MSM8996 on the mainline
# kernel. This tree builds ONE thing: a vendor image for any Android 15 to 17
# system image (GSI), started by our gsi-init boot ramdisk from lk2nd. No boot,
# recovery or system image: the kernel and the boot image come from our kernel
# build (gsi/tools/mkgsiboot.sh), the system from the GSI the user downloads.
#

DEVICE_PATH := device/samsung/gts3

# --- Architecture ---
# MSM8996's Kryo is ARMv8.0-A (Pixel 1, marlin, used the same variant). Nothing
# newer: a vendor built for ARMv8.1+ crashes here (Linaro's db845c one did).
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_VARIANT := kryo

# 32-bit vendor libraries too: GSIs with a 32-bit zygote (and 32-bit apps that
# use the GPU) need them; the CPU runs AArch32.
TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv8-a
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi
TARGET_2ND_CPU_VARIANT := kryo

# A neutral name, not msm8996: that one switches on Qualcomm's own HAL trees
# (hardware/qcom/*) in places, and nothing here may come from them.
TARGET_BOARD_PLATFORM := gts3
TARGET_BOOTLOADER_BOARD_NAME := gts3

# --- What is built ---
# lk2nd + our boot image (gsi-init) start Android; only the vendor image is ours.
TARGET_NO_BOOTLOADER := true
TARGET_NO_KERNEL := true
TARGET_NO_RECOVERY := true
BOARD_USES_RECOVERY_AS_BOOT := false

# --- Images ---
# The vendor image is a file on the userdata partition (gsi-init loop-mounts
# it): no partition to fit, so the build sizes it from its content. ext4, so
# tools/mkvendor-standalone.sh can still add the kernel modules and the
# firmware links of one kernel build after it.
BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_VENDORIMAGE_PARTITION_RESERVED_SIZE := 67108864
TARGET_COPY_OUT_VENDOR := vendor
TARGET_USERIMAGES_USE_EXT4 := true

# Android 15 vendor API (target-level 202404), but presented to a GSI as API 34
# (VNDK v34): Android 16 GSIs derive the VNDK apex to mount from
# ro.board.api_level, 202404 maps to v35, and no VNDK apex above v34 exists, so
# linkerconfig aborts and the GSI never boots. AOSP 15 r36's build has the
# override. (From SamuraiArtem's j5xnlte tree, where Android 15-17 GSIs boot on
# the same scheme.)
BOARD_API_LEVEL_PROP_OVERRIDE := 34

# --- VINTF ---
DEVICE_MANIFEST_FILE := $(DEVICE_PATH)/vintf/manifest.xml

# --- SELinux ---
BOARD_VENDOR_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy/vendor

# --- Graphics: upstream Mesa (manifests/gts3.xml puts it at external/mesa3d) ---
# Mesa's own AOSP build (android/Android.mk runs meson with the platform's
# toolchain): freedreno for the Adreno 530, EGL/GLES with the platform's
# IMapper, so it knows every buffer layout (no gralloc shim as with the NDK
# build). Vulkan: add freedreno (turnip) once it supports a5xx.
BOARD_MESA3D_USES_MESON_BUILD := true
BOARD_MESA3D_GALLIUM_DRIVERS := freedreno
BOARD_MESA3D_VULKAN_DRIVERS :=
BOARD_MESA3D_MESON_ARGS := -Dfreedreno-kmds=msm

# --- Wi-Fi: ath10k (QCA6174) through the kernel's nl80211 ---
# No Wi-Fi vendor HAL (as in the GloDroid vendor this replaces): the framework
# then drives wpa_supplicant and hostapd alone, which is all a mainline driver
# needs.
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_HOSTAPD_DRIVER := NL80211
WPA_SUPPLICANT_VERSION := VER_0_8_X
