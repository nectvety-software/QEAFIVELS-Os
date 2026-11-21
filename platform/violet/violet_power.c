/* platform/violet/violet_power.c - KEY_POWER + KEY_VOLUMEUP/DOWN. */
#define _GNU_SOURCE
#include "violet_power.h"
#include "violet.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int vio_power_open(struct vio_power *pw, const char *path) {
    memset(pw, 0, sizeof(*pw));
    for (int i = 0; i < VIO_POWER_FDS; i++) pw->fds[i] = -1;
    pw->nfd = 0;
    pw->volume = 25;
    if (!path) return -1;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        vio_log("power: cannot open %s: %s", path, strerror(errno));
        return -1;
    }
    pw->fds[0] = fd;
    pw->nfd = 1;
    return 0;
}

int vio_power_open_more(struct vio_power *pw, const char *path);

int vio_power_open_more(struct vio_power *pw, const char *path) {
    if (!pw || pw->nfd >= VIO_POWER_FDS) return -1;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    pw->fds[pw->nfd++] = fd;
    return 0;
}

const char *vio_find_power_device(char *buf, int cap) {
    (void)buf; (void)cap;
    return buf;
}

int vio_power_poll(struct vio_power *pw) {
    struct input_event ev;
    ssize_t n;
    pw->volume_delta = 0;
    if (!pw || pw->nfd <= 0) return 0;
    for (int k = 0; k < pw->nfd; k++) {
        int fd = pw->fds[k];
        if (fd < 0) continue;
        while ((n = read(fd, &ev, sizeof(ev))) > 0) {
            if (ev.type == EV_KEY && ev.code == KEY_POWER) {
                if (ev.value == 1) {
                    if (pw->vol_key_pressed != 0) {
                        /* Power + Volume is the screenshot shortcut; it must
                         * not also open the power menu on release. */
                        pw->screenshot = 1;
                        pw->combo_active = 1;
                    } else {
                        pw->pressed = 1;
                        pw->pressed_ever = 1;
                        pw->pressed_since_ms = vio_monotonic_ms();
                        pw->menu_shown = 0;
                    }
                } else if (ev.value == 0) {
                    if (pw->combo_active) {
                        pw->combo_active = 0;
                    } else {
                        if (pw->pressed && !pw->menu_shown)
                            pw->short_press = 1;
                        pw->pressed = 0;
                    }
                }
            } else if (ev.type == EV_KEY && (ev.code == KEY_VOLUMEUP || ev.code == KEY_VOLUMEDOWN)) {
                int d = (ev.code == KEY_VOLUMEUP) ? 10 : -10;
                if (ev.value == 1) {
                    if (pw->pressed) {
                        pw->screenshot = 1;
                        pw->combo_active = 1;
                        pw->pressed = 0;
                    }
                    pw->vol_key_pressed = (ev.code == KEY_VOLUMEUP) ? 1 : -1;
                    pw->vol_pressed_since_ms = vio_monotonic_ms();
                    pw->vol_last_repeat_ms = pw->vol_pressed_since_ms;
                    pw->volume += d;
                    if (pw->volume < 0) pw->volume = 0;
                    if (pw->volume > 100) pw->volume = 100;
                    pw->volume_delta += d;
                } else if (ev.value == 0) {
                    pw->vol_key_pressed = 0;
                }
            }
        }
    }
    if (pw->vol_key_pressed != 0) {
        uint64_t now = vio_monotonic_ms();
        uint64_t held = now - pw->vol_pressed_since_ms;
        if (held >= 400 && now - pw->vol_last_repeat_ms >= 150) {
            int d = pw->vol_key_pressed > 0 ? 10 : -10;
            pw->volume += d;
            if (pw->volume < 0) pw->volume = 0;
            if (pw->volume > 100) pw->volume = 100;
            pw->volume_delta += d;
            pw->vol_last_repeat_ms = now;
        }
    }

    if (pw->short_press) {
        pw->short_press = 0;
        return 2;
    }
    if (pw->pressed && !pw->menu_shown && pw->pressed_ever) {
        uint64_t el = vio_monotonic_ms() - pw->pressed_since_ms;
        if (el >= 1000) {
            pw->menu_shown = 1;
            return 1;
        }
    }
    return 0;
}

int vio_power_volume(struct vio_power *pw) {
    return pw ? pw->volume : 0;
}
