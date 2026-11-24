# SPDX-License-Identifier: GPL-2.0-only
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(CURDIR)
MO ?= $(OUT_DIR)/$(M)

KBUILD_OPTIONS :=
KBUILD_OPTIONS += TOUCH_ROOT=$(abspath $(KERNEL_SRC)/$(M))
KBUILD_OPTIONS += CONFIG_ARCH_KALAMA=y
KBUILD_OPTIONS += CONFIG_MSM_TOUCH=m
KBUILD_OPTIONS += CONFIG_QTS_ENABLE=y
KBUILD_OPTIONS += CONFIG_TOUCHSCREEN_GOODIX_BRL=y
KBUILD_OPTIONS += CONFIG_TOUCHSCREEN_GOODIX_BRL_MEIZU=y
KBUILD_OPTIONS += CONFIG_TOUCHSCREEN_NT36XXX_I2C=n
KBUILD_OPTIONS += CONFIG_TOUCHSCREEN_ATMEL_MXT=n
KBUILD_OPTIONS += CONFIG_TOUCHSCREEN_DUMMY=n

all: modules

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) modules $(KBUILD_OPTIONS) KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/meizu/system/Module.symvers $(OUT_DIR)/../m2391-modules/qcom/opensource/touch-drivers/mz_gesture/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-notify/Module.symvers"

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) INSTALL_MOD_STRIP=1 modules_install $(KBUILD_OPTIONS) KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/meizu/system/Module.symvers $(OUT_DIR)/../m2391-modules/qcom/opensource/touch-drivers/mz_gesture/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-notify/Module.symvers"

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) clean $(KBUILD_OPTIONS) KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/meizu/system/Module.symvers $(OUT_DIR)/../m2391-modules/qcom/opensource/touch-drivers/mz_gesture/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-notify/Module.symvers"

.PHONY: all modules modules_install clean
