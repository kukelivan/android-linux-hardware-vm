obj-m := gh_hugepage_reserve.o
ccflags-y += -Wno-missing-prototypes -Wno-unused-function
KDIR ?= /lib/modules/$(shell uname -r)/build
module:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
