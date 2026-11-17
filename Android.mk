# Copyright (C) 2026 The gts3lwifi-mainline project
# SPDX-License-Identifier: Apache-2.0
#
# What Soong can't build: libcamera (meson, through aospext).

ifeq ($(TARGET_DEVICE),gts3)
include device/samsung/gts3/camera/libcamera.mk
endif
