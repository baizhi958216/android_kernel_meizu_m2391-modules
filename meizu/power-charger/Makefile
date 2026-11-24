# SPDX-License-Identifier: GPL-2.0-only
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(CURDIR)
MO ?= $(OUT_DIR)/$(M)

all: modules

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) modules KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/meizu/power-notify/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-battery-logger/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-manager/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-thermal/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/storage/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-logger/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-interface/Module.symvers"

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) INSTALL_MOD_STRIP=1 modules_install KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/meizu/power-notify/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-battery-logger/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-manager/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-thermal/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/storage/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-logger/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-interface/Module.symvers"

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) clean KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/meizu/power-notify/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-battery-logger/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-manager/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-thermal/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/storage/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-logger/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-interface/Module.symvers"

.PHONY: all modules modules_install clean
