# octool portability layer. Two artifacts:
#   make kmod   - build the kernel module against the running kernel
#   make hal    - build the userspace static library
#   make check  - run the offline protocol test
#   make dkms-install / dkms-remove - manage the module via DKMS
DKMS_NAME := octool-hwio
DKMS_VER  := $(shell cat ../VERSION)
all: hal
kmod:
	$(MAKE) -C kmod
hal:
	$(MAKE) -C hal
check:
	$(MAKE) -C tests check
dkms-install:
	sh packaging/dkms-install.sh $(DKMS_VER)
dkms-remove:
	-dkms remove -m $(DKMS_NAME) -v $(DKMS_VER) --all
clean:
	$(MAKE) -C hal clean
	$(MAKE) -C tests clean
	$(MAKE) -C kmod clean
.PHONY: all kmod hal check dkms-install dkms-remove clean
