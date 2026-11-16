#
# Copyright (C) 2026 The gts3lwifi-mainline project
# SPDX-License-Identifier: Apache-2.0
#

$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/base.mk)
$(call inherit-product, vendor/twrp/config/common.mk)

PRODUCT_COPY_FILES += \
    system/timezone/output_data/iana/tzdata:recovery/root/system/usr/share/zoneinfo/tzdata

PRODUCT_DEVICE := gts3llte
PRODUCT_NAME := twrp_gts3llte
PRODUCT_BRAND := samsung
PRODUCT_MODEL := SM-T825
PRODUCT_MANUFACTURER := samsung
PRODUCT_RELEASE_NAME := Galaxy Tab S3 (LTE)
