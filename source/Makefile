#
# veeamsnap kernel module make file
#
KERNEL_MODULE_NAME := veeamsnap

KERNEL_MODULE_OBJECT_FILE_LIST := \
	mem_alloc.o \
	sparse_array_1lv.o \
	sparse_bitmap.o \
	container.o \
	container_spinlocking.o \
	queue_spinlocking.o \
	bitmap_sync.o \
	log.o \
	page_array.o \
	blk_dev_utile.o \
	dio_request.o \
	rangeset.o \
	rangelist.o \
	rangevector.o \
	defer_io.o \
	cbt_map.o \
	tracker.o \
	tracker_queue.o \
	snapimage.o \
	snapshotdata.o \
	snapdata_collect.o \
	snapshot.o \
	tracking.o \
	direct_device.o \
	ctrl_fops.o \
	ctrl_pipe.o \
	veeamsnap_main.o

obj-m := $(KERNEL_MODULE_NAME).o
$(KERNEL_MODULE_NAME)-y += $(KERNEL_MODULE_OBJECT_FILE_LIST)
ccflags-y := -g -ggdb -O1 -DDEBUG $(COMPATIBILITY_FLAGS) -Wno-multichar

all:
	@echo Making all...
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	@echo Completed.

clean:
	@echo Cleaning ...
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	@echo Completed.

install:
	@echo Installing veeamsnap kernel module
	mkdir -p /lib/modules/$(shell uname -r)/kernel/drivers/veeam
	cp $(KERNEL_MODULE_NAME).ko /lib/modules/$(shell uname -r)/kernel/drivers/veeam
	depmod
	@echo Completed.

load:
	@echo Loading $(KERNEL_MODULE_NAME) kernel module from current folder
	mknod /dev/$(KERNEL_MODULE_NAME) c $(shell insmod ./$(KERNEL_MODULE_NAME).ko; cat /proc/devices | grep $(KERNEL_MODULE_NAME) | awk '{print $$1}') 0

unload:
	@echo Unloading $(KERNEL_MODULE_NAME) kernel module
	@echo $(shell rmmod $(KERNEL_MODULE_NAME); rm -f /dev/$(KERNEL_MODULE_NAME))

