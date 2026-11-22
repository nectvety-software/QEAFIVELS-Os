/* SPDX-License-Identifier: MIT */
/* platform/violet/violet_input.h - evdev multitouch for the violet panel. */
#ifndef HOMO_VIOLET_INPUT_H
#define HOMO_VIOLET_INPUT_H

#include "panel_scale.h"
#include "violet_tap.h"

struct vio_touch {
    int fd;
    int use_mt;
    int min_x, max_x, min_y, max_y;
    int slot;
    int pending_x, pending_y;
    int have_x, have_y;
    int pending_pressed;
    int pressed;
    int x, y;
    vio_panel_t *panel;
    /* Rescue-channel tap injection; see violet_tap.h. */
    vio_tap_state_t tap;
    /* UI-thread side of a queued drag. */
    int swipe_active;
    int swipe_step;
    int swipe_steps;
    int swipe_x0, swipe_y0, swipe_x1, swipe_y1;
    /* Three-finger swipe -> screenshot. Raw MT slots are tracked separately
     * from the single pointer used by LVGL. */
#define VIO_TOUCH_MAX_SLOTS 5
    int mt_id[VIO_TOUCH_MAX_SLOTS];
    int mt_x[VIO_TOUCH_MAX_SLOTS], mt_y[VIO_TOUCH_MAX_SLOTS];
    int active_count;
    int three_active;
    int three_x0, three_y0, three_last_x, three_last_y;
    int three_finger_swipe;
};

int vio_touch_open(struct vio_touch *touch, const char *path, vio_panel_t *panel);
void vio_touch_close(struct vio_touch *touch);
void vio_touch_poll(struct vio_touch *touch);
/* Queue a synthetic click at UI-canvas coordinates (not raw panel axes).
 * Safe to call from another thread; the UI thread presents the press and
 * release through vio_touch_poll(). Returns 0 when queued, -1 when refused. */
int vio_touch_inject(struct vio_touch *touch, int ui_x, int ui_y);
/* Queue a synthetic long press: the same point stays down for `ms`
 * (100..10000) so LVGL's long-press timer sees a real hold, then releases. */
int vio_touch_hold(struct vio_touch *touch, int ui_x, int ui_y, int ms);
/* Queue a synthetic drag: press at (x0,y0), `steps` moves to (x1,y1), release.
 * Steps 2..64. Same threading and one-slot rules as vio_touch_inject(). */
int vio_touch_swipe(struct vio_touch *touch, int x0, int y0, int x1, int y1,
                    int steps);
/* Returns 1 once when a completed three-finger swipe was seen, clearing it. */
int vio_touch_take_three_finger(struct vio_touch *touch);
/* Host-testable synthetic three-finger swipe: press three contacts, move by
 * (dx,dy) raw units, release. Uses the exact same detector as real evdev. */
int vio_touch_inject_three(struct vio_touch *touch, int dx, int dy);

#endif /* HOMO_VIOLET_INPUT_H */
