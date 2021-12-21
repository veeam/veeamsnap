#
# veeamsnap kernel module make file
#
KERNEL_RELEASE := $(shell uname -r)
KERNEL_MODULE_NAME := veeamsnap

VERSION_MAJOR?=5
VERSION_MINOR?=0
VERSION_REV?=2
VERSION_BUILD?=4567
VERSION=${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_REV}.${VERSION_BUILD}

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
ccflags-y := -O2 -Wno-multichar
SHELL:=/bin/bash 
ccflags-y += $(shell if [[ -r /etc/os-release ]]; then . /etc/os-release; val="DISTRIB_NAME.$$ID"; echo "-D"$${val//[.-]/_} | awk '{print toupper($$0)}' ; fi )
ccflags-y += $(shell if [[ -r /etc/os-release ]]; then . /etc/os-release; awk '{ n=split($$0,v,"."); for (i=0; ++i<=n;) print "-DDISTRIB_VERSION_"i"="v[i] }' <<< $$VERSION_ID ; fi )

default:
	@echo Configuring...
	chmod +x ./genconfig.sh && ./genconfig.sh "$(KERNEL_RELEASE)"
	@echo Making all...
	@echo ccflags-y= $(ccflags-y)
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
	mknod /dev/$(KERNEL_MODULE_NAME) c $(shell insmod ./$(KERNEL_MODULE_NAME).ko zerosnapdata=1; cat /proc/devices | grep $(KERNEL_MODULE_NAME) | awk '{print $$1}') 0

unload:
	@echo Unloading $(KERNEL_MODULE_NAME) kernel module
	@echo $(shell rmmod $(KERNEL_MODULE_NAME); rm -f /dev/$(KERNEL_MODULE_NAME))
	$(shell rmmod $(KERNEL_MODULE_NAME))

PWD=$(shell pwd)
BUILDROOT?=../build
dkms-deb-pkg:
	mkdir -p $(BUILDROOT)/src
	@cp -r ./* $(BUILDROOT)/src
	@cp -r ../debian $(BUILDROOT)/
	chmod 0666 $(BUILDROOT)/debian/control
	chmod 0766 $(BUILDROOT)/debian/rules
	chmod 0766 $(BUILDROOT)/src/genconfig.sh
	sed -i '0,/FILEVER_MAJOR [0-9]\+/s/FILEVER_MAJOR [0-9]\+/FILEVER_MAJOR $(VERSION_MAJOR)/g' $(BUILDROOT)/src/version.h
	sed -i '0,/FILEVER_MINOR [0-9]\+/s/FILEVER_MINOR [0-9]\+/FILEVER_MINOR $(VERSION_MINOR)/g' $(BUILDROOT)/src/version.h
	sed -i '0,/FILEVER_REVISION [0-9]\+/s/FILEVER_REVISION [0-9]\+/FILEVER_REVISION $(VERSION_REV)/g' $(BUILDROOT)/src/version.h
	sed -i '0,/FILEVER_BUILD [0-9]\+/s/FILEVER_BUILD [0-9]\+/FILEVER_BUILD $(VERSION_BUILD)/g' $(BUILDROOT)/src/version.h
	find $(BUILDROOT) -type f -exec sed -i 's/#PACKAGE_VERSION#/$(VERSION)/g' {} +
	cd $(BUILDROOT) && dpkg-buildpackage -us -uc -b
