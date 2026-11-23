#ifndef MRE_UI_H
#define MRE_UI_H
#include <stddef.h>
#include <stdint.h>
#include "mre_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MRE_UI_APP_COUNT 4u
typedef enum mre_ui_action {
 MRE_UI_ACTION_NONE=0, MRE_UI_ACTION_PHONE=1, MRE_UI_ACTION_MESSAGES=2,
 MRE_UI_ACTION_FILES=3, MRE_UI_ACTION_SETTINGS=4,
 MRE_UI_ACTION_QUICK_SETTINGS=5, MRE_UI_ACTION_NOTIFICATIONS=6
} mre_ui_action_t;
typedef struct mre_ui_state {
 const char *clock_text; const char *date_text; const char *status_text;
 uint8_t battery_percent, wifi_connected, cellular_available, charging;
} mre_ui_state_t;
/* UTF-8 decoding and glyph rasterization belong to the selected font service. */
typedef void (*mre_ui_draw_text_fn)(void *context, uint16_t *framebuffer,
 uint32_t width, uint32_t height, uint32_t stride_pixels,
 int32_t x, int32_t y, uint32_t pixel_scale, uint16_t color_rgb565,
 const char *utf8);
typedef struct mre_ui_font_ops { mre_ui_draw_text_fn draw_text; void *context; } mre_ui_font_ops_t;
typedef struct mre_ui_rect { int32_t x,y,width,height; } mre_ui_rect_t;
mre_status_t mre_ui_render_home(uint16_t *framebuffer,uint32_t width,uint32_t height,
 uint32_t stride_pixels,const mre_ui_state_t *state,const mre_ui_font_ops_t *font);
mre_ui_action_t mre_ui_hit_test(uint32_t width,uint32_t height,int32_t x,int32_t y);
#ifdef __cplusplus
}
#endif
#endif
