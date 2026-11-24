# SPDX-License-Identifier: GPL-2.0-only
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(CURDIR)
MO ?= $(OUT_DIR)/$(M)

KBUILD_OPTIONS :=
KBUILD_OPTIONS += AUDIO_ROOT=$(abspath $(KERNEL_SRC)/$(M))
KBUILD_OPTIONS += MODNAME=audio_dlkm
KBUILD_OPTIONS += BOARD_PLATFORM=kalama
KBUILD_OPTIONS += CONFIG_ARCH_KALAMA=y

all: modules

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) modules $(KBUILD_OPTIONS) KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/qcom/opensource/mm-drivers/msm_ext_display/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-charger/Module.symvers"

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) INSTALL_MOD_STRIP=1 modules_install $(KBUILD_OPTIONS) KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/qcom/opensource/mm-drivers/msm_ext_display/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-charger/Module.symvers"

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) clean $(KBUILD_OPTIONS) KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/qcom/opensource/mm-drivers/msm_ext_display/Module.symvers $(OUT_DIR)/../m2391-modules/meizu/power-charger/Module.symvers"

.PHONY: all modules modules_install clean
