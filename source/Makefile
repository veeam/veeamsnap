#
# veeamsnap kernel module make file
#
KERNEL_RELEASE := $(shell uname -r)
KERNEL_MODULE_NAME := veeamsnap

KERNEL_MODULE_OBJECT_FILE_LIST := \
	log.o \
	mem_alloc.o \
	sparse_bitmap.o \
	container.o \
	container_spinlocking.o \
	queue_spinlocking.o \
	bitmap_sync.o \
	page_array.o \
	rangelist.o \
	rangelist_ex.o \
	rangevector.o \
	blk_util.o \
	blk_direct.o \
	blk_redirect.o \
	blk_deferred.o \
	defer_io.o \
	cbt_storage.o \
	cbt_map.o \
	cbt_notify.o \
	cbt_params.o \
	cbt_persistent.o \
	cbt_checkfs.o \
	ext4_check.o \
	tracker.o \
	tracker_queue.o \
	snapimage.o \
	snapdata_collect.o \
	blk_descr_mem.o \
	blk_descr_file.o \
	blk_descr_multidev.o \
	blk_descr_pool.o \
	blk_descr_array.o \
	snapstore.o \
	snapstore_device.o \
	snapstore_file.o \
	snapstore_mem.o \
	snapstore_multidev.o \
	snapshot.o \
	tracking.o \
	ctrl_fops.o \
	ctrl_pipe.o \
	ctrl_sysfs.o \
	enum_block.o \
	veeamsnap_main.o

obj-m := $(KERNEL_MODULE_NAME).o
$(KERNEL_MODULE_NAME)-y += $(KERNEL_MODULE_OBJECT_FILE_LIST)
ccflags-y := -O2 -Wno-multichar
SHELL:=/bin/bash
ccflags-y += $(shell if [[ -r /etc/os-release ]]; then . /etc/os-release; val="DISTRIB_NAME.$$ID"; echo "-D"$${val//[.-]/_} | awk '{print toupper($$0)}' ; fi )
ccflags-y += $(shell if [[ -r /etc/os-release ]]; then . /etc/os-release; awk '{ n=split($$0,v,"."); for (i=0; ++i<=n;) print "-DDISTRIB_VERSION_"i"="v[i] }' <<< $$VERSION_ID ; fi )

# try to find kernel headers files
include_linux = "."
ifeq ($(shell test -e "/lib/modules/$(KERNEL_RELEASE)/build/include/linux" && echo -n yes),yes)
include_linux = /lib/modules/$(KERNEL_RELEASE)/build/include/linux
endif
ifeq ($(shell test -e "/lib/modules/$(KERNEL_RELEASE)/source/include/linux" && echo -n yes),yes)
include_linux = /lib/modules/$(KERNEL_RELEASE)/source/include/linux
endif

# try to find patch for lookup_bdev
# https://patchwork.kernel.org/patch/7748091/
ifeq ($(shell test -e $(include_linux)/fs.h && echo -n yes),yes)
ccflags-y += $(shell if [[ 0 -ne $(shell cat $(include_linux)/fs.h | grep 'lookup_bdev' | grep -c 'mask' ) ]]; then echo "-DLOOKUP_BDEV_MASK"; fi )
endif

# try to find exported functions in rhel 8 kernel
system_map = "/lib/modules/$(KERNEL_RELEASE)/System.map"
ifeq ($(shell test -e $(system_map) && echo -n yes),yes) 
ccflags-y += $(shell cat $(system_map) | grep '__ksymtab_blk_mq_make_request' >>/dev/null && echo "-DVEEAMSNAP_MQ_REQUEST")
ccflags-y += $(shell cat $(system_map) | grep '__ksymtab_blk_alloc_queue_rh' >>/dev/null && echo "-DVEEAMSNAP_ALLOC_QUEUE_RH")
endif

all:
	@echo Configuring...
	#if [ ! -f config.h ]; then ./genconfig.sh ./config.h "$(KERNEL_RELEASE)"; fi;
	./genconfig.sh ./config.h "$(KERNEL_RELEASE)"
	@echo Making all...
	@echo ccflags-y= $(ccflags-y)
	make -j$(shell npros) -C /lib/modules/$(KERNEL_RELEASE)/build M=$(PWD) modules
	@echo Completed.

clean:
	@echo Cleaning ...
	make -C /lib/modules/$(KERNEL_RELEASE)/build M=$(PWD) clean
	@rm ./config.h
	@echo Completed.

install:
	@echo Installing veeamsnap kernel module
	mkdir -p /lib/modules/$(KERNEL_RELEASE)/kernel/drivers/veeam
	cp $(KERNEL_MODULE_NAME).ko /lib/modules/$(KERNEL_RELEASE)/kernel/drivers/veeam
	depmod
	@echo Completed.

load:
	@echo Loading $(KERNEL_MODULE_NAME) kernel module from current folder
	mknod /dev/$(KERNEL_MODULE_NAME) c $(shell insmod ./$(KERNEL_MODULE_NAME).ko zerosnapdata=1; cat /proc/devices | grep $(KERNEL_MODULE_NAME) | awk '{print $$1}') 0

unload:
	@echo Unloading $(KERNEL_MODULE_NAME) kernel module
	@echo $(shell rmmod $(KERNEL_MODULE_NAME); rm -f /dev/$(KERNEL_MODULE_NAME))
	$(shell rmmod $(KERNEL_MODULE_NAME))
