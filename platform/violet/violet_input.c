/* SPDX-License-Identifier: MIT */
/* platform/violet/violet_input.c - evdev touch reader.
 *
 * The FTS / NT36xxx drivers report ABS_MT_POSITION_X/Y plus BTN_TOUCH and
 * ABS_MT_TRACKING_ID. Slot 0 is the pointer; extra fingers are ignored so a
 * two-finger gesture cannot make the cursor jump between slots.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "violet_input.h"

#include "violet.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/input.h>

static int axis_present(const unsigned char *bits, int axis)
{
    return (bits[axis / 8] >> (axis % 8)) & 1;
}

static void read_range(int fd, int axis, int *minimum, int *maximum)
{
    struct input_absinfo info;
    if(ioctl(fd, EVIOCGABS(axis), &info) == 0 && info.maximum > info.minimum) {
        *minimum = info.minimum;
        *maximum = info.maximum;
    }
}

int vio_touch_open(struct vio_touch *touch, const char *path, vio_panel_t *panel)
{
    unsigned char abs_bits[ABS_CNT / 8 + 1];

    memset(touch, 0, sizeof(*touch));
    for(int i = 0; i < VIO_TOUCH_MAX_SLOTS; i++) touch->mt_id[i] = -1;
    touch->fd = -1;
    touch->panel = panel;
    touch->min_x = 0;
    touch->max_x = panel->panel_w - 1;
    touch->min_y = 0;
    touch->max_y = panel->panel_h - 1;

    if(path == NULL) return -1;
    touch->fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if(touch->fd < 0) {
        vio_log("touch: cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    memset(abs_bits, 0, sizeof(abs_bits));
    if(ioctl(touch->fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) >= 0) {
        if(axis_present(abs_bits, ABS_MT_POSITION_X) &&
           axis_present(abs_bits, ABS_MT_POSITION_Y)) {
            touch->use_mt = 1;
            read_range(touch->fd, ABS_MT_POSITION_X, &touch->min_x, &touch->max_x);
            read_range(touch->fd, ABS_MT_POSITION_Y, &touch->min_y, &touch->max_y);
        }
        else if(axis_present(abs_bits, ABS_X) && axis_present(abs_bits, ABS_Y)) {
            touch->use_mt = 0;
            read_range(touch->fd, ABS_X, &touch->min_x, &touch->max_x);
            read_range(touch->fd, ABS_Y, &touch->min_y, &touch->max_y);
        }
    }
    vio_log("touch: %s x[%d..%d] y[%d..%d] %s", path,
            touch->min_x, touch->max_x, touch->min_y, touch->max_y,
            touch->use_mt ? "multitouch" : "single-touch");
    return 0;
}

void vio_touch_close(struct vio_touch *touch)
{
    if(touch->fd >= 0) close(touch->fd);
    touch->fd = -1;
}

static void commit(struct vio_touch *touch)
{
    if(touch->pending_pressed && touch->have_x && touch->have_y) {
        int ui_x = 0, ui_y = 0;
        int raw_x = touch->pending_x - touch->min_x;
        int raw_y = touch->pending_y - touch->min_y;
        int raw_w = touch->max_x - touch->min_x + 1;
        int raw_h = touch->max_y - touch->min_y + 1;
        if(vio_panel_touch_map(touch->panel, raw_x, raw_y, raw_w, raw_h,
                               &ui_x, &ui_y) == 0) {
            touch->x = ui_x;
            touch->y = ui_y;
            touch->pressed = 1;
        }
        else {
            /* A tap in the letterbox is not a tap on the desktop. */
            touch->pressed = 0;
        }
    }
    else if(!touch->pending_pressed) {
        touch->pressed = 0;
    }
}

int vio_touch_inject(struct vio_touch *touch, int ui_x, int ui_y)
{
    if(touch == NULL) return -1;
    return vio_tap_queue(&touch->tap, ui_x, ui_y);
}

int vio_touch_hold(struct vio_touch *touch, int ui_x, int ui_y, int ms)
{
    if(touch == NULL) return -1;
    return vio_hold_queue(&touch->tap, ui_x, ui_y, ms);
}

int vio_touch_swipe(struct vio_touch *touch, int x0, int y0, int x1, int y1,
                    int steps)
{
    if(touch == NULL) return -1;
    return vio_swipe_queue(&touch->tap, x0, y0, x1, y1, steps);
}

static void three_finger_report(struct vio_touch *touch);

int vio_touch_take_three_finger(struct vio_touch *touch)
{
    if(touch == NULL || !touch->three_finger_swipe) return 0;
    touch->three_finger_swipe = 0;
    return 1;
}

int vio_touch_inject_three(struct vio_touch *touch, int dx, int dy)
{
    if(touch == NULL) return -1;
    if(dx < -400) dx = -400;
    if(dx > 400) dx = 400;
    if(dy < -400) dy = -400;
    if(dy > 400) dy = 400;
    for(int i = 0; i < 3; i++) {
        touch->mt_id[i] = 100 + i;
        touch->mt_x[i] = 200;
        touch->mt_y[i] = 300 + i * 30;
    }
    three_finger_report(touch);
    for(int i = 0; i < 3; i++) {
        touch->mt_x[i] += dx;
        touch->mt_y[i] += dy;
    }
    three_finger_report(touch);
    for(int i = 0; i < 3; i++) touch->mt_id[i] = -1;
    three_finger_report(touch);
    return 0;
}

/* Called on SYN_REPORT with the latest per-slot data. A three-finger gesture
 * is active while at least three contacts are down; it fires when they are
 * released after moving more than a panel-relative threshold. */
static void three_finger_report(struct vio_touch *touch)
{
    int active = 0, sx = 0, sy = 0, i;

    for(i = 0; i < VIO_TOUCH_MAX_SLOTS; i++) {
        if(touch->mt_id[i] < 0) continue;
        active++;
        sx += touch->mt_x[i];
        sy += touch->mt_y[i];
    }
    touch->active_count = active;

    if(active >= 3) {
        int cx = sx / active, cy = sy / active;
        int dx, dy;
        if(!touch->three_active) {
            touch->three_active = 1;
            touch->three_x0 = touch->three_last_x = cx;
            touch->three_y0 = touch->three_last_y = cy;
            return;
        }
        touch->three_last_x = cx;
        touch->three_last_y = cy;
        dx = cx - touch->three_x0;
        dy = cy - touch->three_y0;
        if(dx < 0) dx = -dx;
        if(dy < 0) dy = -dy;
        if(dx > 60 || dy > 60) touch->three_finger_swipe = 1;
        return;
    }

    if(touch->three_active) {
        int dx = touch->three_last_x - touch->three_x0;
        int dy = touch->three_last_y - touch->three_y0;
        if(dx < 0) dx = -dx;
        if(dy < 0) dy = -dy;
        if(dx > 60 || dy > 60) touch->three_finger_swipe = 1;
        touch->three_active = 0;
    }
}

/* Presents one synthetic contact step: press, then one move per poll along the
 * drag, then release. Returns 1 while the injection owns this poll, so the
 * real evdev queue is drained on the next one. */
static int present_injected(struct vio_touch *touch)
{
    int x = 0;
    int y = 0;
    int state;

    if(touch->swipe_active) {
        int t;

        if(touch->swipe_step > touch->swipe_steps) {
            touch->swipe_active = 0;
            touch->pressed = 0;
            return 1;
        }
        t = touch->swipe_step;
        touch->x = touch->swipe_x0 +
                   (int)(((long long)(touch->swipe_x1 - touch->swipe_x0) * t) /
                         touch->swipe_steps);
        touch->y = touch->swipe_y0 +
                   (int)(((long long)(touch->swipe_y1 - touch->swipe_y0) * t) /
                         touch->swipe_steps);
        touch->pressed = 1;
        touch->swipe_step++;
        return 1;
    }

    state = vio_tap_present(&touch->tap, &x, &y);
    if(state > 0) {
        touch->x = x;
        touch->y = y;
        touch->pressed = 1;
        return 1;
    }
    if(state < 0) {
        touch->pressed = 0;
        return 1;   /* keep this poll for the release */
    }

    if(vio_swipe_take(&touch->tap, &touch->swipe_x0, &touch->swipe_y0,
                      &touch->swipe_x1, &touch->swipe_y1,
                      &touch->swipe_steps)) {
        touch->swipe_step = 1;
        touch->swipe_active = 1;
        touch->x = touch->swipe_x0;
        touch->y = touch->swipe_y0;
        touch->pressed = 1;
        return 1;
    }
    return 0;
}

void vio_touch_poll(struct vio_touch *touch)
{
    struct input_event events[32];
    ssize_t count;

    if(touch == NULL) return;
    /* Injected taps work even when no evdev node was found, which is what
     * lets a host operate the shell on an unproven touch driver. */
    if(present_injected(touch)) return;
    if(touch->fd < 0) return;
    while((count = read(touch->fd, events, sizeof(events))) > 0) {
        size_t n = (size_t)count / sizeof(events[0]);
        for(size_t i = 0; i < n; i++) {
            const struct input_event *ev = &events[i];
            if(ev->type == EV_ABS) {
                if(ev->code == ABS_MT_SLOT) {
                    touch->slot = ev->value;
                    if(touch->slot < 0) touch->slot = 0;
                    if(touch->slot >= VIO_TOUCH_MAX_SLOTS)
                        touch->slot = VIO_TOUCH_MAX_SLOTS - 1;
                }
                else if(touch->use_mt && ev->code == ABS_MT_POSITION_X) {
                    touch->mt_x[touch->slot] = ev->value;
                    if(touch->slot == 0) {
                        touch->pending_x = ev->value;
                        touch->have_x = 1;
                    }
                }
                else if(touch->use_mt && ev->code == ABS_MT_POSITION_Y) {
                    touch->mt_y[touch->slot] = ev->value;
                    if(touch->slot == 0) {
                        touch->pending_y = ev->value;
                        touch->have_y = 1;
                    }
                }
                else if(touch->use_mt && ev->code == ABS_MT_TRACKING_ID) {
                    touch->mt_id[touch->slot] = ev->value;
                    if(touch->slot == 0) touch->pending_pressed = ev->value >= 0;
                }
                else if(ev->code == ABS_MT_POSITION_X ||
                        (ev->code == ABS_X && !touch->use_mt)) {
                    touch->pending_x = ev->value;
                    touch->have_x = 1;
                }
                else if(ev->code == ABS_MT_POSITION_Y ||
                        (ev->code == ABS_Y && !touch->use_mt)) {
                    touch->pending_y = ev->value;
                    touch->have_y = 1;
                }
                else if(ev->code == ABS_MT_TRACKING_ID) {
                    touch->pending_pressed = ev->value >= 0;
                }
            }
            else if(ev->type == EV_KEY && ev->code == BTN_TOUCH) {
                touch->pending_pressed = ev->value != 0;
            }
            else if(ev->type == EV_SYN && ev->code == SYN_REPORT) {
                if(touch->use_mt) three_finger_report(touch);
                /* A three-finger gesture is not a single-pointer drag. */
                if(touch->active_count >= 3) touch->pending_pressed = 0;
                commit(touch);
            }
        }
    }
}
