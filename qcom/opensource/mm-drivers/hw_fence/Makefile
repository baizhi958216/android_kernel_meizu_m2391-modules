# SPDX-License-Identifier: GPL-2.0-only
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(CURDIR)
MO ?= $(OUT_DIR)/$(M)

KBUILD_OPTIONS :=
KBUILD_OPTIONS += MSM_HW_FENCE_ROOT=$(abspath $(KERNEL_SRC)/$(M)/..)/

all: modules

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) modules $(KBUILD_OPTIONS)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) INSTALL_MOD_STRIP=1 modules_install $(KBUILD_OPTIONS)

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) clean $(KBUILD_OPTIONS)

.PHONY: all modules modules_install clean
