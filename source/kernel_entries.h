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

#if defined(VEEAMSNAP_DISK_SUBMIT_BIO) || defined(VEEAMSNAP_MQ_REQUEST)
int ke_set_addr(const char* name, void* addr);
int ke_get_unresolved(char *buf, size_t max_size);
void* ke_get_addr(int ke_inx);
int ke_init(void);
#else
static inline int ke_set_addr(
    __attribute__((unused)) const char* name,
    __attribute__((unused)) void* addr)
{
    return -EINVAL;
};
static inline int ke_get_unresolved(
    __attribute__((unused)) char* buf,
    __attribute__((unused)) size_t max_size)
{
    return 0;
};
static inline void* ke_get_addr(
    __attribute__((unused)) int ke_inx)
{
    return NULL;
};
static inline int ke_init(void)
{
    return 0;
};
#endif

