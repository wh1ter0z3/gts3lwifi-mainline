#
# Copyright (C) 2026 The gts3lwifi-mainline project
# SPDX-License-Identifier: Apache-2.0
#
# android.hardware.cas@1.2-service is a HIDL service: an Android 15-level
# vendor declares no HIDL, hwservicemanager stays off, and the service aborts
# at every start ("Error while registering cas service"); init respawns it
# forever. There is no CAS hardware anyway. base_vendor.mk adds it through
# PRODUCT_PACKAGES_SHIPPING_API_LEVEL_33 (our shipping level), which a device
# can't filter out; a module that overrides it keeps it out of the image.
# (From SamuraiArtem's j5xnlte tree, dead-hidl/.)
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := gts3-dead-hidl-overrides
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := dead-hidl-overrides.txt
LOCAL_MODULE_RELATIVE_PATH := gts3
LOCAL_PROPRIETARY_MODULE := true
LOCAL_OVERRIDES_MODULES := android.hardware.cas@1.2-service
include $(BUILD_PREBUILT)
