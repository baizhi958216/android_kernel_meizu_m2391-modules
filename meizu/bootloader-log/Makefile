KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) modules
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) clean
