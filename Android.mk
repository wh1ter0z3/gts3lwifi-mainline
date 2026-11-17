# Copyright (C) 2026 The gts3lwifi-mainline project
# SPDX-License-Identifier: Apache-2.0
#
# What Soong can't build: libcamera (meson, through aospext). Make doesn't
# look for Android.mk files below a folder that has one, so the others in
# this tree are included here.

ifeq ($(TARGET_DEVICE),gts3)
include device/samsung/gts3/camera/libcamera.mk
include device/samsung/gts3/dead-hidl/Android.mk
endif
