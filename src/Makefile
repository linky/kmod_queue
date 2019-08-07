ccflags-y := -std=gnu89
obj-m = queue.o
KVERSION = $(shell uname -r)
all:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) modules
	$(CC) queuectl.c -o queuectl
clean:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean
	rm -f queuectl
