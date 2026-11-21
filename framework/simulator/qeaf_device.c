/* SPDX-License-Identifier: MIT */
/* framework/simulator/qeaf_device.c - built-in QEAPP VM device profiles. */
#include "qeaf_device.h"

#include <string.h>

#include "qeaf_broker.h"
#include "violet.h"

/* Redmi Note 7 Pro (codename violet). The phone panel is 1080x2340; the
 * QEAPP canvas is 720x1560, an exact 1.5x fit with no letterboxing. Keeping
 * these numbers in one profile means the host VM, the on-device broker and
 * panel_scale.c all describe the same device. */
static const qeaf_device_profile_t kViolet = {
    "violet",
    "Redmi Note 7 Pro",
    VIO_PANEL_W,
    VIO_PANEL_H,
    VIO_UI_PORTRAIT_W,
    VIO_UI_PORTRAIT_H,
    0,
    "XRGB8888",
    QEAF_QUOTA_MEMORY_BYTES,
    QEAF_QUOTA_CALL_MS,
};

static const qeaf_device_profile_t *const kDevices[] = { &kViolet };

size_t qeaf_device_count(void)
{
    return sizeof(kDevices) / sizeof(kDevices[0]);
}

const qeaf_device_profile_t *qeaf_device_at(size_t index)
{
    return index < qeaf_device_count() ? kDevices[index] : NULL;
}

const qeaf_device_profile_t *qeaf_device_find(const char *id)
{
    if(id == NULL || id[0] == '\0') return NULL;
    for(size_t i = 0; i < qeaf_device_count(); i++) {
        const qeaf_device_profile_t *profile = kDevices[i];
        if(strcmp(id, profile->id) == 0 ||
           strcmp(id, profile->name) == 0 ||
           strcmp(id, "redmi-note-7-pro") == 0 ||
           strcmp(id, "redmi-note7-pro") == 0)
            return profile;
    }
    return NULL;
}
