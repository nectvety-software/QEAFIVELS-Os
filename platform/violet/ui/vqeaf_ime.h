/* SPDX-License-Identifier: MIT */
/* vqeaf_ime.h - responsive soft keyboard as an independent input-method window.
 *
 * Mirrors the IME service contract: the IME is a window on its own
 * layer, separate from the running app; it measures the display (size, density,
 * orientation, smallest-width breakpoint) to compute its height H, then the
 * shell resizes the app content by H (adjustResize / imePadding) in sync with
 * the slide-up. Keys divide each row by layout weight so they stretch from
 * narrow phones to tablets; adaptive modes cover split / one-handed / floating
 * / extract. No device state is invented: metrics are pure functions of the
 * display geometry passed in.
 */
#ifndef VQEAF_IME_H
#define VQEAF_IME_H

#include "lvgl.h"

typedef enum {
    VQEAF_IME_MODE_NORMAL = 0,
    VQEAF_IME_MODE_SPLIT,
    VQEAF_IME_MODE_ONE_HAND_LEFT,
    VQEAF_IME_MODE_ONE_HAND_RIGHT,
    VQEAF_IME_MODE_FLOATING,
    VQEAF_IME_MODE_EXTRACT
} vqeaf_ime_mode_t;

/* Responsive metrics, all in pixels unless suffixed _dp. Pure: same inputs
 * always give same outputs, so the gate can sweep geometries without a UI. */
typedef struct {
    int32_t screen_w, screen_h;
    int32_t dpi;
    int32_t sw_dp;        /* smallest-width dp, drives resource breakpoints */
    int32_t landscape;
    int32_t width, height;   /* IME window size (H) */
    int32_t x, y;            /* window origin on screen */
    int32_t row_h;           /* key row height after caps */
    int32_t rows;            /* key rows incl. optional number row */
    int32_t number_row;      /* 1 when a number row is added (wide/landscape) */
    int32_t gap;             /* split-mode centre gap */
    int32_t key_font;        /* rasterised face size: 20 / 24 / 28 */
} vqeaf_ime_metrics_t;

void vqeaf_ime_metrics(int32_t w, int32_t h, int32_t dpi,
                       vqeaf_ime_mode_t mode, vqeaf_ime_metrics_t *out);

/* Height caps as percent of screen height (common IME convention). */
#define VQEAF_IME_PORTRAIT_MIN_PCT 27
#define VQEAF_IME_PORTRAIT_MAX_PCT 39
#define VQEAF_IME_LANDSCAPE_MIN_PCT 30
#define VQEAF_IME_LANDSCAPE_MAX_PCT 55
/* One-handed keeps ~78% width; floating is a free ~62% window. */
#define VQEAF_IME_ONE_HAND_PCT 78
#define VQEAF_IME_FLOATING_PCT 62
/* Resource-qualifier breakpoints in smallest-width dp. */
#define VQEAF_IME_SW600_DP 600
#define VQEAF_IME_SW720_DP 720

/* The independent IME window. show() binds a target textarea and an optional
 * app-content object to resize (adjustResize); hide() reverses it. */
void vqeaf_ime_show(lv_obj_t *textarea, lv_obj_t *content);
void vqeaf_ime_hide(void);
int  vqeaf_ime_visible(void);
int32_t vqeaf_ime_height(void);          /* current H, 0 when hidden */
void vqeaf_ime_set_mode(vqeaf_ime_mode_t mode);
vqeaf_ime_mode_t vqeaf_ime_get_mode(void);
void vqeaf_ime_restyle(void);            /* re-apply the applied keyboard theme */
lv_obj_t *vqeaf_ime_window(void);        /* the top-layer window, NULL if hidden */
int vqeaf_ime_haptics_available(void);
int vqeaf_ime_sound_available(void);

#endif
