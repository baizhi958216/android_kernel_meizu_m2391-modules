# SPDX-License-Identifier: GPL-2.0-only
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(CURDIR)
MO ?= $(OUT_DIR)/$(M)

KBUILD_OPTIONS :=
KBUILD_OPTIONS += RMNET_CORE_ROOT=$(abspath $(KERNEL_SRC)/$(M))
KBUILD_OPTIONS += RMNET_CORE_INC_DIR=$(abspath $(KERNEL_SRC)/$(M))
KBUILD_OPTIONS += CONFIG_ARCH_KALAMA=y

all: modules

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) modules $(KBUILD_OPTIONS)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) INSTALL_MOD_STRIP=1 modules_install $(KBUILD_OPTIONS)

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) clean $(KBUILD_OPTIONS)

.PHONY: all modules modules_install clean
