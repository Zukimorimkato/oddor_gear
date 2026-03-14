obj-m := oddor_gear.o

KVERSION := $(shell uname -r)
KERNEL_SRC := /lib/modules/$(KVERSION)/build
SRC_DIR := $(shell pwd)

all:
	@test -d $(KERNEL_SRC) || (echo "ERROR: Kernel headers not found at $(KERNEL_SRC). Install them"; exit 1)
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC_DIR) modules

clean:
	rm -f *.o *.ko *.mod.o *.mod.c *.symvers *.order
	rm -rf .tmp_versions

clean-dkms:
	$(MAKE) clean

help:
	@echo "Targets:"
	@echo "  all        - Build the kernel module"
	@echo "  clean      - Remove build artifacts"
	@echo "  clean-dkms - Alias for clean (used by DKMS)"
