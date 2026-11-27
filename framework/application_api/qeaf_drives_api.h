#ifndef VQEAF_DRIVES_API_H
#define VQEAF_DRIVES_API_H

/* framework/application_api/qeaf_drives_api.h - SDK view of the driver core.
 *
 * This is the read-only census an SDK tool or app uses to ask "which drivers
 * does this OS know, and in what state". It is a census, not a handle: no
 * pointer, no register, no instance id leaves the core, and there is no call
 * here that can change a driver's state. Binding, probing and removal stay
 * with the OS.
 *
 * The table is frozen at v1 and is not part of the QEAPP app ABI chain
 * (qeaf_api.h v2..v5): an app runtime hands it over only when it exposes the
 * import, so adding it never changes an existing module's layout.
 */
#include <stddef.h>
#include <stdint.h>

#define VQEAF_DRIVES_API_VERSION 1u

/* Bounded, pointer-free snapshot of one bound driver instance. `name` is the
 * driver name (a device-tree compatible for the drives/ set) and `state` is
 * the core's state name. Both are NUL terminated and truncated, never
 * rejected, so a caller with a small buffer still gets a usable record. */
#define QEAF_DRIVE_NAME_MAX 48u
#define QEAF_DRIVE_STATE_MAX 16u

typedef struct qeaf_drive_info {
    char name[QEAF_DRIVE_NAME_MAX];
    char state[QEAF_DRIVE_STATE_MAX];
    uint32_t device_class;
    int32_t last_error;
    uint32_t attempts;
} qeaf_drive_info_t;

typedef struct qeaf_drives_api_v1 {
    uint32_t size;
    uint32_t abi_version;
    /* Bound instances (READY, DEGRADED, FAILED, DEFERRED, PROBING); REMOVED
     * instances are history and are not counted. */
    uint32_t (*count)(void);
    /* 0 on success; -1 for a NULL out or an index past count(). */
    int (*info)(uint32_t index, qeaf_drive_info_t *out);
    /* Multi-line registry text. Returns bytes written (never cap or more) or
     * -1 on a bad buffer; the text is truncated rather than rejected. */
    int (*report)(char *buffer, uint32_t capacity);
} qeaf_drives_api_v1;

/* The v1 table, or NULL when the OS build has no driver core linked in.
 * Never NULL on a build that links system/core/driver.c. */
const qeaf_drives_api_v1 *qeaf_drives_api(void);

#endif
