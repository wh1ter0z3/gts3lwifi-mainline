# Copyright (C) 2026 The gts3lwifi-mainline project
# SPDX-License-Identifier: Apache-2.0
#
# libcamera (upstream, plus camera/libcamera-patches: our sensor helpers,
# autofocus and tuning, as in Nura) with its Android camera HAL3 module,
# built by meson through GloDroid's aospext (vendor/camera/aospext). After
# aospext's meson_libcamera.mk, without udev and libevent (no cam app): the
# simple pipeline and its software ISP (CPU), the softisp IPA.

LOCAL_PATH := vendor/camera/aospext
include $(LOCAL_PATH)/aospext_cleanup.mk

AOSPEXT_PROJECT_NAME := LIBCAMERA
AOSPEXT_BUILD_SYSTEM := meson

LOCAL_SHARED_LIBRARIES := libc libdl libexif libjpeg libyuv libcrypto libhardware libcutils liblog
AOSPEXT_GEN_PKGCONFIGS := libexif libjpeg dl libyuv libcrypto libhardware

MESON_BUILD_ARGUMENTS := \
    -Dwerror=false \
    -Dandroid=enabled \
    -Dandroid_platform=generic \
    -Dpipelines=simple \
    -Dipas=softisp \
    -Dsoftisp-gpu=disabled \
    -Dudev=disabled \
    -Dv4l2=disabled \
    -Dcam=disabled \
    -Dqcam=disabled \
    -Dgstreamer=disabled \
    -Dpycamera=disabled \
    -Dlc-compliance=disabled \
    -Dlibdw=disabled \
    -Dlibunwind=disabled \
    -Dtracing=disabled \
    -Ddocumentation=disabled \
    -Dtest=false \
    -Dsysconfdir=/vendor/etc

# TYPE:PATH_IN_INSTALL:VENDOR_SUBDIR:MODULE:SYMLINK_SUFFIX
AOSPEXT_GEN_TARGETS := \
    lib:libcamera.so::libcamera: \
    lib:libcamera-base.so::libcamera-base: \
    lib:libcamera-hal.so:hw:camera.libcamera: \
    libetc:libcamera/ipa/ipa_softisp.so:libcamera/ipa:ipa_softisp.so: \
    libetc:libcamera/ipa/ipa_softisp.so.sign:libcamera/ipa:ipa_softisp.so.sign: \
    bin:libcamera/softisp_ipa_proxy:libcamera:softisp_ipa_proxy: \
    etc:shared/libcamera/ipa/softisp/imx258.yaml:shared/libcamera/ipa/softisp:imx258.yaml: \
    etc:shared/libcamera/ipa/softisp/s5k5e3.yaml:shared/libcamera/ipa/softisp:s5k5e3.yaml: \
    etc:shared/libcamera/ipa/softisp/uncalibrated.yaml:shared/libcamera/ipa/softisp:uncalibrated.yaml:

AOSPEXT_TARGETS_SO_DEPS := \
    camera.libcamera=libcamera:libcamera-base \
    libcamera=libcamera-base

AOSPEXT_EXPORT_INSTALLED_INCLUDE_DIRS := vendor/include/libcamera

LOCAL_MULTILIB := first
include $(LOCAL_PATH)/aospext_cross_compile.mk
include $(LOCAL_PATH)/aospext_gen_targets.mk
