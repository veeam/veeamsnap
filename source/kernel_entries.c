// Copyright (c) Veeam Software Group GmbH

#include "stdafx.h"
#include <linux/kprobes.h>
#include "kernel_entries.h"

#define SECTION "k_entries "

#if defined(VEEAMSNAP_DISK_SUBMIT_BIO) || defined(VEEAMSNAP_MQ_REQUEST)
struct kernel_entry {
    char* name;
    void* addr;
};

static struct kernel_entry ke_addr_table[KE_SIZE] = {
#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
    { "blk_mq_submit_bio",
#if defined(KERNEL_ENTRY_BASE_ADDR) && defined(KE_BLK_MQ_SUBMIT_BIO)
      (void *)(BLK_MQ_SUBMIT_BIO_ADDR + ((unsigned long)(KERNEL_ENTRY_BASE_FUNCTION)-KERNEL_ENTRY_BASE_ADDR)) },
#else
      NULL },
#endif
#elif defined(VEEAMSNAP_MQ_REQUEST)
    { "blk_mq_make_request",
#if defined(KERNEL_ENTRY_BASE_ADDR) && defined(BLK_MQ_MAKE_REQUEST_ADDR)
      (void *)(BLK_MQ_MAKE_REQUEST_ADDR + ((unsigned long)(KERNEL_ENTRY_BASE_FUNCTION)-KERNEL_ENTRY_BASE_ADDR)) },
#else
      NULL },
#endif
#endif
};

int ke_set_addr(const char* name, void* addr)
{
    int ke_inx;

    for (ke_inx = 0; ke_inx < KE_SIZE; ke_inx++) {
        if (strcmp(ke_addr_table[ke_inx].name, name) == 0) {
            ke_addr_table[ke_inx].addr = addr;
            return 0;
        }
    }

    return -EINVAL;
}

int ke_get_unresolved(char *buf, size_t max_size)
{
    size_t src_len = 0;
    int buf_ofs = 0;
    int ke_inx;

    for (ke_inx = 0; ke_inx < KE_SIZE; ke_inx++) {
        if (ke_addr_table[ke_inx].addr)
            continue;

        src_len = strlen(ke_addr_table[ke_inx].name);
        if ((src_len + 1) > (max_size - buf_ofs))
            return -ENOSPC;

        strcpy(buf + buf_ofs, ke_addr_table[ke_inx].name);
        buf_ofs += (src_len + 1);
    }
    return buf_ofs;
}

void* ke_get_addr(int ke_inx)
{
    if (ke_inx >= KE_SIZE)
        return NULL;
    return ke_addr_table[ke_inx].addr;
}

static void *get_symbol_via_kprobe(char* name)
{
    void* addr;
    struct kprobe kp;
    int ret;

    memset(&kp, 0, sizeof(kp));
    kp.symbol_name = name;
    ret = register_kprobe(&kp);
    if (ret)
        return ERR_PTR(ret);

    addr = kp.addr;
    unregister_kprobe(&kp);

    return addr;
}

int ke_init(void)
{
    int i;

    for (i = 0; i < KE_SIZE; i++) {
        if (!ke_addr_table[i].addr) {
            char* name = ke_addr_table[i].name;
            void *addr;

            addr = get_symbol_via_kprobe(name);
            if (IS_ERR(addr)) {
                log_err_s("Failed to get address of function ", name);
                return PTR_ERR(addr);
            }

            ke_addr_table[i].addr = addr;
            log_tr_s("Detected address for ", name);
        }
    }
    return 0;
}
#endif
