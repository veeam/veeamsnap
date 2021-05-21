# Copyright (c) Veeam Software Group GmbH

# Generate config.h file with specific options for module

if [ ! -z "$1" ]; then
	KERNEL_VERSION="$1"
else
	KERNEL_VERSION="$(uname -r)"
fi

if [ ! -z "$2" ]; then
	OUTPUT_FILE=$2
else
	OUTPUT_FILE="$(dirname "$0")/config.h"
fi

echo "// Copyright (c) Veeam Software Group GmbH" > ${OUTPUT_FILE}
echo "#ifndef VEEAM_CONFIG" >> ${OUTPUT_FILE}
echo "#define VEEAM_CONFIG" >> ${OUTPUT_FILE}

# parses the system map and determines the addresses for some non-exported functions
SYSTEM_MAP_FILE="/lib/modules/${KERNEL_VERSION}/System.map"
if [ ! -f "${SYSTEM_MAP_FILE}" ]; then
	SYSTEM_MAP_FILE="/boot/System.map-${KERNEL_VERSION}"
fi

SYMBOLS="printk blk_mq_submit_bio"
for SYMBOL_NAME in ${SYMBOLS} ; do
	if [ -z "${SYMBOL_NAME}" ]; then
		continue
	fi

	echo "performing ${SYMBOL_NAME} lookup"
	MACRO_NAME="$(echo ${SYMBOL_NAME} | awk '{print toupper($0)}')_ADDR"
	SYMBOL_ADDR=$(grep " ${SYMBOL_NAME}$" "${SYSTEM_MAP_FILE}" | awk '{print $1}')
	if [ -z "${SYMBOL_ADDR}" ]; then
		SYMBOL_ADDR="0"
	fi
	echo "#define ${MACRO_NAME} 0x${SYMBOL_ADDR}" >> ${OUTPUT_FILE}
done

# the end of the config file
echo "#endif" >> ${OUTPUT_FILE}
