/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_ime.c - host gate for the responsive independent IME.
 *
 * Two layers of proof, both with real LVGL layout:
 *  1. vqeaf_ime_metrics() is pure, so the gate sweeps phone portrait/landscape,
 *     7" (sw600dp) and 10" (sw720dp) tablets, a narrow 21:9 fold, and asserts
 *     the height caps, the number-row breakpoint, and every adaptive mode's
 *     width/anchor (split gap, one-handed 78%, floating 62%, extract full).
 *  2. The IME is a window on its own layer: show() resizes the bound app
 *     content by H (adjustResize) in sync with the slide-up, keys divide each
 *     row by layout weight (a 1.5-weight key is wider than a 1-weight key),
 *     split renders two half-grids, and clicking a key writes to the bound
 *     textarea (a real input service, not invented state).
 *
 * The keyboard-theme registry lives in vqeaf_pages.c (exercised by the shell
 * gate); here it is a honest test double so this gate stays focused on the IME.
 * No device state is read; nothing here is part of the initramfs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "vqeaf_theme.h"
#include "vqeaf_ime.h"

/* Test doubles for the page-level keyboard-theme registry seam. */
int vqeaf_pages_kb_theme_count(void) { return 1; }
int vqeaf_pages_kb_theme_applied(void) { return 0; }
uint32_t vqeaf_pages_kb_theme_key_color(int index) { (void)index; return 0x2A2D34; }
void vqeaf_pages_kb_theme_style(lv_obj_t *keyboard) { (void)keyboard; }

/* Test doubles for platform haptics: host gate has no vibrator device. */
int vio_haptics_available(void) { return 0; }
int vio_haptic_pulse(int ms) { (void)ms; return 0; }

static int failures;

static void expect(const char *name, int got, int want)
{
    if(got != want) {
        printf("FAIL %s: got %d want %d\n", name, got, want);
        failures++;
    } else {
        printf("ok   %s (%d)\n", name, got);
    }
}

static void expect_true(const char *name, int cond)
{
    if(!cond) { printf("FAIL %s\n", name); failures++; }
    else printf("ok   %s\n", name);
}

static uint32_t *framebuffer;
static lv_display_t *display;
static int width = 720;
static int height = 1560;
static uint32_t g_tick;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area; (void)px_map;
    lv_display_flush_ready(disp);
}
static uint32_t tick_cb(void) { return g_tick; }
static void step(int ms)
{
    for(int i = 0; i < ms; i += 10) { g_tick += 10; lv_timer_handler(); }
}

/* ---- pure metrics sweep -------------------------------------------------- */

static void check_caps(const char *tag, int w, int h, int dpi)
{
    vqeaf_ime_metrics_t m;
    vqeaf_ime_metrics(w, h, dpi, VQEAF_IME_MODE_NORMAL, &m);
    int land = w > h;
    int minp = land ? VQEAF_IME_LANDSCAPE_MIN_PCT : VQEAF_IME_PORTRAIT_MIN_PCT;
    int maxp = land ? VQEAF_IME_LANDSCAPE_MAX_PCT : VQEAF_IME_PORTRAIT_MAX_PCT;
    char name[96];
    snprintf(name, sizeof(name), "%s: H within [%d%%,%d%%] of screen", tag, minp, maxp);
    expect_true(name, m.height >= h * minp / 100 && m.height <= h * maxp / 100);
    snprintf(name, sizeof(name), "%s: width == match_parent", tag);
    expect(name, m.width, w);
    snprintf(name, sizeof(name), "%s: bottom-anchored y == h-H", tag);
    expect(name, m.y, h - m.height);
    snprintf(name, sizeof(name), "%s: rows == 4 + number_row", tag);
    expect(name, m.rows, 4 + m.number_row);
}

static void metrics_sweep(void)
{
    vqeaf_ime_metrics_t m;

    /* Phone portrait 720x1560 @320dpi -> sw360dp, no number row. */
    vqeaf_ime_metrics(720, 1560, 320, VQEAF_IME_MODE_NORMAL, &m);
    expect("phone portrait sw_dp", m.sw_dp, 360);
    expect("phone portrait not landscape", m.landscape, 0);
    expect("phone portrait no number row", m.number_row, 0);
    expect("phone portrait H == raw 428 (27% floor)", m.height, 428);
    expect("phone portrait key font 24", m.key_font, 24);
    check_caps("phone-portrait", 720, 1560, 320);

    /* Phone landscape 1560x720 -> number row from orientation, 55% cap. */
    vqeaf_ime_metrics(1560, 720, 320, VQEAF_IME_MODE_NORMAL, &m);
    expect("phone landscape is landscape", m.landscape, 1);
    expect("phone landscape adds number row", m.number_row, 1);
    expect("phone landscape H == 55% cap (396)", m.height, 396);
    expect("phone landscape key font 20", m.key_font, 20);
    check_caps("phone-landscape", 1560, 720, 320);

    /* 7" tablet sw600dp: number row from width, taller 56dp rows. */
    vqeaf_ime_metrics(1200, 1920, 320, VQEAF_IME_MODE_NORMAL, &m);
    expect("tablet7 sw_dp == 600", m.sw_dp, 600);
    expect("tablet7 adds number row", m.number_row, 1);
    expect("tablet7 H == raw 552", m.height, 552);
    check_caps("tablet7", 1200, 1920, 320);

    /* 10" tablet sw720dp+: 60dp rows. */
    vqeaf_ime_metrics(1600, 2560, 320, VQEAF_IME_MODE_NORMAL, &m);
    expect_true("tablet10 sw_dp >= 720", m.sw_dp >= 720);
    expect("tablet10 adds number row", m.number_row, 1);
    expect("tablet10 H == 27% floor (691)", m.height, 691);
    check_caps("tablet10", 1600, 2560, 320);

    /* Narrow 21:9 fold 480x1080 -> sw240dp, raw height fits inside the caps. */
    vqeaf_ime_metrics(480, 1080, 320, VQEAF_IME_MODE_NORMAL, &m);
    expect("fold sw_dp", m.sw_dp, 240);
    expect("fold no number row", m.number_row, 0);
    expect("fold H == 39% cap (421)", m.height, 421);
    check_caps("fold", 480, 1080, 320);

    /* Adaptive modes on the phone portrait geometry. */
    vqeaf_ime_metrics(720, 1560, 320, VQEAF_IME_MODE_ONE_HAND_LEFT, &m);
    expect("one-hand-left width == 78% (561)", m.width, 561);
    expect("one-hand-left anchored x == 0", m.x, 0);
    vqeaf_ime_metrics(720, 1560, 320, VQEAF_IME_MODE_ONE_HAND_RIGHT, &m);
    expect("one-hand-right width == 78% (561)", m.width, 561);
    expect("one-hand-right anchored x == w-width (159)", m.x, 159);
    vqeaf_ime_metrics(720, 1560, 320, VQEAF_IME_MODE_FLOATING, &m);
    expect("floating width == 62% (446)", m.width, 446);
    expect("floating centred x (137)", m.x, 137);
    expect_true("floating lifted above the bottom edge", m.y < 1560 - m.height);
    vqeaf_ime_metrics(720, 1560, 320, VQEAF_IME_MODE_SPLIT, &m);
    expect("split width == match_parent", m.width, 720);
    expect("split centre gap == 64dp (128px)", m.gap, 128);
    vqeaf_ime_metrics(720, 1560, 320, VQEAF_IME_MODE_EXTRACT, &m);
    expect("extract covers full height", m.height, 1560);
    expect("extract anchored at top", m.y, 0);

    /* Determinism: same inputs -> same outputs. */
    vqeaf_ime_metrics_t a, b;
    vqeaf_ime_metrics(720, 1560, 320, VQEAF_IME_MODE_SPLIT, &a);
    vqeaf_ime_metrics(720, 1560, 320, VQEAF_IME_MODE_SPLIT, &b);
    expect_true("metrics are pure/deterministic", memcmp(&a, &b, sizeof(a)) == 0);
}

/* ---- live window / insets / key engine ----------------------------------- */

static lv_obj_t *grid_of(lv_obj_t *win) { return lv_obj_get_child(win, 0); }

static void window_gate(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *content = vqeaf_obj(screen);
    lv_obj_set_size(content, width, height);
    lv_obj_t *ta = lv_textarea_create(content);
    lv_obj_set_size(ta, 400, 80);
    lv_textarea_set_one_line(ta, true);
    lv_obj_update_layout(screen);
    int base = lv_obj_get_height(content);

    vqeaf_ime_set_mode(VQEAF_IME_MODE_NORMAL);
    vqeaf_ime_show(ta, content);
    step(400);
    lv_obj_update_layout(screen);

    expect_true("IME is visible after show", vqeaf_ime_visible());
    lv_obj_t *win = vqeaf_ime_window();
    expect_true("IME window exists on its own layer", win != NULL);
    expect_true("IME window lives on the top layer, not the app",
               lv_obj_get_parent(win) == lv_layer_top());
    expect("IME height H == 428", vqeaf_ime_height(), 428);
    expect("window height == H", lv_obj_get_height(win), 428);
    expect("window slid up to y == h-H (1132)", lv_obj_get_y(win), 1132);
    expect("app content resized by H (adjustResize)", lv_obj_get_height(content),
           base - 428);

    /* Weighted keys: row3 shift (weight 3) is wider than z (weight 2). */
    lv_obj_t *grid = grid_of(win);
    expect("normal grid has 4 key rows", lv_obj_get_child_count(grid), 4);
    lv_obj_t *row3 = lv_obj_get_child(grid, 2);
    lv_obj_t *shift = lv_obj_get_child(row3, 0);
    lv_obj_t *zkey = lv_obj_get_child(row3, 1);
    lv_obj_update_layout(win);
    expect_true("layout_weight: 1.5-weight key wider than 1-weight key",
                lv_obj_get_width(shift) > lv_obj_get_width(zkey));

    /* Key engine writes to the bound textarea (real input, not fake state). */
    lv_obj_t *qkey = lv_obj_get_child(lv_obj_get_child(grid, 0), 0);
    lv_obj_send_event(qkey, LV_EVENT_CLICKED, NULL);
    expect_true("clicking 'q' inserts into the bound textarea",
                strcmp(lv_textarea_get_text(ta), "q") == 0);
    lv_obj_t *del = lv_obj_get_child(row3, lv_obj_get_child_count(row3) - 1);
    lv_obj_send_event(del, LV_EVENT_CLICKED, NULL);
    expect_true("backspace deletes the character",
                strcmp(lv_textarea_get_text(ta), "") == 0);

    /* Emoji panel: ?123 row's emoji toggle, then a real emoji inserts its
     * full UTF-8 sequence into the bound textarea (no mock / no fake state). */
    {
        lv_obj_t *bottom = lv_obj_get_child(grid, 3);
        lv_obj_t *emoji_toggle = lv_obj_get_child(bottom, 1);
        lv_obj_send_event(emoji_toggle, LV_EVENT_CLICKED, NULL);
        step(50); /* ime_rebuild_async runs deferred */
        grid = grid_of(win);
        expect("emoji panel has 4 rows", lv_obj_get_child_count(grid), 4);
        lv_obj_t *emoji_key = lv_obj_get_child(lv_obj_get_child(grid, 0), 0);
        lv_obj_send_event(emoji_key, LV_EVENT_CLICKED, NULL);
        expect_true("clicking emoji inserts real UTF-8 (U+1F600)",
                    strcmp(lv_textarea_get_text(ta), "\xF0\x9F\x98\x80") == 0);
        lv_obj_t *emoji_del = lv_obj_get_child(lv_obj_get_child(grid, 3), 2);
        lv_obj_send_event(emoji_del, LV_EVENT_CLICKED, NULL);
        expect_true("emoji backspace removes the 4-byte codepoint",
                    strcmp(lv_textarea_get_text(ta), "") == 0);
        /* ABC returns to the letter layout. */
        lv_obj_t *emoji_abc = lv_obj_get_child(lv_obj_get_child(grid, 3), 0);
        lv_obj_send_event(emoji_abc, LV_EVENT_CLICKED, NULL);
        step(50);
        grid = grid_of(win);
        expect("emoji ABC returns to 4 letter rows",
               lv_obj_get_child_count(grid), 4);
    }

    /* Split mode renders two independent half-grids. */
    vqeaf_ime_set_mode(VQEAF_IME_MODE_SPLIT);
    step(50);
    lv_obj_update_layout(win);
    grid = grid_of(win);
    expect("split grid has two half panels", lv_obj_get_child_count(grid), 2);
    expect("left half keeps the 4 key rows",
           lv_obj_get_child_count(lv_obj_get_child(grid, 0)), 4);
    expect("split window still match_parent width", lv_obj_get_width(win), 720);

    /* One-handed shrinks and anchors the window. */
    vqeaf_ime_set_mode(VQEAF_IME_MODE_ONE_HAND_RIGHT);
    step(50);
    lv_obj_update_layout(win);
    expect("one-hand window width 561", lv_obj_get_width(win), 561);
    expect("one-hand window anchored right (x159)", lv_obj_get_x(win), 159);

    /* Floating is a free, centred, lifted window. */
    vqeaf_ime_set_mode(VQEAF_IME_MODE_FLOATING);
    step(50);
    lv_obj_update_layout(win);
    expect("floating window width 446", lv_obj_get_width(win), 446);
    expect("floating window centred (x137)", lv_obj_get_x(win), 137);

    /* Hide reverses the inset and tears the window down. */
    vqeaf_ime_set_mode(VQEAF_IME_MODE_NORMAL);
    step(50);
    vqeaf_ime_hide();
    step(400);
    lv_obj_update_layout(screen);
    expect_true("IME hidden after hide", !vqeaf_ime_visible());
    expect_true("IME window torn down", vqeaf_ime_window() == NULL);
    expect("app content restored to full height", lv_obj_get_height(content), base);
    expect("height reports 0 when hidden", vqeaf_ime_height(), 0);
}

int main(void)
{
    framebuffer = calloc((size_t)width * height, sizeof(uint32_t));
    if(framebuffer == NULL) return 2;
    lv_init();
    lv_tick_set_cb(tick_cb);
    display = lv_display_create(width, height);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display, framebuffer, NULL,
                           (uint32_t)(sizeof(uint32_t) * width * height),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, flush_cb);
    lv_display_set_default(display);
    vqeaf_theme_set(VQEAF_THEME_DARK);

    metrics_sweep();
    window_gate();

    free(framebuffer);
    if(failures) { printf("vqeaf_ime: %d FAILURES\n", failures); return 1; }
    printf("vqeaf_ime: ALL OK\n");
    return 0;
}