# SPDX-License-Identifier: GPL-2.0-only
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(CURDIR)
MO ?= $(OUT_DIR)/$(M)

KBUILD_OPTIONS :=
KBUILD_OPTIONS += WLAN_PLATFORM_ROOT=$(abspath $(KERNEL_SRC)/$(M))
KBUILD_OPTIONS += CONFIG_CNSS_OUT_OF_TREE=y
KBUILD_OPTIONS += CONFIG_CNSS_GENL=m
KBUILD_OPTIONS += CONFIG_WCNSS_MEM_PRE_ALLOC=m
KBUILD_OPTIONS += CONFIG_CNSS_UTILS=m
KBUILD_OPTIONS += CONFIG_CNSS_QMI_SVC=m
KBUILD_OPTIONS += CONFIG_CNSS_PLAT_IPC_QMI_SVC=m
KBUILD_OPTIONS += CONFIG_CNSS2=m
KBUILD_OPTIONS += CONFIG_CNSS2_QMI=y
KBUILD_OPTIONS += CONFIG_CNSS_HW_SECURE_DISABLE=y

all: modules

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) modules $(KBUILD_OPTIONS) KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/qcom/opensource/securemsm-kernel/smcinvoke/Module.symvers"

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) INSTALL_MOD_STRIP=1 modules_install $(KBUILD_OPTIONS) KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/qcom/opensource/securemsm-kernel/smcinvoke/Module.symvers"

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) MO=$(MO) clean $(KBUILD_OPTIONS) KBUILD_EXTRA_SYMBOLS="$(OUT_DIR)/../m2391-modules/qcom/opensource/securemsm-kernel/smcinvoke/Module.symvers"

.PHONY: all modules modules_install clean
