#ifndef HOMO_VIOLET_POWER_H
#define HOMO_VIOLET_POWER_H

#include <stdint.h>

#define VIO_POWER_FDS 4

struct vio_power {
    int fds[VIO_POWER_FDS];
    int nfd;
    int pressed;
    uint64_t pressed_since_ms;
    int menu_shown;
    int pressed_ever;
    int short_press;
    int volume;
    int volume_delta;
    int vol_key_pressed;      /* +1 up, -1 down, 0 none */
    uint64_t vol_pressed_since_ms;
    uint64_t vol_last_repeat_ms;
    int combo_active;         /* power held together with a volume key */
    int screenshot;           /* power+volume gesture seen by the caller */
};

int vio_power_open(struct vio_power *pw, const char *path);
int vio_power_open_more(struct vio_power *pw, const char *path);
const char *vio_find_power_device(char *buf, int cap);
int vio_power_poll(struct vio_power *pw);
int vio_power_volume(struct vio_power *pw);

#endif
