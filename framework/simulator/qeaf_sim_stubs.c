/* SPDX-License-Identifier: MIT */
/* framework/simulator/qeaf_sim_stubs.c - host stubs for device-only services.
 *
 * The simulator links vqeaf_qeapp.c (the real device installer) without the
 * p77 appstore backend, which lives behind FAT volumes and flash devices the
 * host does not have. These stubs report honest failure: persistence is
 * simply unavailable in the simulator, so installs stay session-only and
 * nothing pretends to have written a catalog.
 */
#include "appstore.h"

#include <stddef.h>
#include <stdint.h>

int appstore_save(const appstore_entry_t *entries, int count)
{
    (void)entries;
    (void)count;
    return -1;
}

int appstore_restore(char names[][APPSTORE_NAME], int max)
{
    (void)names;
    (void)max;
    return 0;
}

int appstore_read(const char *name, uint8_t **out, size_t *size)
{
    (void)name;
    (void)out;
    (void)size;
    return -1;
}

int appstore_read_data(const char *name, uint8_t **out, size_t *size)
{
    (void)name;
    (void)out;
    (void)size;
    return -1;
}

int appstore_read_grants(const char *name, uint32_t *grants)
{
    (void)name;
    if(grants) *grants = 0;
    return -1;
}

int appstore_volume_has(const char *name)
{
    (void)name;
    return 0;
}

void appstore_set_restoring(int restoring)
{
    (void)restoring;
}

int appstore_is_restoring(void)
{
    return 0;
}

#include "core/log.h"

void umos_logf(const char *fmt, ...)
{
    /* Simulator host log: keep quiet. */
    (void)fmt;
}
