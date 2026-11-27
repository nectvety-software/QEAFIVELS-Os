/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_SOUND_H
#define VQEAF_SOUND_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    VQEAF_SOUND_NONE = -1,
    VQEAF_SOUND_TAP = 0,
    VQEAF_SOUND_DROP,
    VQEAF_SOUND_NAVIGATE,
    VQEAF_SOUND_NOTIFY,
    VQEAF_SOUND_CAPTURE,
    VQEAF_SOUND_RECORD,
    VQEAF_SOUND_WARNING,
    VQEAF_SOUND_CHARGE,
    VQEAF_SOUND_COUNT,
} vqeaf_sound_event_t;

typedef struct {
    const char *name;
    void *context;
    int (*open)(void *context);
    int (*write)(void *context, const int16_t *samples, size_t count);
    void (*close)(void *context);
} vqeaf_sound_backend_t;

void vqeaf_sound_set_backend(const vqeaf_sound_backend_t *backend);
int vqeaf_sound_init(void);
void vqeaf_sound_shutdown(void);
int vqeaf_sound_ready(void);
const char *vqeaf_sound_status(void);
int vqeaf_sound_play(vqeaf_sound_event_t event);
void vqeaf_sound_tick(void);

#endif
