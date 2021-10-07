// Copyright (c) Veeam Software Group GmbH

#pragma once

enum {
#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
    KE_BLK_MQ_SUBMIT_BIO,
#elif defined(VEEAMSNAP_MQ_REQUEST)
    KE_BLK_MQ_MAKE_REQUEST,
#endif
    KE_SIZE
};

int ke_set_addr(const char* name, void* addr);
int ke_get_unresolved(char *buf, size_t max_size);
void* ke_get_addr(int ke_inx);

