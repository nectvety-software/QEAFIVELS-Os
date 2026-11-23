#ifndef MRE_DISPLAY_H
#define MRE_DISPLAY_H
#include "mre_types.h"
typedef struct mre_display_info { uint32_t width, height, stride_bytes, pixel_format; } mre_display_info_t;
typedef struct mre_display_ops {
    mre_status_t (*get_info)(mre_display_info_t *out);
    mre_status_t (*present)(const void *pixels, size_t bytes);
    mre_status_t (*set_backlight)(uint32_t level);
} mre_display_ops_t;
#endif
