#include "mre_device_manager.h"
#include <string.h>

static const mre_device_t *g_devices[MRE_DEVICE_REGISTRY_CAPACITY];
static size_t g_device_count;

mre_status_t mre_device_register(const mre_device_t *device)
{
    size_t i;
    if (device == NULL || device->name == NULL || device->compatible == NULL) {
        return MRE_E_INVALID;
    }
    for (i = 0; i < g_device_count; ++i) {
        if (strcmp(g_devices[i]->name, device->name) == 0) {
            return MRE_E_EXISTS;
        }
    }
    if (g_device_count >= MRE_DEVICE_REGISTRY_CAPACITY) {
        return MRE_E_NOMEM;
    }
    g_devices[g_device_count++] = device;
    return MRE_OK;
}

const mre_device_t *mre_device_find(const char *name)
{
    size_t i;
    if (name == NULL) return NULL;
    for (i = 0; i < g_device_count; ++i) {
        if (strcmp(g_devices[i]->name, name) == 0) return g_devices[i];
    }
    return NULL;
}

const mre_device_t *mre_device_find_compatible(const char *compatible, size_t index)
{
    size_t i, found = 0;
    if (compatible == NULL) return NULL;
    for (i = 0; i < g_device_count; ++i) {
        if (strcmp(g_devices[i]->compatible, compatible) == 0) {
            if (found == index) return g_devices[i];
            ++found;
        }
    }
    return NULL;
}

size_t mre_device_count(void) { return g_device_count; }

void mre_device_reset_for_test(void)
{
    size_t i;
    for (i = 0; i < MRE_DEVICE_REGISTRY_CAPACITY; ++i) g_devices[i] = NULL;
    g_device_count = 0;
}
