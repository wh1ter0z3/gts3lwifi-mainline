#
# Copyright (C) 2026 The gts3lwifi-mainline project
# SPDX-License-Identifier: Apache-2.0
#

PRODUCT_MAKEFILES := \
    aosp_gts3:$(LOCAL_DIR)/aosp_gts3.mk

# The release name depends on the AOSP tag: bp1a is Android 15 QPR2 (r36);
# `lunch` without arguments lists what the tree has.
COMMON_LUNCH_CHOICES := \
    aosp_gts3-bp1a-userdebug \
    aosp_gts3-bp1a-user
