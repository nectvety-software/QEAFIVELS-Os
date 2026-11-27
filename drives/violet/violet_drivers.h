#ifndef VIO_DRIVERS_H
#define VIO_DRIVERS_H

#include <stddef.h>
#include <stdint.h>

#include "vio_caps.h"
#include "vqeaf/capability.h"

#define VIO_DRIVERS_ABI 1u
#define VIO_DRIVERS_MAX 19u

typedef struct {
    uint32_t abi;
    uint32_t size;
    int drm_modeset_ok;
    int touch_open;
    int usb_gadget_bound;
    int power_input_open;
    int camera_frame_ok;
} VioDriverEvidence;

typedef struct {
    const char *name;
    const char *linux_interface;
    VqeafCapabilityState state;
    int32_t detail;
    const char *reason;
} VioDriverStatus;

void vio_drivers_evidence_init(VioDriverEvidence *evidence);
int vio_drivers_set_evidence(const VioDriverEvidence *evidence);
void vio_drivers_mark_camera_ready(int ready);

int vio_drivers_collect(const VioHwProbe *probe,
                        const VioDriverEvidence *evidence,
                        VioDriverStatus *out, size_t cap);
int vio_drivers_refresh(void);
size_t vio_drivers_count(void);
int vio_drivers_get(size_t index, VioDriverStatus *out);
int vio_drivers_render(const VioDriverStatus *items, size_t count,
                       char *buf, int cap);
int vio_drivers_report(char *buf, int cap);

#endif
