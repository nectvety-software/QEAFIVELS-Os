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
#include "vqeaf_fonts.h"
#include <vqeaf/clipboard.h>

/* Test doubles for the page-level keyboard-theme registry seam. */
int vqeaf_pages_kb_theme_count(void) { return 1; }
int vqeaf_pages_kb_theme_applied(void) { return 0; }
uint32_t vqeaf_pages_kb_theme_key_color(int index) { (void)index; return 0x2A2D34; }
uint32_t vqeaf_pages_kb_theme_bg_color(int index) { (void)index; return 0x1A1D1F; }
uint32_t vqeaf_pages_kb_theme_accent_color(int index) { (void)index; return 0x9AA0A6; }
void vqeaf_pages_kb_theme_style(lv_obj_t *keyboard) { (void)keyboard; }
void vqeaf_pages_terminal_ime_state(int visible) { (void)visible; }

/* Test doubles for platform haptics: host gate has no vibrator device. */
int vio_haptics_available(void) { return 0; }
int vio_haptic_pulse(int ms) { (void)ms; return 0; }

static int failures;
static int ready_events;

static void ready_event_cb(lv_event_t *event)
{
    (void)event;
    ready_events++;
}

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
    expect("phone portrait H == roomy raw 552", m.height, 552);
    expect("phone portrait row height 104", m.row_h, 104);
    expect("phone portrait key font 28", m.key_font, 28);
    expect("phone portrait row gap 12", m.row_gap, 12);
    expect("phone portrait column gap 12", m.column_gap, 12);
    expect("phone portrait window padding 12", m.padding, 12);
    check_caps("phone-portrait", 720, 1560, 320);

    /* Phone landscape 1560x720 -> number row from orientation, 55% cap. */
    vqeaf_ime_metrics(1560, 720, 320, VQEAF_IME_MODE_NORMAL, &m);
    expect("phone landscape is landscape", m.landscape, 1);
    expect("phone landscape adds number row", m.number_row, 1);
    expect("phone landscape H == 68% cap (489)", m.height, 489);
    expect("phone landscape row height 68", m.row_h, 68);
    expect("phone landscape key font 24", m.key_font, 24);
    check_caps("phone-landscape", 1560, 720, 320);

    /* 7" tablet sw600dp: number row from width, taller 56dp rows. */
    vqeaf_ime_metrics(1200, 1920, 320, VQEAF_IME_MODE_NORMAL, &m);
    expect("tablet7 sw_dp == 600", m.sw_dp, 600);
    expect("tablet7 adds number row", m.number_row, 1);
    expect("tablet7 H == roomy raw 708", m.height, 708);
    expect("tablet7 key font 28", m.key_font, 28);
    check_caps("tablet7", 1200, 1920, 320);

    /* 10" tablet sw720dp+: 60dp rows. */
    vqeaf_ime_metrics(1600, 2560, 320, VQEAF_IME_MODE_NORMAL, &m);
    expect_true("tablet10 sw_dp >= 720", m.sw_dp >= 720);
    expect("tablet10 adds number row", m.number_row, 1);
    expect("tablet10 H == roomy raw 748", m.height, 748);
    expect("tablet10 key font 34", m.key_font, 34);
    check_caps("tablet10", 1600, 2560, 320);

    /* Narrow 21:9 fold 480x1080 -> sw240dp, capped without shrinking the face. */
    vqeaf_ime_metrics(480, 1080, 320, VQEAF_IME_MODE_NORMAL, &m);
    expect("fold sw_dp", m.sw_dp, 240);
    expect("fold no number row", m.number_row, 0);
    expect("fold H == 39% cap (421)", m.height, 421);
    expect("fold key font 24", m.key_font, 24);
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

static lv_obj_t *grid_of(lv_obj_t *win) { (void)win; return vqeaf_ime_grid(); }

static lv_obj_t *find_label(lv_obj_t *obj, const char *text)
{
    if(lv_obj_get_class(obj) == &lv_label_class &&
       strcmp(lv_label_get_text(obj), text) == 0) return obj;
    uint32_t count = lv_obj_get_child_count(obj);
    for(uint32_t i = 0; i < count; i++) {
        lv_obj_t *found = find_label(lv_obj_get_child(obj, i), text);
        if(found != NULL) return found;
    }
    return NULL;
}

static uint32_t next_utf8(const char **cursor)
{
    const unsigned char *s = (const unsigned char *)*cursor;
    uint32_t cp = *s++;
    if(cp < 0x80) {
        *cursor = (const char *)s;
        return cp;
    }
    if((cp & 0xE0) == 0xC0) {
        cp &= 0x1F;
        for(int i = 0; i < 1; i++) cp = (cp << 6) | (*s++ & 0x3F);
    } else if((cp & 0xF0) == 0xE0) {
        cp &= 0x0F;
        for(int i = 0; i < 2; i++) cp = (cp << 6) | (*s++ & 0x3F);
    } else if((cp & 0xF8) == 0xF0) {
        cp &= 0x07;
        for(int i = 0; i < 3; i++) cp = (cp << 6) | (*s++ & 0x3F);
    } else {
        return 0;
    }
    *cursor = (const char *)s;
    return cp;
}

static int emoji_label_ready(lv_obj_t *label, uint32_t *missing)
{
    if(label == NULL || lv_obj_get_class(label) != &lv_label_class ||
       lv_obj_get_style_text_font(label, 0) != &vqeaf_emoji_28) {
        *missing = 0;
        return 0;
    }
    const char *text = lv_label_get_text(label);
    while(*text != '\0') {
        uint32_t cp = next_utf8(&text);
        lv_font_glyph_dsc_t dsc;
        if(cp == 0 || !lv_font_get_glyph_dsc(&vqeaf_emoji_28, &dsc, cp, 0) ||
           dsc.box_w == 0 || dsc.box_h == 0 || dsc.gid.index == 0) {
            *missing = cp;
            return 0;
        }
    }
    return 1;
}

static void window_gate(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *content = vqeaf_obj(screen);
    lv_obj_set_size(content, width, height);
    lv_obj_t *ta = lv_textarea_create(content);
    lv_obj_set_size(ta, 400, 80);
    lv_textarea_set_one_line(ta, true);
    lv_obj_add_event_cb(ta, ready_event_cb, LV_EVENT_READY, NULL);
    lv_obj_update_layout(screen);
    int base = lv_obj_get_height(content);

    vqeaf_ime_set_mode(VQEAF_IME_MODE_NORMAL);
    vqeaf_ime_show(ta, content);
    step(400);
    lv_obj_update_layout(screen);

    expect_true("IME is visible after show", vqeaf_ime_visible());
    const lv_font_t *editor_font = lv_obj_get_style_text_font(ta, 0);
    lv_font_glyph_dsc_t editor_ascii;
    lv_font_glyph_dsc_t editor_emoji;
    expect_true("IME editor font keeps ordinary Latin glyphs",
                lv_font_get_glyph_dsc(editor_font, &editor_ascii, 'q', 0) &&
                editor_ascii.resolved_font != &vqeaf_emoji_28);
    expect_true("IME editor font resolves inserted emoji through fallback",
                lv_font_get_glyph_dsc(editor_font, &editor_emoji, 0x1F600, 0) &&
                editor_emoji.resolved_font == &vqeaf_emoji_28);
    lv_obj_t *win = vqeaf_ime_window();
    expect_true("IME window exists on its own layer", win != NULL);
    expect_true("IME window lives on the top layer, not the app",
               lv_obj_get_parent(win) == lv_layer_top());
    expect("IME height H == 552", vqeaf_ime_height(), 552);
    expect("window height == H", lv_obj_get_height(win), 552);
    expect("window slid up to y == h-H (1008)", lv_obj_get_y(win), 1008);
    expect("app content resized by H (adjustResize)", lv_obj_get_height(content),
           base - 552);
    /* A duplicate focus/click notification must not recapture the shrunken
     * height as the new base or compound the keyboard inset. */
    vqeaf_ime_show(ta, content);
    step(50);
    expect("duplicate show keeps the original resize inset",
           lv_obj_get_height(content), base - 552);
    expect("window uses the measured horizontal padding",
           lv_obj_get_style_pad_left(win, 0), 12);
    expect("window uses the measured vertical padding",
           lv_obj_get_style_pad_top(win, 0), 12);
    expect("suggestion strip uses the measured row gap",
           lv_obj_get_style_pad_row(win, 0), 12);
    expect("suggestion strip keeps the taller NuKey-style rail",
           lv_obj_get_height(vqeaf_ime_suggestions()), 64);

    /* Weighted keys: row3 shift (weight 3) is wider than z (weight 2). */
    lv_obj_t *grid = grid_of(win);
    expect("normal grid has 4 key rows", lv_obj_get_child_count(grid), 4);
    lv_obj_t *row3 = lv_obj_get_child(grid, 2);
    lv_obj_t *shift = lv_obj_get_child(row3, 0);
    lv_obj_t *zkey = lv_obj_get_child(row3, 1);
    lv_obj_update_layout(win);
    expect_true("layout_weight: 1.5-weight key wider than 1-weight key",
                lv_obj_get_width(shift) > lv_obj_get_width(zkey));
    expect("normal keys use the roomy horizontal gap",
           lv_obj_get_style_pad_column(lv_obj_get_child(grid, 0), 0), 12);
    expect("normal rows use the roomy vertical gap",
           lv_obj_get_style_pad_row(grid, 0), 12);

    /* Text edits refresh suggestions without replacing the key grid. */
    lv_obj_t *qkey = lv_obj_get_child(lv_obj_get_child(grid, 0), 0);
    lv_obj_t *wkey = lv_obj_get_child(lv_obj_get_child(grid, 0), 1);
    expect("letter keys use the large rounded cap",
           lv_obj_get_style_radius(qkey, 0), 20);
    expect("letter keys carry a soft lower shadow",
           lv_obj_get_style_shadow_width(qkey, 0), 6);
    expect("Q key exposes its secondary number hint",
           lv_obj_get_child_count(qkey), 2);
    expect_true("Q key number hint is real text",
                find_label(qkey, "1") != NULL);
    lv_obj_t *del = lv_obj_get_child(row3, lv_obj_get_child_count(row3) - 1);
    expect_true("normal letter labels use the larger bold 28 px face",
                lv_obj_get_style_text_font(lv_obj_get_child(qkey, 0), 0) ==
                &vqeaf_font_bold_28);
    lv_obj_send_event(qkey, LV_EVENT_CLICKED, NULL);
    step(50);
    lv_obj_update_layout(win);
    expect_true("clicking 'q' inserts into the bound textarea",
                strcmp(lv_textarea_get_text(ta), "q") == 0);
    expect_true("character input keeps key objects stable",
                qkey == lv_obj_get_child(lv_obj_get_child(grid, 0), 0) &&
                wkey == lv_obj_get_child(lv_obj_get_child(grid, 0), 1));
    lv_obj_send_event(wkey, LV_EVENT_CLICKED, NULL);
    expect_true("consecutive character keys accumulate",
                strcmp(lv_textarea_get_text(ta), "qw") == 0);
    expect_true("unmatched prefix has no fabricated candidate",
                find_label(vqeaf_ime_suggestions(), "—") != NULL);

    lv_textarea_set_text(ta, "");
    lv_textarea_set_cursor_pos(ta, 0);
    {
        const char *const letters[] = { "s", "h", "i", "p" };
        for(size_t i = 0; i < sizeof(letters) / sizeof(letters[0]); i++) {
            lv_obj_t *letter = find_label(grid, letters[i]);
            if(letter != NULL)
                lv_obj_send_event(lv_obj_get_parent(letter), LV_EVENT_CLICKED, NULL);
        }
    }
    step(50);
    expect_true("suggestions offer the current word prefix",
                find_label(vqeaf_ime_suggestions(), "shipping") != NULL &&
                find_label(vqeaf_ime_suggestions(), "ship") != NULL);
    lv_textarea_set_text(ta, "ship today");
    lv_textarea_set_cursor_pos(ta, 4);
    lv_obj_send_event(qkey, LV_EVENT_CLICKED, NULL);
    lv_obj_send_event(del, LV_EVENT_CLICKED, NULL);
    step(50);
    lv_obj_t *suggestion = find_label(vqeaf_ime_suggestions(), "shipping");
    lv_obj_send_event(lv_obj_get_parent(suggestion), LV_EVENT_CLICKED, NULL);
    step(50);
    expect_true("candidate replaces only the current word",
                strcmp(lv_textarea_get_text(ta), "shipping today") == 0);
    expect("candidate places the cursor after the replacement",
           lv_textarea_get_cursor_pos(ta), 8);
    expect_true("candidate click keeps the IME visible", vqeaf_ime_visible());

    lv_textarea_set_text(ta, "");
    lv_textarea_set_cursor_pos(ta, 0);
    lv_obj_send_event(qkey, LV_EVENT_CLICKED, NULL);
    step(50);
    lv_obj_send_event(del, LV_EVENT_CLICKED, NULL);
    expect_true("backspace deletes the character",
                strcmp(lv_textarea_get_text(ta), "") == 0);

    /* Emoji panel: ?123 row's emoji toggle, then a real emoji inserts its
     * full UTF-8 sequence into the bound textarea (no mock / no fake state). */
    {
        lv_obj_t *bottom = lv_obj_get_child(grid, 3);
        lv_obj_t *emoji_toggle = lv_obj_get_child(bottom, 1);
        lv_obj_t *toggle_label = lv_obj_get_child(emoji_toggle, 0);
        uint32_t missing = 0;
        expect_true("emoji toggle shows the real U+1F600 glyph",
                    strcmp(lv_label_get_text(toggle_label), "\xF0\x9F\x98\x80") == 0);
        expect_true("emoji toggle uses a resolvable emoji font glyph",
                    emoji_label_ready(toggle_label, &missing));
        lv_obj_send_event(emoji_toggle, LV_EVENT_CLICKED, NULL);
        step(50); /* ime_rebuild_async runs deferred */
        grid = grid_of(win);
        expect("emoji panel has 4 rows", lv_obj_get_child_count(grid), 4);
        int panel_ready = 1;
        for(int row = 0; row < 3; row++) {
            lv_obj_t *keys = lv_obj_get_child(grid, row);
            for(int col = 0; col < 10; col++) {
                lv_obj_t *label = lv_obj_get_child(lv_obj_get_child(keys, col), 0);
                if(!emoji_label_ready(label, &missing)) {
                    printf("FAIL emoji panel row %d key %d missing U+%04X\n",
                           row, col, (unsigned)missing);
                    failures++;
                    panel_ready = 0;
                }
            }
        }
        if(panel_ready) printf("ok   all 30 visible emoji keys resolve real glyphs\n");
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

    /* Clipboard controls copy and paste UTF-8 text into the focused editor. */
    {
        lv_obj_t *copy = lv_obj_get_child(vqeaf_ime_suggestions(), 3);
        lv_obj_t *paste = lv_obj_get_child(vqeaf_ime_suggestions(), 4);
        lv_textarea_set_text(ta, "termux style clipboard");
        lv_obj_send_event(copy, LV_EVENT_CLICKED, NULL);
        lv_textarea_set_text(ta, "");
        lv_obj_send_event(paste, LV_EVENT_CLICKED, NULL);
        expect_true("IME COPY/PASTE round-trips clipboard text",
                    strcmp(lv_textarea_get_text(ta),
                           "termux style clipboard") == 0);
        expect_true("clipboard actions keep the IME visible",
                    vqeaf_ime_visible());
    }

    /* ENTER on a one-line command field must dispatch READY, not append an
     * ignored newline; the terminal subscribes to this event to run a command. */
    {
        lv_obj_t *bottom = lv_obj_get_child(grid, 3);
        lv_obj_t *enter = lv_obj_get_child(bottom,
                                           lv_obj_get_child_count(bottom) - 1);
        lv_textarea_set_text(ta, "help");
        ready_events = 0;
        lv_obj_send_event(enter, LV_EVENT_CLICKED, NULL);
        expect("IME ENTER dispatches READY for one-line input", ready_events, 1);
        expect_true("ENTER does not dismiss the keyboard", vqeaf_ime_visible());
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
    lv_obj_t *split_q = lv_obj_get_child(lv_obj_get_child(
        lv_obj_get_child(grid, 0), 0), 0);
    expect_true("split letter labels stay readable at bold 24 px",
                lv_obj_get_style_text_font(lv_obj_get_child(split_q, 0), 0) ==
                &vqeaf_font_bold_24);

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
    grid = grid_of(win);
    lv_obj_t *floating_q = lv_obj_get_child(lv_obj_get_child(grid, 0), 0);
    expect_true("floating letter labels stay readable at bold 24 px",
                lv_obj_get_style_text_font(lv_obj_get_child(floating_q, 0), 0) ==
                &vqeaf_font_bold_24);

    /* Extract keeps its expanded editor and every key row on screen. */
    vqeaf_ime_set_mode(VQEAF_IME_MODE_EXTRACT);
    step(50);
    lv_obj_update_layout(win);
    expect("extract window covers the display", lv_obj_get_height(win), height);
    expect("extract window has editor, suggestions and keys",
           lv_obj_get_child_count(win), 3);
    grid = grid_of(win);
    lv_obj_t *extract_last = lv_obj_get_child(grid, lv_obj_get_child_count(grid) - 1);
    expect_true("extract bottom key row stays inside the window",
                lv_obj_get_y(grid) + lv_obj_get_y(extract_last) +
                lv_obj_get_height(extract_last) <= height - 12);

    /* Hide followed by an immediate refocus must cancel the old teardown
     * animation and re-open without losing the original content height. */
    vqeaf_ime_set_mode(VQEAF_IME_MODE_NORMAL);
    step(50);
    vqeaf_ime_hide();
    step(80);
    vqeaf_ime_show(ta, content);
    step(400);
    lv_obj_update_layout(screen);
    expect_true("IME can reopen while hide animation is in flight",
                vqeaf_ime_visible());
    expect("rapid re-open restores the exact single inset",
           lv_obj_get_height(content), base - 552);
    vqeaf_ime_hide();
    step(400);
    lv_obj_update_layout(screen);
    expect_true("IME hidden after hide", !vqeaf_ime_visible());
    expect_true("IME window torn down", vqeaf_ime_window() == NULL);
    expect("app content restored to full height", lv_obj_get_height(content), base);
    expect("height reports 0 when hidden", vqeaf_ime_height(), 0);
}

static void extract_landscape_gate(void)
{
    width = 1560;
    height = 720;
    lv_display_set_resolution(display, width, height);
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_size(screen, width, height);
    lv_obj_t *content = vqeaf_obj(screen);
    lv_obj_set_size(content, width, height);
    lv_obj_t *ta = lv_textarea_create(content);
    lv_obj_set_size(ta, 400, 80);

    vqeaf_ime_set_mode(VQEAF_IME_MODE_EXTRACT);
    vqeaf_ime_show(ta, content);
    step(400);
    lv_obj_t *win = vqeaf_ime_window();
    lv_obj_update_layout(win);
    expect("landscape extract window covers the display",
           lv_obj_get_height(win), height);
    expect("landscape extract keeps the number row and four letter rows",
           lv_obj_get_child_count(grid_of(win)), 5);
    lv_obj_t *grid = grid_of(win);
    lv_obj_t *last = lv_obj_get_child(grid, lv_obj_get_child_count(grid) - 1);
    expect_true("landscape extract bottom key row stays inside the window",
                lv_obj_get_y(grid) + lv_obj_get_y(last) +
                lv_obj_get_height(last) <= height - 12);

    vqeaf_ime_set_mode(VQEAF_IME_MODE_NORMAL);
    vqeaf_ime_hide();
    step(400);
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
    extract_landscape_gate();

    free(framebuffer);
    if(failures) { printf("vqeaf_ime: %d FAILURES\n", failures); return 1; }
    printf("vqeaf_ime: ALL OK\n");
    return 0;
}