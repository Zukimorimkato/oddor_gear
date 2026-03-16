obj-m := oddor_gear.o

KVERSION := $(shell uname -r)
KERNEL_SRC := /lib/modules/$(KVERSION)/build
SRC_DIR := $(shell pwd)

all:
	@test -d $(KERNEL_SRC) || \
		(echo "ERROR: Kernel headers not found at $(KERNEL_SRC)."; \
		 echo "       Install with: sudo apt install linux-headers-$(KVERSION)"; \
		 exit 1)
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC_DIR) modules

clean:
	rm -f *.o *.ko *.mod.o *.mod.c *.symvers *.order
	rm -rf .tmp_versions

clean-dkms:
	$(MAKE) clean

help:
	@echo "Targets:"
	@echo "  all        - Build the kernel module (requires kernel headers)"
	@echo "  clean      - Remove build artifacts"
	@echo "  clean-dkms - Alias for clean (called by DKMS)"
	@echo "  help       - Show this message"
	@echo ""
	@echo "Kernel: $(KVERSION)"
	@echo "Headers: $(KERNEL_SRC)"
