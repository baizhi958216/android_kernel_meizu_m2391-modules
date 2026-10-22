ifneq ($(KERNELRELEASE),)
include $(src)/Kbuild
else
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) modules
endif
