/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_theme.h - VQEAF OS phone shell look and feel.
 *
 * The palette and soft vector wallpaper use native shapes and gradients. The
 * OS background has no LVGL image descriptor, bitmap asset, or image decoder.
 */
#ifndef VQEAF_THEME_H
#define VQEAF_THEME_H

#include "lvgl.h"

#include "vqeaf_fonts.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VQEAF_SCREEN_W 720
#define VQEAF_SCREEN_H 1560
#define VQEAF_STATUS_H 56
#define VQEAF_PAD 24

/* --- palette -------------------------------------------------------------- */
/* A runtime palette: every VQEAF_C_* name now resolves to a field, so the
 * whole shell can switch dark/light without touching 300+ call sites. Styles
 * are re-applied by rebuilding the shell chrome in vqeaf_ui_set_theme(). */
typedef struct {
    lv_color_t bg, bg_deep, surface, surface_2, surface_3, line;
    lv_color_t accent, accent_dk, cyan, green;
    lv_color_t text, text_dim, text_mute, danger, warn;
    /* Material Design 3 roles. They are derived from the same seed as the
     * shell palette, so a dynamic-colour change re-tints the components too. */
    lv_color_t primary, on_primary;
    lv_color_t primary_container, on_primary_container;
    lv_color_t secondary_container, on_secondary_container;
    lv_color_t surface_container_low, surface_container, surface_container_high,
               surface_container_highest;
    lv_color_t outline, outline_variant;
    lv_color_t error, on_error, error_container, on_error_container;
    lv_color_t inverse_surface, inverse_on_surface;
} vqeaf_palette_t;

extern vqeaf_palette_t vqeaf_palette;

#define VQEAF_C_BG          (vqeaf_palette.bg)
#define VQEAF_C_BG_DEEP     (vqeaf_palette.bg_deep)
#define VQEAF_C_SURFACE     (vqeaf_palette.surface)
#define VQEAF_C_SURFACE_2   (vqeaf_palette.surface_2)
#define VQEAF_C_SURFACE_3   (vqeaf_palette.surface_3)
#define VQEAF_C_LINE        (vqeaf_palette.line)
#define VQEAF_C_ACCENT      (vqeaf_palette.accent)
#define VQEAF_C_ACCENT_DK   (vqeaf_palette.accent_dk)
#define VQEAF_C_CYAN        (vqeaf_palette.cyan)
#define VQEAF_C_GREEN       (vqeaf_palette.green)
#define VQEAF_C_TEXT        (vqeaf_palette.text)
#define VQEAF_C_TEXT_DIM    (vqeaf_palette.text_dim)
#define VQEAF_C_TEXT_MUTE   (vqeaf_palette.text_mute)
#define VQEAF_C_DANGER      (vqeaf_palette.danger)
#define VQEAF_C_WARN        (vqeaf_palette.warn)

/* Material Design 3 colour roles (Material 3 vocabulary). */
#define VQEAF_C_PRIMARY                   (vqeaf_palette.primary)
#define VQEAF_C_ON_PRIMARY                (vqeaf_palette.on_primary)
#define VQEAF_C_PRIMARY_CONTAINER         (vqeaf_palette.primary_container)
#define VQEAF_C_ON_PRIMARY_CONTAINER      (vqeaf_palette.on_primary_container)
#define VQEAF_C_SECONDARY_CONTAINER       (vqeaf_palette.secondary_container)
#define VQEAF_C_ON_SECONDARY_CONTAINER    (vqeaf_palette.on_secondary_container)
#define VQEAF_C_SURFACE_CONTAINER_LOW     (vqeaf_palette.surface_container_low)
#define VQEAF_C_SURFACE_CONTAINER         (vqeaf_palette.surface_container)
#define VQEAF_C_SURFACE_CONTAINER_HIGH    (vqeaf_palette.surface_container_high)
#define VQEAF_C_SURFACE_CONTAINER_HIGHEST (vqeaf_palette.surface_container_highest)
#define VQEAF_C_OUTLINE                   (vqeaf_palette.outline)
#define VQEAF_C_OUTLINE_VARIANT           (vqeaf_palette.outline_variant)
#define VQEAF_C_ERROR                     (vqeaf_palette.error)
#define VQEAF_C_ON_ERROR                  (vqeaf_palette.on_error)
#define VQEAF_C_ERROR_CONTAINER           (vqeaf_palette.error_container)
#define VQEAF_C_ON_ERROR_CONTAINER        (vqeaf_palette.on_error_container)
#define VQEAF_C_INVERSE_SURFACE           (vqeaf_palette.inverse_surface)
#define VQEAF_C_INVERSE_ON_SURFACE        (vqeaf_palette.inverse_on_surface)

typedef enum {
    VQEAF_THEME_DARK = 0,
    VQEAF_THEME_LIGHT = 1
} vqeaf_theme_mode_t;

/* Fills the palette. Session-only: no prefs partition on violet. */
void vqeaf_theme_set(vqeaf_theme_mode_t mode);
vqeaf_theme_mode_t vqeaf_theme_get(void);

/* Liquid Glass is an independent, session-only surface treatment. It affects
 * floating chrome and selected Home/lock surfaces without changing the user's
 * dark/light mode or dynamic-colour seed. */
void vqeaf_theme_set_liquid_glass(int on);
int vqeaf_theme_liquid_glass(void);
void vqeaf_glass_surface(lv_obj_t *obj, int32_t radius, lv_opa_t opacity);

/* Windows 11 (Fluent) skin. Independent session switch like Liquid Glass: it
 * takes over the palette (Mica neutrals + the #0067C0 accent, or their dark
 * counterparts), the corner radius and the surfaces the shell paints, so one
 * switch turns the whole phone shell Fluent. Turning it on clears the dynamic
 * colour seed and Liquid Glass, which would otherwise fight it. */
void vqeaf_theme_set_fluent(int on);
int vqeaf_theme_fluent(void);
/* Corner radius for shell cards: Fluent 8 dp (12 px on the 720-wide canvas),
 * the stock 28 otherwise. */
int32_t vqeaf_theme_card_radius(void);
/* Acrylic surface: translucent Mica tint, hairline border and a short drop
 * shadow. The renderer has no backdrop blur, so the tint is alpha only. */
void vqeaf_acrylic_surface(lv_obj_t *obj, int32_t radius);

/* Dynamic colour (Material You style): every role is generated from one seed
 * colour — tinted neutrals for the background and surfaces, the seed's hue at
 * higher chroma for the accent. `rgb` is 0xRRGGBB; 0 restores the stock
 * palette. Session-only like the mode, and the seed survives a mode switch. */
void vqeaf_theme_set_seed(uint32_t rgb);
uint32_t vqeaf_theme_seed(void);

/* Material You automatic mode: the seed follows the wallpaper's dominant
 * colour instead of a hand-picked one. The wallpaper is procedural today, so
 * the derivation samples the fills vqeaf_wallpaper() paints; a future image
 * wallpaper only needs to replace the sampling, not the palette code. */
void vqeaf_theme_set_auto(int on);
int vqeaf_theme_auto(void);
/* The seed the current wallpaper would yield (valid even when auto is off,
 * so the settings card can preview it). */
uint32_t vqeaf_theme_wallpaper_seed(void);

/* --- constructors --------------------------------------------------------- */

/* A bare container: transparent, no border, no padding, no scrolling. */
lv_obj_t *vqeaf_obj(lv_obj_t *parent);
/* A raised surface card with the shell's radius and hairline border. */
lv_obj_t *vqeaf_card(lv_obj_t *parent, int32_t w, int32_t h);
/* Flex column / row with the shell's spacing. */
lv_obj_t *vqeaf_column(lv_obj_t *parent);
lv_obj_t *vqeaf_row(lv_obj_t *parent);
/* Proportional layout weights: children of a weighted container share the
 * free space in ratio to vqeaf_weight(). Row weights split width, column
 * weights split the given height. */
lv_obj_t *vqeaf_weighted_row(lv_obj_t *parent);
lv_obj_t *vqeaf_weighted_column(lv_obj_t *parent, int32_t height);
void vqeaf_weight(lv_obj_t *obj, int weight);
/* Text and Material Icons glyphs. */
lv_obj_t *vqeaf_label(lv_obj_t *parent, const char *text,
                      const lv_font_t *font, lv_color_t color);
lv_obj_t *vqeaf_glyph(lv_obj_t *parent, const char *symbol,
                      const lv_font_t *font, lv_color_t color);
/* A rounded square icon badge. */
lv_obj_t *vqeaf_tile(lv_obj_t *parent, const char *symbol,
                     lv_color_t bg, lv_color_t fg,
                     int32_t size, int32_t radius);
/* Vivid app icon tile: top is the app accent and bottom is its deep shade. */
void vqeaf_app_icon_style(lv_obj_t *tile, lv_color_t top,
                          lv_color_t bottom);
/* A bitmap app icon (VPEPixel art compiled into the shell) scaled to size.
 * Returns NULL when dsc is NULL so callers can fall back to a glyph tile. */
lv_obj_t *vqeaf_icon_img(lv_obj_t *parent, const lv_image_dsc_t *dsc,
                         int32_t size);
/* Make an object clickable and route LV_EVENT_CLICKED to cb. */
void vqeaf_on_click(lv_obj_t *obj, lv_event_cb_t cb, void *user_data);

/* --- Material Design 3 ---------------------------------------------------- */
/* The Material design system, reduced to what a shell without a
 * bitmap pipeline needs: the shape scale, elevation, state layers/ripple and
 * the core components. Tokens are units of the 720x1560 shell, not dp, but
 * the ratios match Material 3 (button 56 high, touch target >= 48). */
#define VQEAF_M3_SHAPE_NONE 0
#define VQEAF_M3_SHAPE_XS   4
#define VQEAF_M3_SHAPE_S    8
#define VQEAF_M3_SHAPE_M    12
#define VQEAF_M3_SHAPE_L    16
#define VQEAF_M3_SHAPE_XL   28
#define VQEAF_M3_SHAPE_FULL LV_RADIUS_CIRCLE
/* Motion durations (ms) from the Material 3 motion scale. */
#define VQEAF_M3_MOTION_SHORT  150
#define VQEAF_M3_MOTION_MEDIUM 300
#define VQEAF_M3_MOTION_LONG   500
/* State-layer opacities (of the on-surface colour) at 12% pressed, 8% hover. */
#define VQEAF_M3_STATE_PRESSED 32
#define VQEAF_M3_STATE_HOVER   20

typedef enum {
    VQEAF_M3_FILLED = 0,   /* primary fill, on-primary label */
    VQEAF_M3_TONAL,        /* secondary container fill */
    VQEAF_M3_ELEVATED,     /* surface + shadow, primary label */
    VQEAF_M3_OUTLINED,     /* transparent + outline border */
    VQEAF_M3_TEXT          /* transparent, no border */
} vqeaf_m3_button_style_t;

typedef enum {
    VQEAF_M3_CARD_ELEVATED = 0,
    VQEAF_M3_CARD_FILLED,
    VQEAF_M3_CARD_OUTLINED
} vqeaf_m3_card_style_t;

/* Elevation level 0..5: shadow width/offset/opacity from the M3 scale. */
void vqeaf_m3_elevate(lv_obj_t *obj, int level);
/* The scale itself: level -> dp, straight from Material 3 (0, 1, 3, 6, 8,
 * 12 dp). Component roles follow it: cards 0/1, app bar 0 at rest and 2 when
 * scrolled, FAB 3, dialog and snackbar 3. */
typedef struct { int level; int dp; } vqeaf_m3_elevation_t;
const vqeaf_m3_elevation_t *vqeaf_m3_elevation_scale(void);
/* Pressed/hovered state layer: blends the on-surface colour over the object's
 * own fill, or paints a 12% tint when the object is transparent. */
void vqeaf_m3_state_layer(lv_obj_t *obj);
/* Same, but with an explicit tint (e.g. a filled button uses on-primary, so
 * the layer darkens instead of washing the primary out with white). */
void vqeaf_m3_state_layer_color(lv_obj_t *obj, lv_color_t tint);
/* State layer plus a real expanding ripple clipped to the object's corners.
 * The caller owns the object; the ripple child is created here. */
void vqeaf_m3_ripple(lv_obj_t *obj);
void vqeaf_m3_ripple_color(lv_obj_t *obj, lv_color_t tint);

lv_obj_t *vqeaf_m3_button(lv_obj_t *parent, const char *text,
                          vqeaf_m3_button_style_t style);
lv_obj_t *vqeaf_m3_fab(lv_obj_t *parent, const char *symbol);
/* Extended FAB: 64 high pill with icon + label, level 3 like the round one. */
lv_obj_t *vqeaf_m3_fab_extended(lv_obj_t *parent, const char *symbol,
                                const char *label);
/* Filter/choice chip; `selected` swaps to the secondary container role. */
lv_obj_t *vqeaf_m3_chip(lv_obj_t *parent, const char *text, int selected);
/* Switch track + thumb. Visual only: the caller stores and flips the state. */
lv_obj_t *vqeaf_m3_switch(lv_obj_t *parent, int on);
lv_obj_t *vqeaf_m3_card(lv_obj_t *parent, int32_t w, int32_t h,
                        vqeaf_m3_card_style_t style);

/* --- Material Design 3 phase 2: type scale, navigation, lists, overlays --- */
/* The M3 type scale mapped onto the rasterised Ubuntu faces. Display sizes are
 * pinned to the bold clock faces; text roles honour the bold-text mode. Only
 * rasterised sizes (20/24/28/34/56/96) are used; the nominal M3 dp size and
 * tracking are kept as tokens so layout and gates can reference the scale. */
typedef enum {
    VQEAF_M3_TYPE_DISPLAY_LARGE = 0,
    VQEAF_M3_TYPE_DISPLAY_SMALL,
    VQEAF_M3_TYPE_HEADLINE_LARGE,
    VQEAF_M3_TYPE_HEADLINE_SMALL,
    VQEAF_M3_TYPE_TITLE_LARGE,
    VQEAF_M3_TYPE_TITLE_MEDIUM,
    VQEAF_M3_TYPE_TITLE_SMALL,
    VQEAF_M3_TYPE_BODY_LARGE,
    VQEAF_M3_TYPE_BODY_MEDIUM,
    VQEAF_M3_TYPE_BODY_SMALL,
    VQEAF_M3_TYPE_LABEL_LARGE,
    VQEAF_M3_TYPE_LABEL_MEDIUM,
    VQEAF_M3_TYPE_LABEL_SMALL
} vqeaf_m3_type_t;
const lv_font_t *vqeaf_m3_type(vqeaf_m3_type_t type);
typedef struct { int32_t size; int32_t tracking; int weight; } vqeaf_m3_type_token_t;
const vqeaf_m3_type_token_t *vqeaf_m3_type_token(vqeaf_m3_type_t type);

/* Small top app bar: 64 high, surface-container fill, leading nav icon,
 * title-large headline, optional trailing action. Nav/action are clickable and
 * route LV_EVENT_CLICKED to cb (pass NULL cb for a decorative bar). */
lv_obj_t *vqeaf_m3_top_app_bar(lv_obj_t *parent, const char *title,
                               const char *nav_symbol, const char *action_symbol,
                               lv_event_cb_t cb, void *user_data);
/* Bottom navigation bar (80 high, surface-container). Add items with
 * vqeaf_m3_nav_item; the selected item shows the M3 active-indicator pill. */
lv_obj_t *vqeaf_m3_navigation_bar(lv_obj_t *parent);
lv_obj_t *vqeaf_m3_nav_item(lv_obj_t *bar, const char *symbol, const char *label,
                            int selected);
/* One-line (56) / two-line (72) list item: leading icon, body-large headline,
 * body-medium supporting text, ripple. A trailing slot (switch/chevron) is
 * created at the end and returned via trailing_out (NULL if not needed). */
lv_obj_t *vqeaf_m3_list_item(lv_obj_t *parent, const char *leading_symbol,
                             const char *headline, const char *supporting,
                             lv_obj_t **trailing_out);
/* Basic dialog: 40% scrim + surface-container-high card (radius XL), headline,
 * supporting text, right-aligned text buttons. Returns the scrim; the buttons
 * are returned via out params so the caller wires real callbacks. */
lv_obj_t *vqeaf_m3_dialog(lv_obj_t *parent, const char *headline,
                          const char *supporting, const char *confirm_text,
                          const char *dismiss_text, lv_obj_t **confirm_out,
                          lv_obj_t **dismiss_out);
/* Snackbar: inverse-surface bar (radius XS) with message + optional action. */
lv_obj_t *vqeaf_m3_snackbar(lv_obj_t *parent, const char *message,
                            const char *action_text, lv_obj_t **action_out);
/* Slider 0..100: primary active track + thumb, surface-container-highest
 * inactive track, 48 high touch target. Draggable: press/drag sends
 * LV_EVENT_VALUE_CHANGED; read the value with vqeaf_m3_slider_get. */
lv_obj_t *vqeaf_m3_slider(lv_obj_t *parent, int value);
void vqeaf_m3_slider_set(lv_obj_t *slider, int value);
int vqeaf_m3_slider_get(lv_obj_t *slider);
/* Linear progress 0..100: 8 high rounded track, primary indicator. */
lv_obj_t *vqeaf_m3_progress_linear(lv_obj_t *parent, int value);
void vqeaf_m3_progress_set(lv_obj_t *bar, int value);
/* Indeterminate linear progress: a sweeping primary indicator, no percentage
 * (for atomic operations where a honest percent does not exist). */
lv_obj_t *vqeaf_m3_progress_indeterminate(lv_obj_t *parent);
/* The mountain-lake gradient used by the lock screen and home header. */
void vqeaf_wallpaper(lv_obj_t *parent);
/* Text colour for labels drawn directly over the photo wallpaper. Returns a
 * light tint while the real image is active so dark text never disappears
 * into the black-hole background; falls back to the normal theme colour. */
lv_color_t vqeaf_wallpaper_text_color(void);
/* 1 when the real photo wallpaper descriptor is active. */
int vqeaf_wallpaper_active(void);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_THEME_H */
