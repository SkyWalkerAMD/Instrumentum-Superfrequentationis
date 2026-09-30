KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build
MODULE_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
all:
	$(MAKE) -C $(KDIR) M=$(MODULE_DIR) modules
clean:
	$(MAKE) -C $(KDIR) M=$(MODULE_DIR) clean
.PHONY: all clean
