#ifndef MRE_DRIVER_H
#define MRE_DRIVER_H

#include "mre_device.h"

#define MRE_DRIVER_NAME_MAX 48u
#define MRE_DRIVER_REGISTRY_CAPACITY 32u

typedef mre_status_t (*mre_driver_probe_fn)(const mre_device_t *device);
typedef void (*mre_driver_remove_fn)(const mre_device_t *device);

typedef struct mre_driver {
    const char *name;
    const char *compatible;
    mre_driver_probe_fn probe;
    mre_driver_remove_fn remove;
} mre_driver_t;

mre_status_t mre_driver_register(const mre_driver_t *driver);
mre_status_t mre_driver_bind(const mre_device_t *device);
size_t mre_driver_count(void);
void mre_driver_reset_for_test(void);

#endif
