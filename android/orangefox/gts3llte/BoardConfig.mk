#
# Copyright (C) 2021 Deokgyu Yang (awesometic)
# Copyright (C) 2026 The gts3lwifi-mainline project
# SPDX-License-Identifier: Apache-2.0
#
# Galaxy Tab S3 (LTE), SM-T825: see ../common/BoardConfigCommon.mk

include device/samsung/gts3l-recovery/common/BoardConfigCommon.mk

# awesometic's kernel for this model (Linux 3.18.140, his public source:
# github.com/awesometic/android_kernel_samsung_msm8996)
TARGET_PREBUILT_KERNEL := device/samsung/gts3l-recovery/gts3llte/prebuilt/Image.gz-dtb
