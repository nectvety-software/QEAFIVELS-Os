#ifndef MRE_DEVICE_MANAGER_H
#define MRE_DEVICE_MANAGER_H
#include "mre_device.h"
#define MRE_DEVICE_REGISTRY_CAPACITY 64u
mre_status_t mre_device_register(const mre_device_t *device);
const mre_device_t *mre_device_find(const char *name);
const mre_device_t *mre_device_find_compatible(const char *compatible, size_t index);
size_t mre_device_count(void);
void mre_device_reset_for_test(void);
#endif
