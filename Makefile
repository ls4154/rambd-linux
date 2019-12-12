obj-m := rambd.o
KDIR := /lib/modules/$(shell uname -r)/build

default:
	make -C $(KDIR) M=$(PWD) modules
clean:
	make -C $(KDIR) M=$(PWD) clean

