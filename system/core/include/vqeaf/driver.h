/* SPDX-License-Identifier: MIT */
/* system/core/include/vqeaf/driver.h - QEAFIVELS driver core.
 *
 * A driver is a name, a device class and a probe/remove pair; the core owns
 * registration, instance bookkeeping and the state machine. The original v1
 * contract still holds: binding is by driver name, probe() returning 0 is the
 * only path to READY, and a failing probe is remembered as FAILED with its
 * first error.
 *
 * What the upgrade adds, all of it optional for the drivers already written:
 *   - VqeafDriver.match lets the core pick a driver for a device instead of
 *     binding whatever happens to share a discovered name.
 *   - VQEAF_DRIVER_DEFER lets probe() say "a clock, a firmware blob or an IRQ
 *     backend is not here yet" without claiming the hardware is broken. The
 *     instance rests in VQEAF_DRV_DEFERRED and
 *     vqeaf_driver_retry_deferred() re-probes it later. A missing resource is
 *     not a failure and must not be recorded as one.
 *   - bind() refuses to attach the same device to the same driver twice, so a
 *     bus rescan cannot multiply instances.
 *   - unbind() only calls remove() for an instance that actually probed, so a
 *     failed probe is never "removed".
 *   - the registry is readable: count, find, state names and a bounded report
 *     the diagnostic verbs can print.
 *
 * Fail-closed: nothing here invents a device. A driver without probe() is
 * refused, an unknown name never binds, DEFERRED is not READY, and this core
 * never reports a capability by itself.
 */
#ifndef VQEAF_DRIVER_H
#define VQEAF_DRIVER_H

#include <stdint.h>

#define VQEAF_DRIVER_ABI 1u
#define VQEAF_DRIVER_MAX 32u

/* Probe result meaning 0 = bound, negative = failure, and this one positive
 * value = "not yet". Kept out of the errno range on purpose: a deferral is not
 * an error and must not be stored as one. */
#define VQEAF_DRIVER_DEFER 1

typedef enum {
    VQEAF_DEV_UNKNOWN = 0,
    VQEAF_DEV_STORAGE,
    VQEAF_DEV_DISPLAY,
    VQEAF_DEV_INPUT,
    VQEAF_DEV_AUDIO,
    VQEAF_DEV_POWER,
    VQEAF_DEV_RADIO,
    VQEAF_DEV_SENSOR
} VqeafDeviceClass;

/* DEFERRED is appended so the v1 numeric values stay what they were. */
typedef enum {
    VQEAF_DRV_REGISTERED = 1,
    VQEAF_DRV_PROBING,
    VQEAF_DRV_READY,
    VQEAF_DRV_DEGRADED,
    VQEAF_DRV_FAILED,
    VQEAF_DRV_REMOVED,
    VQEAF_DRV_DEFERRED
} VqeafDriverState;

/* `match` is appended after remove and may stay NULL; a driver without it
 * keeps the v1 behaviour (bind by name). It returns 1 to claim the device, 0
 * to decline, and any negative value is treated as a decline. */
typedef struct {
    uint32_t abi, size;
    const char *name;
    VqeafDeviceClass device_class;
    int (*probe)(void *device);
    int (*remove)(void *device);
    int (*match)(void *device);
} VqeafDriver;

typedef struct {
    const VqeafDriver *driver;
    void *device;
    VqeafDriverState state;
    int last_error;
    uint32_t attempts;      /* probe() calls, including deferrals */
} VqeafDriverInstance;

/* 0 registered, -1 invalid descriptor, -2 duplicate name, -3 table full. */
int vqeaf_driver_register(const VqeafDriver *driver);

/* Probe and record one instance. Returns the probe result: 0 READY, a negative
 * errno FAILED, or VQEAF_DRIVER_DEFER (1) DEFERRED. -1 invalid argument, -2 no
 * such driver, -3 table full, -4 this device is already bound to it. */
int vqeaf_driver_bind(const char *name, void *device, uint32_t *instance_id);

/* Same, choosing the first registered driver whose match() claims `device`.
 * -5 when no registered driver claims it. */
int vqeaf_driver_bind_match(void *device, uint32_t *instance_id);

/* Calls remove() only for an instance that probed, then marks it REMOVED.
 * -1 for an unknown or already removed instance. */
int vqeaf_driver_unbind(uint32_t instance_id);

int vqeaf_driver_get(uint32_t instance_id, VqeafDriverInstance *out);

/* Re-probes every DEFERRED instance once. Returns how many became READY. */
int vqeaf_driver_retry_deferred(void);

/* First live instance of `name`: 0 found, -1 invalid argument, -2 none. */
int vqeaf_driver_find(const char *name, uint32_t *instance_id);

uint32_t vqeaf_driver_count(void);
uint32_t vqeaf_driver_instance_count(void);

/* "registered", "probing", "ready", "degraded", "failed", "removed",
 * "deferred"; "?" for an unknown value. */
const char *vqeaf_driver_state_name(VqeafDriverState state);

/* Multi-line registry listing, NUL terminated and truncated at cap. Returns
 * the bytes written (never cap or more), or -1 on a bad buffer. */
int vqeaf_driver_report(char *out, int cap);

void vqeaf_driver_reset(void);

#endif
