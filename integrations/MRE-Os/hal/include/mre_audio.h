#ifndef MRE_AUDIO_H
#define MRE_AUDIO_H
#include "mre_types.h"
typedef struct mre_audio_format { uint32_t sample_rate, channels, bits_per_sample; } mre_audio_format_t;
typedef struct mre_audio_ops {
    mre_status_t (*configure)(const mre_audio_format_t *format);
    mre_status_t (*write)(const void *samples, size_t bytes);
    mre_status_t (*set_gain)(uint32_t milli_db);
} mre_audio_ops_t;
#endif
