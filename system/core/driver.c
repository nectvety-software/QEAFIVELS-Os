/* SPDX-License-Identifier: MIT */
/* system/core/driver.c - QEAFIVELS driver core.
 *
 * Registration, one instance per bind, and one state transition per probe.
 * Deliberately freestanding: no printf, no allocation, no locks - it is linked
 * into KERNEL.EFI and called from the boot path as well as from host tests.
 *
 * The rules this file enforces:
 *   - only probe() returning 0 produces READY;
 *   - a deferral is DEFERRED, never FAILED and never READY;
 *   - the same device cannot be bound twice to the same driver;
 *   - remove() runs only for an instance that probed successfully, so a failed
 *     probe cannot be "cleaned up" into looking like a healthy device.
 */
#include <string.h>

#include "include/vqeaf/driver.h"

static const VqeafDriver *drivers[VQEAF_DRIVER_MAX];
static VqeafDriverInstance instances[VQEAF_DRIVER_MAX];
static uint32_t driver_count, instance_count;

static int name_valid(const char *name)
{
    return name != NULL && name[0] != '\0';
}

static int driver_valid(const VqeafDriver *d)
{
    return d != NULL && d->abi == VQEAF_DRIVER_ABI &&
           d->size >= sizeof(VqeafDriver) && name_valid(d->name) &&
           d->probe != NULL;
}

void vqeaf_driver_reset(void)
{
    memset(drivers, 0, sizeof(drivers));
    memset(instances, 0, sizeof(instances));
    driver_count = 0;
    instance_count = 0;
}

int vqeaf_driver_register(const VqeafDriver *driver)
{
    uint32_t i;

    if(!driver_valid(driver)) return -1;
    for(i = 0; i < driver_count; i++)
        if(strcmp(drivers[i]->name, driver->name) == 0) return -2;
    if(driver_count == VQEAF_DRIVER_MAX) return -3;
    drivers[driver_count++] = driver;
    return 0;
}

static const VqeafDriver *find_driver(const char *name)
{
    uint32_t i;

    if(!name_valid(name)) return NULL;
    for(i = 0; i < driver_count; i++)
        if(strcmp(drivers[i]->name, name) == 0) return drivers[i];
    return NULL;
}

/* A REMOVED instance is history, not a live binding, so it never blocks a new
 * bind of the same device. */
static VqeafDriverInstance *find_instance(const VqeafDriver *d, void *device)
{
    uint32_t i;

    for(i = 0; i < instance_count; i++)
        if(instances[i].driver == d && instances[i].device == device &&
           instances[i].state != VQEAF_DRV_REMOVED)
            return &instances[i];
    return NULL;
}

static int probe_instance(VqeafDriverInstance *x)
{
    int rc;

    x->attempts++;
    x->state = VQEAF_DRV_PROBING;
    rc = x->driver->probe(x->device);
    x->last_error = rc;
    if(rc == 0) x->state = VQEAF_DRV_READY;
    else if(rc == VQEAF_DRIVER_DEFER) x->state = VQEAF_DRV_DEFERRED;
    else x->state = VQEAF_DRV_FAILED;
    return rc;
}

int vqeaf_driver_bind(const char *name, void *device, uint32_t *instance_id)
{
    const VqeafDriver *d;
    VqeafDriverInstance *x;

    if(instance_id == NULL) return -1;
    *instance_id = 0;
    if(!name_valid(name)) return -1;
    d = find_driver(name);
    if(d == NULL) return -2;
    if(find_instance(d, device) != NULL) return -4;
    if(instance_count == VQEAF_DRIVER_MAX) return -3;

    x = &instances[instance_count];
    memset(x, 0, sizeof(*x));
    x->driver = d;
    x->device = device;
    x->state = VQEAF_DRV_PROBING;
    *instance_id = instance_count;
    instance_count++;
    return probe_instance(x);
}

int vqeaf_driver_bind_match(void *device, uint32_t *instance_id)
{
    uint32_t i;

    if(instance_id == NULL) return -1;
    *instance_id = 0;
    for(i = 0; i < driver_count; i++) {
        const VqeafDriver *d = drivers[i];
        if(d->match != NULL && d->match(device) <= 0) continue;
        return vqeaf_driver_bind(d->name, device, instance_id);
    }
    return -5;
}

int vqeaf_driver_unbind(uint32_t instance_id)
{
    VqeafDriverInstance *x;
    int rc = 0;

    if(instance_id >= instance_count) return -1;
    x = &instances[instance_id];
    if(x->driver == NULL || x->state == VQEAF_DRV_REMOVED) return -1;
    /* Only a device that probed has something to tear down. A DEFERRED or
     * FAILED instance never acquired resources, so remove() must not run. */
    if((x->state == VQEAF_DRV_READY || x->state == VQEAF_DRV_DEGRADED) &&
       x->driver->remove != NULL)
        rc = x->driver->remove(x->device);
    x->state = VQEAF_DRV_REMOVED;
    x->last_error = rc;
    return rc;
}

int vqeaf_driver_get(uint32_t instance_id, VqeafDriverInstance *out)
{
    if(out == NULL || instance_id >= instance_count ||
       instances[instance_id].driver == NULL)
        return -1;
    *out = instances[instance_id];
    return 0;
}

int vqeaf_driver_retry_deferred(void)
{
    uint32_t i;
    int ready = 0;

    for(i = 0; i < instance_count; i++) {
        if(instances[i].driver == NULL ||
           instances[i].state != VQEAF_DRV_DEFERRED)
            continue;
        if(probe_instance(&instances[i]) == 0) ready++;
    }
    return ready;
}

int vqeaf_driver_find(const char *name, uint32_t *instance_id)
{
    uint32_t i;

    if(instance_id == NULL || !name_valid(name)) return -1;
    for(i = 0; i < instance_count; i++) {
        if(instances[i].driver == NULL ||
           instances[i].state == VQEAF_DRV_REMOVED)
            continue;
        if(strcmp(instances[i].driver->name, name) == 0) {
            *instance_id = i;
            return 0;
        }
    }
    return -2;
}

uint32_t vqeaf_driver_count(void)
{
    return driver_count;
}

uint32_t vqeaf_driver_instance_count(void)
{
    return instance_count;
}

const char *vqeaf_driver_state_name(VqeafDriverState state)
{
    switch(state) {
    case VQEAF_DRV_REGISTERED: return "registered";
    case VQEAF_DRV_PROBING: return "probing";
    case VQEAF_DRV_READY: return "ready";
    case VQEAF_DRV_DEGRADED: return "degraded";
    case VQEAF_DRV_FAILED: return "failed";
    case VQEAF_DRV_REMOVED: return "removed";
    case VQEAF_DRV_DEFERRED: return "deferred";
    }
    return "?";
}

/* ---- bounded formatting, no snprintf in a freestanding kernel ---- */

static int put(char *out, int cap, int at, const char *text)
{
    if(text == NULL) return at;
    while(*text != '\0' && at < cap - 1) out[at++] = *text++;
    out[at] = '\0';
    return at;
}

static int put_u32(char *out, int cap, int at, uint32_t value)
{
    char digits[11];
    int n = 0;

    do {
        digits[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while(value != 0 && n < (int)sizeof(digits));
    while(n > 0) {
        if(at >= cap - 1) break;
        out[at++] = digits[--n];
    }
    out[at] = '\0';
    return at;
}

static int put_i32(char *out, int cap, int at, int value)
{
    uint32_t magnitude;

    if(value < 0) {
        at = put(out, cap, at, "-");
        magnitude = (uint32_t)(-(value + 1)) + 1u;
    } else {
        magnitude = (uint32_t)value;
    }
    return put_u32(out, cap, at, magnitude);
}

int vqeaf_driver_report(char *out, int cap)
{
    uint32_t i, j;
    int at = 0;

    if(out == NULL || cap <= 0) return -1;
    out[0] = '\0';

    at = put(out, cap, at, "drv: registered=");
    at = put_u32(out, cap, at, driver_count);
    at = put(out, cap, at, " instances=");
    at = put_u32(out, cap, at, instance_count);
    at = put(out, cap, at, "\n");

    for(i = 0; i < instance_count; i++) {
        const VqeafDriverInstance *x = &instances[i];
        if(x->driver == NULL) continue;
        at = put(out, cap, at, "drv: ");
        at = put(out, cap, at, x->driver->name);
        at = put(out, cap, at, " state=");
        at = put(out, cap, at, vqeaf_driver_state_name(x->state));
        at = put(out, cap, at, " class=");
        at = put_u32(out, cap, at, (uint32_t)x->driver->device_class);
        at = put(out, cap, at, " err=");
        at = put_i32(out, cap, at, x->last_error);
        at = put(out, cap, at, " tries=");
        at = put_u32(out, cap, at, x->attempts);
        at = put(out, cap, at, "\n");
    }

    /* Drivers that were registered but never bound are part of the picture
     * too: an unused driver is why a device can stay PRESENT with no READY. */
    for(j = 0; j < driver_count; j++) {
        int bound = 0;
        for(i = 0; i < instance_count; i++)
            if(instances[i].driver == drivers[j] &&
               instances[i].state != VQEAF_DRV_REMOVED) {
                bound = 1;
                break;
            }
        if(bound) continue;
        at = put(out, cap, at, "drv: ");
        at = put(out, cap, at, drivers[j]->name);
        at = put(out, cap, at, " state=registered class=");
        at = put_u32(out, cap, at, (uint32_t)drivers[j]->device_class);
        at = put(out, cap, at, "\n");
    }
    return at;
}
