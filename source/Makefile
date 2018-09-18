#
# veeamsnap kernel module make file
#
KERNEL_MODULE_NAME := veeamsnap

KERNEL_MODULE_OBJECT_FILE_LIST := \
	log.o \
	mem_alloc.o \
	sparse_array_1lv.o \
	sparse_bitmap.o \
	container.o \
	container_spinlocking.o \
	queue_spinlocking.o \
	bitmap_sync.o \
	page_array.o \
	rangelist.o \
	rangevector.o \
	blk_util.o \
	blk_direct.o \
	blk_redirect.o \
	blk_deferred.o \
	defer_io.o \
	cbt_map.o \
	tracker.o \
	tracker_queue.o \
	snapimage.o \
	snapdata_collect.o \
	blk_descr_mem.o \
	blk_descr_file.o \
	blk_descr_array.o \
	snapstore.o \
	snapstore_device.o \
	snapstore_file.o \
	snapstore_mem.o \
	snapshotdata.o \
	snapshotdata_blkinfo.o \
	snapshotdata_buffer.o \
	snapshotdata_common.o \
	snapshotdata_file.o \
	snapshotdata_memory.o \
	snapshotdata_shared.o \
	snapshotdata_stretch.o \
	zerosectors.o \
	snapshot.o \
	tracking.o \
	ctrl_fops.o \
	ctrl_pipe.o \
	veeamsnap_main.o

obj-m := $(KERNEL_MODULE_NAME).o
$(KERNEL_MODULE_NAME)-y += $(KERNEL_MODULE_OBJECT_FILE_LIST)
ccflags-y := -O2 -Wno-multichar $(COMPATIBILITY_FLAGS)
SHELL:=/bin/bash 
ccflags-y += $(shell if [[ -r /etc/os-release ]]; then . /etc/os-release; val="DISTRIB_NAME.$$ID"; echo "-D"$${val//[.-]/_} | awk '{print toupper($$0)}' ; fi )
ccflags-y += $(shell if [[ -r /etc/os-release ]]; then . /etc/os-release; awk '{ n=split($$0,v,"."); for (i=0; ++i<=n;) print "-DDISTRIB_VERSION_"i"="v[i] }' <<< $$VERSION_ID ; fi )

all:
	@echo Making all...
	@echo ccflags-y= $(ccflags-y)
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
	mknod /dev/$(KERNEL_MODULE_NAME) c $(shell insmod ./$(KERNEL_MODULE_NAME).ko zerosnapdata=1; cat /proc/devices | grep $(KERNEL_MODULE_NAME) | awk '{print $$1}') 0

unload:
	@echo Unloading $(KERNEL_MODULE_NAME) kernel module
	@echo $(shell rmmod $(KERNEL_MODULE_NAME); rm -f /dev/$(KERNEL_MODULE_NAME))

