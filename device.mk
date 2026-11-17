#
# Copyright (C) 2026 The gts3lwifi-mainline project
# SPDX-License-Identifier: Apache-2.0
#
# The vendor image of the Galaxy Tab S3 on the mainline kernel. Rules:
#  - open source only: nothing from Samsung or Qualcomm is built in. The
#    tablet's own firmware is read at boot from its own partitions (gsi-init
#    mounts them under /vendor/firmware/.part, tools/mkvendor-standalone.sh
#    adds the links and the kernel's modules after this build);
#  - for any unmodified system image (GSI): nothing here may rely on a change
#    to the system side. Patches (patches/) touch vendor-side sources only;
#  - each choice that a GSI forced is explained where it is made.
# Many of those lessons come from SamuraiArtem's Galaxy J5 2016 tree (mainline
# MSM8916, LineageOS 22, Apache-2.0), see NOTICE.
#

DEVICE_PATH := device/samsung/gts3

PRODUCT_SOONG_NAMESPACES += \
    $(DEVICE_PATH) \
    external/v4l2_codec2 \
    device/linaro/dragonboard

# --- Treble level ---
# Shipping API 33 and a VNDK version of 34, with an Android 15 build: what lets
# Android 15 to 17 GSIs resolve a VNDK apex they actually ship (v34 is the last
# one ever published; see BOARD_API_LEVEL_PROP_OVERRIDE in BoardConfig.mk).
# phh/TrebleDroid-based GSIs read ro.vndk.version through vndk-detect.
PRODUCT_SHIPPING_API_LEVEL := 33
PRODUCT_VENDOR_PROPERTIES += \
    ro.vndk.version=34

# The device manifest's target-level (202404) only counts with this; without it
# keystore2 finds no KeyMint (VINTF can't be assembled) and boot hangs.
PRODUCT_ENFORCE_VINTF_MANIFEST_OVERRIDE := true
PRODUCT_OTA_ENFORCE_VINTF_KERNEL_REQUIREMENTS := false
PRODUCT_USE_DYNAMIC_PARTITIONS := false

# A vendor library a GSI's linker namespace must let through: our vendor links
# libapexsupport, and the VNDK v34 lists (what ro.vndk.version=34 selects)
# don't name it. Declared, not shipped: a second copy makes linkerconfig abort.
PRODUCT_VENDOR_LINKER_CONFIG_FRAGMENTS += \
    $(DEVICE_PATH)/linker.config.json

# 4 GB of RAM
$(call inherit-product, frameworks/native/build/phone-xhdpi-4096-dalvik-heap.mk)

# --- Board files ---
PRODUCT_PACKAGES += \
    fstab.gts3 \
    init.gts3.rc \
    init.gts3.usb.rc \
    ueventd.gts3.rc \
    gts3-audio-init

# 2048x1536 on 9.7 in. Larger timeouts: four Kryo cores and eMMC take longer
# than the framework's defaults for a big GSI's first boot (its package scan
# tripped the watchdog), and the vendor value is the one every GSI reads.
PRODUCT_VENDOR_PROPERTIES += \
    ro.sf.lcd_density=320 \
    ro.hw_timeout_multiplier=4

# --- Graphics ---
# drm_hwcomposer (AIDL composer3) on the MDP5 display controller, minigbm
# (msm backend: Adreno tiling and alignment) for buffers, Mesa freedreno for
# EGL/GLES. gsi-init points the composer at the display controller's DRM card
# (vendor.hwc.drm.device) and sets the panel's orientation.
PRODUCT_PACKAGES += \
    android.hardware.composer.hwc3-service.drm \
    android.hardware.graphics.allocator-service.minigbm_msm \
    mapper.minigbm \
    gralloc.minigbm_msm \
    libEGL_mesa \
    libGLESv1_CM_mesa \
    libGLESv2_mesa \
    libgallium_dri

# GLES 3.1: what freedreno exposes on a5xx (the GloDroid vendor said 2.0).
PRODUCT_VENDOR_PROPERTIES += \
    ro.hardware.egl=mesa \
    ro.hardware.gralloc=minigbm_msm \
    ro.opengles.version=196609 \
    vendor.minigbm.debug=nocompression

# --- Video: Venus through V4L2 (AOSP external/v4l2_codec2) ---
# Our AIDL service around the V4L2 component store (codec2/, from the J5 tree:
# the project itself only ships a HIDL service). Needs the kernel's Venus
# patches 0194-0206 (gsi/kernel).
PRODUCT_PACKAGES += \
    android.hardware.media.c2-service-v4l2

PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/media/media_codecs.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs.xml \
    $(DEVICE_PATH)/media/media_codecs_v4l2_c2.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_v4l2_c2.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_c2_audio.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_c2_audio.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_c2_video.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_c2_video.xml \
    $(DEVICE_PATH)/seccomp/android.hardware.media.c2-extended-seccomp_policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/android.hardware.media.c2-extended-seccomp_policy \
    $(DEVICE_PATH)/seccomp/mediaextractor.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/mediaextractor.policy \
    $(DEVICE_PATH)/seccomp/mediaswcodec.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/mediaswcodec.policy

# The framework on AIDL Codec2 (HIDL needs hwservicemanager, which an
# Android 15 vendor doesn't declare). Pool mask, by allocator ID (AOSP 15+;
# the v4l2_codec2 README's numbers are from before IGBA): GRALLOC (17),
# BUFFERQUEUE (18), BLOB (19), SECURE_LINEAR (21), SECURE_GRAPHIC (22).
# GRALLOC: without it a decoder into byte buffers gets the basic pool, and
# the V4L2 plugin needs a bufferpool there (dectest, 2026-11-17).
# Four instances each way, as listed in media_codecs_v4l2_c2.xml.
# c2inputsurface -1: the framework makes encoder input surfaces (screen
# recording, camera video) itself. Otherwise it asks the vendor's OMX service,
# which an AIDL-only vendor doesn't have, and crashes (screenrecord, 2026-11-17).
PRODUCT_VENDOR_PROPERTIES += \
    media.c2.hal.selection=aidl \
    debug.stagefright.c2-poolmask=0x6e0000 \
    debug.stagefright.c2inputsurface=-1 \
    ro.vendor.v4l2_codec2.decode_concurrent_instances=4 \
    ro.vendor.v4l2_codec2.encode_concurrent_instances=4

# --- Audio: AIDL default HAL on the q6 card "S3" (WCD9335, RT5508 amps) ---
# The primary module opens the PCM named in each device port's address
# (CARD_<card>_DEV_<pcm>, primary_audio_policy_configuration.xml);
# bin/gts3-audio-init.sh sets the mixer routes at boot (from postmarketOS's
# UCM profile). Speakers and built-in mics for now; headphones (PCM 1) and
# the headset mic (PCM 3) become device ports of their own with jack events.
# The HAL, its init script and its VINTF declaration (audio core v3) come in
# AOSP's vendor APEX; only the policy files below are ours.
PRODUCT_PACKAGES += \
    com.android.hardware.audio \
    tinymix.vendor \
    tinyplay.vendor \
    tinycap.vendor \
    tinypcminfo.vendor

PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/audio/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    $(DEVICE_PATH)/audio/primary_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/primary_audio_policy_configuration.xml \
    $(DEVICE_PATH)/audio/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    $(DEVICE_PATH)/audio/bluetooth_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_audio_policy_configuration.xml \
    $(DEVICE_PATH)/audio/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    $(DEVICE_PATH)/audio/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    $(DEVICE_PATH)/audio/surround_sound_configuration_5_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/surround_sound_configuration_5_0.xml

# Speakers on PCM 0 (MultiMedia1 -> both speaker pairs), built-in mics on
# PCM 2 (MultiMedia3 <- main and sub mic): the device ports' addresses.
# 40 ms periods: shorter ones underran before the ADSP took its first buffer.
PRODUCT_VENDOR_PROPERTIES += \
    persist.vendor.audio.primary.latency_ms=40 \
    ro.audio.alsa.silence_on_underrun=true

# --- Wi-Fi: wpa_supplicant and hostapd on nl80211 (ath10k) ---
PRODUCT_PACKAGES += \
    wpa_supplicant \
    hostapd \
    libwpa_client

PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/wifi/wpa_supplicant.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant.conf \
    $(DEVICE_PATH)/wifi/android.hardware.wifi.supplicant-service.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/android.hardware.wifi.supplicant-service.rc \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.direct.xml

PRODUCT_VENDOR_PROPERTIES += \
    wifi.interface=wlan0

# --- Bluetooth: QCA6174 on hci_uart, AIDL HAL over the HCI user channel ---
# gsi-init sets the controller's address from efs. The firmware claims
# Android's vendor HCI capabilities but doesn't answer them (LE_ADV_FILTER
# timed out and the stack aborted): off.
PRODUCT_PACKAGES += \
    android.hardware.bluetooth-service.default

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml

PRODUCT_VENDOR_PROPERTIES += \
    bluetooth.core.le.vendor_capabilities.enabled=false

# --- Health, lights: ours (hal/), from /sys/class/power_supply and backlight ---
PRODUCT_PACKAGES += \
    android.hardware.health-service.gts3 \
    android.hardware.lights-service.gts3

# --- Samsung system images (OneUI): stand-ins for the Samsung HALs their
# system_server waits for forever (hal/samsung/). A plain GSI never asks.
PRODUCT_PACKAGES += \
    vendor.samsung.hardware.seh-stubs

# --- Security: software KeyMint and Gatekeeper (no trusted OS used yet) ---
PRODUCT_PACKAGES += \
    android.hardware.security.keymint-service.nonsecure \
    com.android.hardware.gatekeeper.nonsecure

# KeyMint wants a valid patch level, or keystore2 gets
# HARDWARE_TYPE_UNAVAILABLE at earlyBootEnded.
VENDOR_SECURITY_PATCH := 2025-03-05
PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.boot_security_patch=2025-03-05

# --- DRM: ClearKey (Widevine is not ours to ship) ---
PRODUCT_PACKAGES += \
    com.android.hardware.drm.clearkey

# --- USB ---
# Port HAL: the AOSP one (the Tab S3 has a USB-C port class). No gadget HAL:
# the framework's legacy path drives our configfs gadget (init.gts3.usb.rc).
PRODUCT_PACKAGES += \
    android.hardware.usb-service.example

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.usb.host.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.accessory.xml

# --- Power ---
PRODUCT_PACKAGES += \
    android.hardware.power-service.example

# No HIDL CAS service (it can't register here and crash-loops): dead-hidl/
PRODUCT_PACKAGES += \
    gts3-dead-hidl-overrides

# --- Features of the hardware ---
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/tablet_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/tablet_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
    frameworks/native/data/etc/android.software.midi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.midi.xml

# --- Sensors: the SLPI's accelerometer, gyroscope, magnetometer and light
# sensor (IIO devices of the kernel's qcom_smgr) through AOSP's AIDL
# multihal and our IIO sub-HAL (hal/sensors/) ---
PRODUCT_PACKAGES += \
    android.hardware.sensors-service.multihal \
    android.hardware.sensors@2.1-gts3-iio-subhal
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/hal/sensors/hals.conf:$(TARGET_COPY_OUT_VENDOR)/etc/sensors/hals.conf \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.light.xml

# --- GNSS: the engine in the modem DSP, through its QMI LOC service
# (hal/gnss/); rmtfs and tqftpserv (init.gts3.rc) run the modem: Linaro's,
# in device/linaro/dragonboard's namespace (above), which is in AOSP ---
PRODUCT_PACKAGES += \
    android.hardware.gnss-service.gts3 \
    rmtfs \
    tqftpserv
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.location.gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.gps.xml

# Still to come (each its own step, tested on the tablet):
#  - sensors: the hall sensor (cover), once the kernel has it;
#  - audio: headphone/headset routing on jack events; Bluetooth audio;
#  - camera (libcamera), Vulkan (turnip on a5xx), fingerprint;
#  - kernel modules and firmware links in this build, not after it.
