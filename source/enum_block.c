// Copyright (c) Veeam Software Group GmbH

#include "stdafx.h"
#include "enum_block.h"
//#include <linux/sysfs.h>
#include <linux/mount.h>

#define SECTION "enum_block"
#include "log_format.h"
/*

bool device_exist(char* partuuid)
{
    dev_t _dev;

    log_tr_s("Try to find device ", partuuid);
    _dev = name_to_dev_t(partuuid);

    if (_dev == 0ul)
        log_tr("Device not found");
    else
        log_tr_dev_t("Found device by UUID: ", _dev);

    return (_dev != 0ul);
}
*/

/*

static struct class _block_class = {
    .name = "block",
};
*/

/*

static int match_dev_by_uuid(struct device *dev, const void *data)
{
    //const struct uuidcmp *cmp = data;
    struct hd_struct *part = dev_to_part(dev);

    if (!part->info)
        return 0;

    log_tr_dev_t("Found device: ", dev->devt);
    log_tr_uuid("uuid: ", (const veeam_uuid_t*)part->info->uuid);
    //if (strncasecmp(cmp->uuid, part->info->uuid, cmp->len))
    //    goto no_match;

    //return 1;
    return 0;//no_match
}
*/
/*

static int enum_device(struct device *dev, const void *data)
{
    log_tr_dev_t("Found device: ", dev->devt);
    return 0;
}*/

void enum_block_print_state(void )
{
    log_tr("");
    log_tr("Enumerate available block devices information:");

    {
//         struct device *dev = NULL;
//
//         //struct uuidcmp cmp;
//         dev = class_find_device(&_block_class, NULL, NULL/* &cmp */, &match_dev_by_uuid);
//         if (!dev)
//         {
//             //not found
//         }

        //class_for_each_device(&block_class, NULL, NULL, enum_device);

        //PARTUUID=00112233-4455-6677-8899-AABBCCDDEEFF
        //https://elixir.bootlin.com/linux/latest/source/init/do_mounts.c#L248
        //name_to_dev_t("/dev/sda1");
/*
        {
            dev_t root_dev = name_to_dev_t("/dev/root");
            if (!root_dev)
                log_tr("Root device not found");
            else
                log_tr_dev_t("Found root device: ", root_dev);
        }
        {
            dev_t _dev = name_to_dev_t("/dev/sda1");
            if (!_dev)
                log_tr("Device not found");
            else
                log_tr_dev_t("Found device: ", _dev);
        }
*/

        {
            //sda1 "6d1acaaa-c4aa-4350-8fb0-161104da44d6"
            //sda2 "1enX0E-VMx4-IN8h-Almx-NbZX-k0kh-qJenFK"
            //root "cde33bf2-4701-4cf0-ad0a-0fac11f77b11"
            //swap "f135ae92-04b7-4ca7-9adf-7460b10f4200"
            //loop0 "2018-10-10-18-34-13-00"


            //device_exist("PARTUUID=6d1acaaa-c4aa-4350-8fb0-161104da44d6");
            //device_exist("PARTUUID=cde33bf2-4701-4cf0-ad0a-0fac11f77b11");
            //device_exist("PARTUUID=2018-10-10-18-34-13-00");
        }
    }
}
