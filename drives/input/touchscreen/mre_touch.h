/* SPDX-License-Identifier: MIT */
/* mre_touch.h - touchscreen driver contract.
 *
 * The panel side (which bus, which controller, which IRQ) is the platform's
 * through mre_touch_ops_t: this driver only takes a frame of raw contacts,
 * applies the declared transform and turns it into queued input events.
 *
 * The transform is explicit rather than guessed - raw maximums, panel size,
 * axis swap and per-axis flip. A frame whose count exceeds
 * MRE_TOUCH_MAX_CONTACTS is rejected, and a full queue counts a drop instead
 * of overwriting an event nobody consumed yet.
 */
#ifndef MRE_TOUCH_H
#define MRE_TOUCH_H

#include <stdint.h>

#include "mre_input.h"
#include "mre_types.h"

#define MRE_TOUCH_MAX_CONTACTS 10u
#define MRE_TOUCH_QUEUE_CAPACITY 64u

typedef struct mre_touch_contact {
    uint16_t x, y;   /* raw controller coordinates */
    uint8_t id;      /* tracking id, passed through as the event code */
    uint8_t down;
} mre_touch_contact_t;

typedef struct mre_touch_frame {
    uint32_t count;
    mre_touch_contact_t contacts[MRE_TOUCH_MAX_CONTACTS];
} mre_touch_frame_t;

typedef struct mre_touch_ops {
    /* Reads one frame from the controller. Called only from
     * mre_touch_service(), i.e. after the IRQ handler reported work. */
    mre_status_t (*read_frame)(mre_touch_frame_t *frame);
} mre_touch_ops_t;

typedef struct mre_touch_transform {
    uint16_t raw_max_x, raw_max_y;
    uint16_t panel_width, panel_height;
    int swap_xy;
    int flip_x, flip_y;
} mre_touch_transform_t;

typedef struct mre_touch_device {
    mre_touch_ops_t ops;
    mre_touch_transform_t transform;
    mre_input_event_t queue[MRE_TOUCH_QUEUE_CAPACITY];
    uint32_t head, tail;
    uint32_t dropped;
    uint8_t irq_pending;
    uint64_t timestamp_ns;
} mre_touch_device_t;

/* Validates ops and transform (no zero raw maximum, no zero panel side) and
 * clears the device. No controller access. */
mre_status_t mre_touch_init(mre_touch_device_t *device,
                            const mre_touch_ops_t *ops,
                            const mre_touch_transform_t *transform);

/* IRQ half: marks work pending. Safe to call from an interrupt. */
void mre_touch_irq(mre_touch_device_t *device);

/* Task half: with work pending, reads one frame and queues one event per
 * contact. MRE_E_NOT_FOUND when there was nothing pending, the reader's
 * status when the frame could not be read. */
mre_status_t mre_touch_service(mre_touch_device_t *device, uint64_t timestamp_ns);

/* Input-poll half: pops the oldest queued event. */
mre_status_t mre_touch_poll(void *context, mre_input_event_t *event);

/* Registers and enables the controller IRQ through the platform GIC. Kept
 * separate so a host test can drive the IRQ half without a GIC. */
mre_status_t mre_touch_register_irq(mre_touch_device_t *device, uint32_t irq);

#endif
