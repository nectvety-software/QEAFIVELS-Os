/* SPDX-License-Identifier: MIT */
/* platform/violet/lv_conf.h - LVGL 9.4 for the VQEAF violet phone shell.
 *
 * The panel is owned by platform/violet/drm_kms.c and touch by
 * violet_input.c, so LVGL's own fbdev/DRM/evdev drivers stay off. The shell
 * renders a 720x1560 RGB565 canvas (1.5x fit of the 1080x2340 panel).
 */
#ifndef VQEAF_VIOLET_LV_CONF_H
#define VQEAF_VIOLET_LV_CONF_H

#include "../../config/lv_conf_common.h"

/* The shell allocates everything up front: screens, cards and the two
 * 720x1560 render buffers live outside this pool. 8 MB leaves room for
 * future app pages without starving the rescue thread. */
#undef LV_MEM_SIZE
#define LV_MEM_SIZE (8U * 1024U * 1024U)

#undef LV_DEF_REFR_PERIOD
#define LV_DEF_REFR_PERIOD 16

#undef LV_DPI_DEF
#define LV_DPI_DEF 200

/* XRGB8888 matches the panel scaler's native canvas and avoids a full-frame
 * RGB565 -> XRGB conversion on every dirty rectangle. */
#undef LV_COLOR_DEPTH
#define LV_COLOR_DEPTH 32

/* No stdio filesystem: the shell reads files through its own providers.
 * PNG (lodepng) and JPEG (tjpgd) decoders are compiled in so the Files viewer
 * can preview images from memory; audio/video/PDF stay metadata-only because
 * the initramfs ships no playback codec. All other art is LVGL widgets plus
 * the Ubuntu/Material font atlases compiled into the init binary. */
#undef LV_USE_FS_STDIO
#define LV_USE_FS_STDIO 0

#undef LV_USE_LODEPNG
#define LV_USE_LODEPNG 1

#undef LV_USE_TJPGD
#define LV_USE_TJPGD 1

#undef LV_USE_TINY_TTF
#define LV_USE_TINY_TTF 0

#undef LV_USE_SDL
#define LV_USE_SDL 0

#undef LV_USE_LINUX_FBDEV
#define LV_USE_LINUX_FBDEV 0

#undef LV_USE_LINUX_DRM
#define LV_USE_LINUX_DRM 0

#undef LV_USE_EVDEV
#define LV_USE_EVDEV 0

/* Keep the built-in default small; every widget in the shell selects a
 * Vietnamese Ubuntu face or the Material Icons face explicitly. */
#undef LV_FONT_MONTSERRAT_14
#define LV_FONT_MONTSERRAT_14 1
#undef LV_FONT_MONTSERRAT_12
#define LV_FONT_MONTSERRAT_12 0
#undef LV_FONT_MONTSERRAT_16
#define LV_FONT_MONTSERRAT_16 0
#undef LV_FONT_MONTSERRAT_18
#define LV_FONT_MONTSERRAT_18 0
#undef LV_FONT_MONTSERRAT_20
#define LV_FONT_MONTSERRAT_20 0
#undef LV_FONT_MONTSERRAT_22
#define LV_FONT_MONTSERRAT_22 0
#undef LV_FONT_MONTSERRAT_24
#define LV_FONT_MONTSERRAT_24 0
#undef LV_FONT_MONTSERRAT_26
#define LV_FONT_MONTSERRAT_26 0
#undef LV_FONT_MONTSERRAT_28
#define LV_FONT_MONTSERRAT_28 0
#undef LV_FONT_MONTSERRAT_30
#define LV_FONT_MONTSERRAT_30 0
#undef LV_FONT_MONTSERRAT_32
#define LV_FONT_MONTSERRAT_32 0
#undef LV_FONT_MONTSERRAT_34
#define LV_FONT_MONTSERRAT_34 0

/* Fonts are plain 8-bit coverage; the compressed reader is dead weight. */
#undef LV_USE_FONT_COMPRESSED
#define LV_USE_FONT_COMPRESSED 0

#endif /* VQEAF_VIOLET_LV_CONF_H */
