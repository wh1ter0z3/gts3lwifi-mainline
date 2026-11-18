# OrangeFox on our mainline kernel (gts3lwifi-mainline), for the dual boot:
# lk2nd boots it from the layout's boot partition with the device tree that
# carries ro.boot.* (see init.recovery.gts3ml.rc). Built with
#   GTS3_KERNEL=mainline OUT_DIR=out-mainline
#   GTS3_MAINLINE_KERNEL=<Image.gz of the Android kernel build>
#   GTS3_MAINLINE_MODULES="<the .ko files the recovery needs>"
# The S-Boot 12 MiB ramdisk limit doesn't apply: lk2nd loads it.

TARGET_PREBUILT_KERNEL := $(GTS3_MAINLINE_KERNEL)
BOARD_KERNEL_IMAGE_NAME := Image.gz
# no androidboot.* here, and SELinux as TWRP's policy has it
BOARD_KERNEL_CMDLINE := console=tty0 clk_ignore_unused pd_ignore_unused \
    msm.separate_gpu_kms=1 printk.devkmsg=on
# display, S-Pen, touch keys, USB-C and the MUIC (USB data), the USB gadget,
# exFAT: loaded by the first stage init from /lib/modules
BOARD_RECOVERY_KERNEL_MODULES := $(GTS3_MAINLINE_MODULES)
# vkms (a virtual display, for a board without panel) only with gts3.vkms=1
BOARD_RECOVERY_KERNEL_MODULES_LOAD := $(filter-out %/vkms.ko,$(GTS3_MAINLINE_MODULES))
TW_BRIGHTNESS_PATH := "/sys/class/backlight/994000.dsi.0/brightness"
