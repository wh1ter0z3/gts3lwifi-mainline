#
# Copyright (C) 2026 The gts3lwifi-mainline project
# SPDX-License-Identifier: Apache-2.0
#

PRODUCT_MAKEFILES := \
    aosp_gts3:$(LOCAL_DIR)/aosp_gts3.mk

# The release name (here trunk_staging) depends on the AOSP tag checked out:
# `lunch` without arguments lists what the tree has.
COMMON_LUNCH_CHOICES := \
    aosp_gts3-trunk_staging-userdebug \
    aosp_gts3-trunk_staging-user
