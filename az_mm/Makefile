# Makefile for the azul memory management modules

#EXTRA_CFLAGS += -I$(srctree)/mm

obj-m += azul_mm.o

azul_mm-objs := az_pmem.o az_vmem.o az_mm.o az_mm_ioctl.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
 
all::
	$(MAKE) -C $(KERNELDIR) M=$(shell pwd) modules

install::
	$(MAKE) -C $(KERNELDIR) M=$(shell pwd) modules_install

clean::
	$(MAKE) -C $(KERNELDIR) M=$(shell pwd) clean
