/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_SOUND_DATA_H
#define VQEAF_SOUND_DATA_H

#include "vqeaf_sound.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const int16_t *samples;
    size_t count;
} vqeaf_sound_clip_t;

extern const int16_t vqeaf_sound_tap[5028];
extern const int16_t vqeaf_sound_drop[10538];
extern const int16_t vqeaf_sound_navigate[10532];
extern const int16_t vqeaf_sound_notify[16034];
extern const int16_t vqeaf_sound_capture[7778];
extern const int16_t vqeaf_sound_record[16067];
extern const int16_t vqeaf_sound_warning[21522];
extern const int16_t vqeaf_sound_charge[21561];
const vqeaf_sound_clip_t *vqeaf_sound_clip(vqeaf_sound_event_t event);

#endif
