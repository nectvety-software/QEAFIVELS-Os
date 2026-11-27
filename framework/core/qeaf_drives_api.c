/* SPDX-License-Identifier: MIT */
/* framework/core/qeaf_drives_api.c - the v1 driver census over the core.
 *
 * Deliberately freestanding: no printf, no allocation. The census walks
 * vqeaf_driver_instance_count() and skips REMOVED instances, so count() and
 * info() always agree on what "index n" means even after an unbind.
 */
#include "qeaf_drives_api.h"

#include "vqeaf/driver.h"

static size_t copy_text(char *dst, size_t capacity, const char *src)
{
    size_t at = 0u;

    if (dst == NULL || capacity == 0u) return 0u;
    if (src != NULL) {
        while (src[at] != '\0' && at + 1u < capacity) {
            dst[at] = src[at];
            at++;
        }
    }
    dst[at] = '\0';
    return at;
}

/* The n-th live instance, in core order. */
static int live_at(uint32_t index, VqeafDriverInstance *out)
{
    uint32_t i, seen = 0u;
    VqeafDriverInstance current;

    if (out == NULL) return -1;
    for (i = 0; i < vqeaf_driver_instance_count(); ++i) {
        if (vqeaf_driver_get(i, &current) != 0) continue;
        if (current.state == VQEAF_DRV_REMOVED) continue;
        if (seen == index) {
            *out = current;
            return 0;
        }
        seen++;
    }
    return -1;
}

static uint32_t census_count(void)
{
    uint32_t i, live = 0u;
    VqeafDriverInstance current;

    for (i = 0; i < vqeaf_driver_instance_count(); ++i) {
        if (vqeaf_driver_get(i, &current) != 0) continue;
        if (current.state == VQEAF_DRV_REMOVED) continue;
        live++;
    }
    return live;
}

static int census_info(uint32_t index, qeaf_drive_info_t *out)
{
    VqeafDriverInstance instance;

    if (out == NULL) return -1;
    if (live_at(index, &instance) != 0) return -1;
    copy_text(out->name, sizeof(out->name),
              instance.driver != NULL ? instance.driver->name : "");
    copy_text(out->state, sizeof(out->state),
              vqeaf_driver_state_name(instance.state));
    out->device_class =
        instance.driver != NULL ? (uint32_t)instance.driver->device_class : 0u;
    out->last_error = instance.last_error;
    out->attempts = instance.attempts;
    return 0;
}

static int census_report(char *buffer, uint32_t capacity)
{
    if (capacity > 0x7fffffffu) return -1;
    return vqeaf_driver_report(buffer, (int)capacity);
}

const qeaf_drives_api_v1 *qeaf_drives_api(void)
{
    static const qeaf_drives_api_v1 api = {
        (uint32_t)sizeof(qeaf_drives_api_v1),
        VQEAF_DRIVES_API_VERSION,
        census_count,
        census_info,
        census_report
    };

    return &api;
}
