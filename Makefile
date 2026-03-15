obj-m := oddor_gear.o

KVERSION := $(shell uname -r)
KERNEL_SRC := /lib/modules/$(KVERSION)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

clean:
	rm -f *.o *.ko *.mod.o *.mod.c *.symvers *.order
	rm -rf .tmp_versions

clean-dkms:
	$(MAKE) clean
