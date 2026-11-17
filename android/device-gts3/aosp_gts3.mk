#
# Copyright (C) 2026 The gts3lwifi-mainline project
# SPDX-License-Identifier: Apache-2.0
#

# A Wi-Fi tablet's base; the system part of it is never used (the GSI brings
# its own), only what lands in the vendor image matters.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)

$(call inherit-product, device/samsung/gts3/device.mk)

PRODUCT_NAME := aosp_gts3
PRODUCT_DEVICE := gts3
PRODUCT_BRAND := samsung
PRODUCT_MANUFACTURER := samsung
PRODUCT_MODEL := Galaxy Tab S3
PRODUCT_CHARACTERISTICS := tablet
