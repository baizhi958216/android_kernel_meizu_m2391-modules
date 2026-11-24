# SPDX-License-Identifier: GPL-2.0-only
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(CURDIR)
MO ?= $(OUT_DIR)/$(M)

KBUILD_OPTIONS :=
KBUILD_OPTIONS += BT_ROOT=$(abspath $(KERNEL_SRC)/$(M))
KBUILD_OPTIONS += CONFIG_MSM_BT_POWER=m
KBUILD_OPTIONS += CONFIG_BTFM_SLIM=m
KBUILD_OPTIONS += CONFIG_SLIM_BTFM_CODEC=n
KBUILD_OPTIONS += CONFIG_I2C_RTC6226_QCA=n

all: modules

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) modules $(KBUILD_OPTIONS) KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/qcom/opensource/wlan/platform/Module.symvers"

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) INSTALL_MOD_STRIP=1 modules_install $(KBUILD_OPTIONS) KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/qcom/opensource/wlan/platform/Module.symvers"

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) clean $(KBUILD_OPTIONS) KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/qcom/opensource/wlan/platform/Module.symvers"

.PHONY: all modules modules_install clean
