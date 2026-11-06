# SPDX-License-Identifier: GPL-2.0-only
#
# Out-of-tree build of the BLSP ownership test aid.
#
#   make KDIR=<prepared kernel build dir> ARCH=arm64 LLVM=1
#
# KDIR must be a kernel build tree configured and prepared for this tablet
# (make ... modules_prepare). Produces gts3-blsp-owner.ko.

obj-m += gts3-blsp-owner.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
