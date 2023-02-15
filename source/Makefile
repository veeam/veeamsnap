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
	kernel_entries.o \
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
EXTRA_CFLAGS += -O2 -Wno-multichar
SHELL:=/bin/bash
EXTRA_CFLAGS += $(shell if [[ -r /etc/os-release ]]; then . /etc/os-release; val="DISTRIB_NAME.$$ID"; echo "-D"$${val//[.-]/_} | awk '{print toupper($$0)}' ; fi )
EXTRA_CFLAGS += $(shell if [[ -r /etc/os-release ]]; then . /etc/os-release; awk '{ n=split($$0,v,"."); for (i=0; ++i<=n;) print "-DDISTRIB_VERSION_"i"="v[i] }' <<< $$VERSION_ID ; fi )

EXTRA_CFLAGS += $(shell                                                        \
	grep -qw "void submit_bio_noacct" $(srctree)/include/linux/blkdev.h &&     \
		echo -D VEEAMSNAP_VOID_SUBMIT_BIO)

EXTRA_CFLAGS += $(shell                                                        \
	grep -qw "void bio_set_dev" $(srctree)/include/linux/bio.h &&              \
		echo -D VEEAMSNAP_FUNC_BIO_SET_DEV)

EXTRA_CFLAGS += $(shell                                                        \
	test -f $(srctree)/include/linux/genhd.h &&                                \
		echo -D HAVE_GENHD_H)

EXTRA_CFLAGS += $(shell                                                        \
	grep -qw "BIO_MAX_PAGES" $(srctree)/include/linux/bio.h &&                 \
		echo -D HAVE_BIO_MAX_PAGES)

EXTRA_CFLAGS += $(shell                                                        \
	grep "bio_alloc_bioset" $(srctree)/include/linux/bio.h |                   \
	grep -qw "struct block_device" &&                                          \
		echo -D HAVE_BDEV_BIO_ALLOC)

EXTRA_CFLAGS += $(shell test -f $(srctree)/include/linux/genhd.h &&	           \
	grep -qw "int add_disk" $(srctree)/include/linux/genhd.h &&                \
		echo -D HAVE_ADD_DISK_RESULT)

EXTRA_CFLAGS += $(shell test -f $(srctree)/include/linux/blkdev.h &&           \
	grep -qw "int __must_check add_disk" $(srctree)/include/linux/blkdev.h &&               \
		echo -D HAVE_ADD_DISK_RESULT)

default:
	@echo Configuring...
	chmod +x ./genconfig.sh && ./genconfig.sh "$(KERNEL_RELEASE)"
	@echo Making all...
	make -j$(shell nproc) -C /lib/modules/$(KERNEL_RELEASE)/build M=$(PWD) modules
	@echo Completed.

clean:
	@echo Cleaning ...
	make -C /lib/modules/$(KERNEL_RELEASE)/build M=$(PWD) clean
	@rm -f ./config.h
	@echo Completed.

#install: default
#	$(MAKE) -C /lib/modules/$(KERNEL_RELEASE)/build M=$(PWD) modules_install
install: default
	@echo Installing veeamsnap kernel module
	mkdir -p /lib/modules/$(KERNEL_RELEASE)/kernel/drivers/veeam
	cp $(KERNEL_MODULE_NAME).ko /lib/modules/$(KERNEL_RELEASE)/kernel/drivers/veeam
	depmod
	@echo Completed.
uninstall:
	$(RM) /lib/modules/$(KERNEL_RELEASE)/kernel/drivers/veeam/veeamsnap.ko

load:
	@echo Loading $(KERNEL_MODULE_NAME) kernel module from current folder
	insmod ./$(KERNEL_MODULE_NAME).ko zerosnapdata=1

unload:
	@echo Unloading $(KERNEL_MODULE_NAME) kernel module
	rmmod $(KERNEL_MODULE_NAME)
