#!/bin/bash -e
# Copyright (c) Veeam Software Group GmbH
#
# Generate config.h file with specific options for module
if [ -n "$1" ]
then
	KERNEL_VERSION="$1"
else
	KERNEL_VERSION="$(uname -r)"
fi

if [ -n "$2" ]
then
	OUTPUT_FILE=$2
else
	OUTPUT_FILE="$(dirname "$0")/config.h"
fi
echo "Generate \"${OUTPUT_FILE}\" for kernel ${KERNEL_VERSION}"

echo "// Copyright (c) Veeam Software Group GmbH" > ${OUTPUT_FILE}
echo "#ifndef VEEAM_CONFIG" >> ${OUTPUT_FILE}
echo "#define VEEAM_CONFIG" >> ${OUTPUT_FILE}

if [[ -r /etc/os-release ]]
then
	. /etc/os-release
	DISTRIB_NAME=$(echo ${ID//[.-]/_} | awk '{print toupper($0)}')
	echo "#define DISTRIB_NAME_${DISTRIB_NAME}" >> ${OUTPUT_FILE}
	awk '{ n=split($0,v,"."); echo "n="n; for (i=0; ++i<=n;) print "#define DISTRIB_VERSION_"i" "v[i] }' <<< $VERSION_ID >> ${OUTPUT_FILE}
fi

# try to find kernel headers files
if [ -e "/lib/modules/${KERNEL_VERSION}/build/include/linux" ]
then
	LINUX_INCLUDE="/lib/modules/${KERNEL_VERSION}/build/include/linux"
elif [ -e "/lib/modules/${KERNEL_VERSION}/source/include/linux" ]
then
	LINUX_INCLUDE="/lib/modules/${KERNEL_VERSION}/source/include/linux"
fi

# try to find patch for lookup_bdev
# https://patchwork.kernel.org/patch/7748091/
if [ -n ${LINUX_INCLUDE} ]
then
	if [[ 0 -ne $(cat ${LINUX_INCLUDE}/fs.h | grep 'lookup_bdev' | grep -c 'mask' ) ]]
	then
		echo "#define LOOKUP_BDEV_MASK" >> ${OUTPUT_FILE}
	fi
fi

SYSTEM_MAP_FILE="/lib/modules/${KERNEL_VERSION}/System.map"
if [ ! -f "${SYSTEM_MAP_FILE}" ]
then
	SYSTEM_MAP_FILE="/boot/System.map-${KERNEL_VERSION}"
fi

# parses the system map and check exported symbols
SYMBOLS="blk_mq_make_request blk_alloc_queue_rh submit_bio_noacct"
for SYMBOL_NAME in ${SYMBOLS}
do
	SYMBOL_ADDR=$(grep " __ksymtab_${SYMBOL_NAME}$" "${SYSTEM_MAP_FILE}" | awk '{print $1}')
	if [ -z "${SYMBOL_ADDR}" ]
	then
		echo "Exported function \"${SYMBOL_NAME}\" not found"
	else
		MACRO_NAME="$(echo ${SYMBOL_NAME} | awk '{print toupper($0)}')_EXPORTED"
		echo "#define ${MACRO_NAME}" >> ${OUTPUT_FILE}
		echo "Exported function \"${SYMBOL_NAME}\" was found"
	fi
done

# parses the system map and determines the addresses for some non-exported functions
SYMBOLS="printk blk_mq_submit_bio"
for SYMBOL_NAME in ${SYMBOLS}
do
	SYMBOL_ADDR=$(grep " ${SYMBOL_NAME}$" "${SYSTEM_MAP_FILE}" | awk '{print $1}')
	if [ -z "${SYMBOL_ADDR}" ]
	then
		echo "Function \"${SYMBOL_NAME}\" not found"
	else
		MACRO_NAME="$(echo ${SYMBOL_NAME} | awk '{print toupper($0)}')_ADDR"
		echo "#define ${MACRO_NAME} 0x${SYMBOL_ADDR}" >> ${OUTPUT_FILE}
		echo "Address of the function \"${SYMBOL_NAME}\" was defined"
	fi
done

# the end of the config file
echo "#endif" >> ${OUTPUT_FILE}
