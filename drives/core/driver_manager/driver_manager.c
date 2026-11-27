#include "mre_driver.h"
#include <string.h>

static const mre_driver_t *g_drivers[MRE_DRIVER_REGISTRY_CAPACITY];
static size_t g_driver_count;

mre_status_t mre_driver_register(const mre_driver_t *driver)
{
    size_t i;
    if (driver == NULL || driver->name == NULL || driver->compatible == NULL ||
        driver->probe == NULL) {
        return MRE_E_INVALID;
    }
    for (i = 0; i < g_driver_count; ++i) {
        if (g_drivers[i] == driver ||
            strcmp(g_drivers[i]->compatible, driver->compatible) == 0) {
            return MRE_E_EXISTS;
        }
    }
    if (g_driver_count >= MRE_DRIVER_REGISTRY_CAPACITY) {
        return MRE_E_NOMEM;
    }
    g_drivers[g_driver_count++] = driver;
    return MRE_OK;
}

mre_status_t mre_driver_bind(const mre_device_t *device)
{
    size_t i;
    if (device == NULL || device->compatible == NULL) {
        return MRE_E_INVALID;
    }
    for (i = 0; i < g_driver_count; ++i) {
        const mre_driver_t *driver = g_drivers[i];
        if (strcmp(driver->compatible, device->compatible) == 0) {
            return driver->probe(device);
        }
    }
    return MRE_E_NOT_FOUND;
}

size_t mre_driver_count(void)
{
    return g_driver_count;
}

void mre_driver_reset_for_test(void)
{
    size_t i;
    for (i = 0; i < MRE_DRIVER_REGISTRY_CAPACITY; ++i) {
        g_drivers[i] = NULL;
    }
    g_driver_count = 0;
}
