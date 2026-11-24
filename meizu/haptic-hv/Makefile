# SPDX-License-Identifier: GPL-2.0-only
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(CURDIR)
MO ?= $(OUT_DIR)/$(M)

all: modules

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) modules KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/meizu/boot-mode/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/system/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/storage/Module.symvers"

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) INSTALL_MOD_STRIP=1 modules_install KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/meizu/boot-mode/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/system/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/storage/Module.symvers"

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) clean KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/meizu/boot-mode/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/system/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/storage/Module.symvers"

.PHONY: all modules modules_install clean
