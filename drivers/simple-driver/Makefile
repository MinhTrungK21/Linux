obj-m += hello_kernel_driver.o
KDIR = /home/iris/ti-sdk-kernel/board-support/ti-linux-kernel-6.1.119+gitAUTOINC+c490f4c0fe-ti
CURRENTDIR=$(shell pwd)
CROSS=/home/iris/ti-sdk-kernel/external-toolchain-dir/arm-gnu-toolchain-11.3.rel1-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-
LOCALVERSION ?= -ti-gc490f4c0fe51
all:
	$(MAKE) -C $(KDIR) M=$(CURRENTDIR) ARCH=arm CROSS_COMPILE=$(CROSS) modules LOCALVERSION=$(LOCALVERSION)
install:
	scp hello_kernel_driver.ko root@192.168.9.8:/home/root
clean:
	$(MAKE) -C $(KDIR) M=$(CURRENTDIR) ARCH=arm CROSS_COMPILE=$(CROSS) clean
