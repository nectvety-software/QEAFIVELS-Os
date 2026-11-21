/* Shared drawing kit for the VQEAF native screens: palette, responsive
 * phone-style content column, brand badge, icon set and menu rows.
 * Freestanding and allocation-free so the UEFI apps and the kernel link the
 * same renderer. */
#ifndef VQEAF_SCREEN_COMMON_H
#define VQEAF_SCREEN_COMMON_H
#include "gfx.h"

typedef struct { uint32_t x, w, scale; } VqeafScreenFrame;

typedef struct {
    uint32_t bg, bar, panel, panel_hi, accent, accent_dim;
    uint32_t text, dim, track, danger;
} VqeafPalette;

typedef enum {
    VQEAF_ICON_NONE = 0,
    VQEAF_ICON_PLAY,
    VQEAF_ICON_REBOOT,
    VQEAF_ICON_DOWNLOAD,
    VQEAF_ICON_TRASH,
    VQEAF_ICON_GEAR,
    VQEAF_ICON_POWER,
    VQEAF_ICON_INFO,
    VQEAF_ICON_CHECK,
    VQEAF_ICON_CLOCK,
    VQEAF_ICON_LOCK,
    VQEAF_ICON_WIFI,
    VQEAF_ICON_BLUETOOTH,
    VQEAF_ICON_STORAGE,
    VQEAF_ICON_HOME,
    VQEAF_ICON_BACK,
    /* Material Icons additions used by the phone shell. */
    VQEAF_ICON_TIMER,
    VQEAF_ICON_KEEP_AWAKE,
    VQEAF_ICON_DISPLAY,
    VQEAF_ICON_UPDATE,
    VQEAF_ICON_BOLT,
    VQEAF_ICON_SLEEP
} VqeafIcon;

VqeafScreenFrame vqeaf_screen_frame(const VqeafFramebuffer *fb);
VqeafPalette vqeaf_screen_palette(const VqeafFramebuffer *fb);
uint32_t vqeaf_screen_row_height(const VqeafScreenFrame *f);
void vqeaf_screen_header(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                         const VqeafPalette *p, const char *mode);
void vqeaf_screen_waves(const VqeafFramebuffer *fb, uint32_t baseline,
                        const VqeafPalette *p);
void vqeaf_screen_waves_region(const VqeafFramebuffer *fb, uint32_t x0,
                               uint32_t w, uint32_t baseline,
                               const VqeafPalette *p);
void vqeaf_screen_badge(const VqeafFramebuffer *fb, uint32_t cx, uint32_t cy,
                        uint32_t radius, int rings, const VqeafPalette *p);
void vqeaf_screen_icon(const VqeafFramebuffer *fb, uint32_t x, uint32_t y,
                       uint32_t size, VqeafIcon icon, uint32_t color);
/* One selectable row. `status` may be NULL. Returns the row height used. */
uint32_t vqeaf_screen_menu_row(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                               const VqeafPalette *p, uint32_t y, VqeafIcon icon,
                               const char *label, const char *status,
                               int selected, int enabled);
uint32_t vqeaf_screen_menu_row_h(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                                 const VqeafPalette *p, uint32_t y, uint32_t row_h,
                                 VqeafIcon icon, const char *label, const char *status,
                                 int selected, int enabled);
#endif
