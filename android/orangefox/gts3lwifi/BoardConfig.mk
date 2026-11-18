#
# Copyright (C) 2021 Deokgyu Yang (awesometic)
# Copyright (C) 2026 The gts3lwifi-mainline project
# SPDX-License-Identifier: Apache-2.0
#
# Galaxy Tab S3 (Wi-Fi), SM-T820: see ../common/BoardConfigCommon.mk

include device/samsung/gts3l-recovery/common/BoardConfigCommon.mk

# awesometic's kernel for this model (Linux 3.18.140, his public source:
# github.com/awesometic/android_kernel_samsung_msm8996)
ifeq ($(GTS3_KERNEL),mainline)
include device/samsung/gts3l-recovery/common/BoardConfigMainline.mk
else
TARGET_PREBUILT_KERNEL := device/samsung/gts3l-recovery/gts3lwifi/prebuilt/Image.gz-dtb
endif
