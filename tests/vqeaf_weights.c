/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_weights.c - host gate for the typography + layout weight system.
 *
 * Proves, with real LVGL layout and real rasterised glyphs, that:
 *   - regular and bold are distinct faces and bold carries more ink;
 *   - the bold-text mode switches every VQEAF_FONT_* role to the bold face;
 *   - weighted rows/columns split free space in ratio to vqeaf_weight();
 *   - a weight-0 child keeps its content size instead of stretching.
 * No device state is read; nothing here is part of the initramfs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "vqeaf_theme.h"

static int failures;

static void expect(const char *name, int got, int want)
{
    if(got != want) {
        printf("FAIL %s: got %d want %d\n", name, got, want);
        failures++;
    } else {
        printf("ok   %s\n", name);
    }
}

static void expect_near(const char *name, int got, int want, int tol)
{
    int d = got - want;
    if(d < 0) d = -d;
    if(d > tol) {
        printf("FAIL %s: got %d want %d (+-%d)\n", name, got, want, tol);
        failures++;
    } else {
        printf("ok   %s (%d)\n", name, got);
    }
}

static uint32_t *framebuffer;
static lv_display_t *display;
static int width = 700;
static int height = 300;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area; (void)px_map;
    lv_display_flush_ready(disp);
}

static uint32_t tick_cb(void) { return 0; }

/* Sum of pixel luminance: white text on black, so more ink = larger sum. */
static unsigned long ink(void)
{
    unsigned long sum = 0;
    for(int i = 0; i < width * height; i++) {
        uint32_t p = framebuffer[i];
        sum += ((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF);
    }
    return sum;
}

static uint32_t crc(void)
{
    uint32_t h = 0x811C9DC5u;
    const uint8_t *b = (const uint8_t *)framebuffer;
    for(size_t i = 0; i < (size_t)width * height * 4; i++) {
        h ^= b[i];
        h *= 0x01000193u;
    }
    return h;
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

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    /* --- faces are distinct and bold is heavier --------------------------- */
    expect("regular body face", vqeaf_type_font_weight(VQEAF_ROLE_BODY,
           VQEAF_WEIGHT_REGULAR) == &vqeaf_font_24, 1);
    expect("bold body face", vqeaf_type_font_weight(VQEAF_ROLE_BODY,
           VQEAF_WEIGHT_BOLD) == &vqeaf_font_bold_24, 1);
    expect("bold differs from regular",
           vqeaf_type_font_weight(VQEAF_ROLE_BODY, VQEAF_WEIGHT_BOLD) !=
           vqeaf_type_font_weight(VQEAF_ROLE_BODY, VQEAF_WEIGHT_REGULAR), 1);

    lv_font_glyph_dsc_t reg, bold;
    lv_font_get_glyph_dsc(&vqeaf_font_24, &reg, 'W', 0);
    lv_font_get_glyph_dsc(&vqeaf_font_bold_24, &bold, 'W', 0);
    expect("bold advance wider than regular", bold.adv_w > reg.adv_w, 1);

    /* --- bold-text mode switches every role ------------------------------- */
    vqeaf_type_set_bold(0);
    expect("mode off resolves regular",
           vqeaf_type_font(VQEAF_ROLE_BODY) == &vqeaf_font_24, 1);
    vqeaf_type_set_bold(1);
    expect("mode on resolves bold body",
           vqeaf_type_font(VQEAF_ROLE_BODY) == &vqeaf_font_bold_24, 1);
    expect("mode on resolves bold caption",
           vqeaf_type_font(VQEAF_ROLE_CAPTION) == &vqeaf_font_bold_20, 1);
    expect("mode on resolves bold title",
           vqeaf_type_font(VQEAF_ROLE_TITLE) == &vqeaf_font_bold_28, 1);
    expect("mode on resolves bold headline",
           vqeaf_type_font(VQEAF_ROLE_HEADLINE) == &vqeaf_font_bold_34, 1);
    expect("get_bold reports on", vqeaf_type_get_bold(), 1);
    vqeaf_type_set_bold(0);
    expect("get_bold reports off", vqeaf_type_get_bold(), 0);

    /* --- pixel proof: bold label carries more ink than regular ------------ */
    lv_obj_t *label = vqeaf_label(screen, "Weights Dam 123",
                                  VQEAF_FONT_BODY, lv_color_hex(0xFFFFFF));
    lv_obj_set_pos(label, 0, 0);
    lv_obj_invalidate(screen);
    lv_refr_now(display);
    unsigned long reg_ink = ink();
    uint32_t reg_crc = crc();
    lv_obj_set_style_text_font(label, VQEAF_FONT_BODY_BOLD, 0);
    lv_obj_invalidate(screen);
    lv_refr_now(display);
    unsigned long bold_ink = ink();
    uint32_t bold_crc = crc();
    expect("bold render differs from regular", bold_crc != reg_crc, 1);
    expect("bold render has more ink", bold_ink > reg_ink, 1);
    lv_obj_delete(label);

    /* --- weighted row splits width 1:2:3 ---------------------------------- */
    lv_obj_t *row = vqeaf_weighted_row(screen);
    lv_obj_set_width(row, 600);
    lv_obj_set_style_pad_column(row, 0, 0);
    lv_obj_t *a = vqeaf_obj(row);
    lv_obj_t *b = vqeaf_obj(row);
    lv_obj_t *c = vqeaf_obj(row);
    lv_obj_set_width(a, 0); lv_obj_set_height(a, 40);
    lv_obj_set_width(b, 0); lv_obj_set_height(b, 40);
    lv_obj_set_width(c, 0); lv_obj_set_height(c, 40);
    vqeaf_weight(a, 1); vqeaf_weight(b, 2); vqeaf_weight(c, 3);
    lv_obj_update_layout(row);
    expect_near("row weight1 width", lv_obj_get_width(a), 100, 2);
    expect_near("row weight2 width", lv_obj_get_width(b), 200, 2);
    expect_near("row weight3 width", lv_obj_get_width(c), 300, 2);
    lv_obj_delete(row);

    /* --- equal weights give equal widths (installer button pattern) ------- */
    lv_obj_t *row2 = vqeaf_weighted_row(screen);
    lv_obj_set_width(row2, 400);
    lv_obj_set_style_pad_column(row2, 0, 0);
    lv_obj_t *b1 = vqeaf_obj(row2);
    lv_obj_t *b2 = vqeaf_obj(row2);
    lv_obj_set_width(b1, 0); lv_obj_set_height(b1, 40);
    lv_obj_set_width(b2, 0); lv_obj_set_height(b2, 40);
    vqeaf_weight(b1, 1); vqeaf_weight(b2, 1);
    lv_obj_update_layout(row2);
    expect_near("equal weight left", lv_obj_get_width(b1), 200, 2);
    expect_near("equal weight right", lv_obj_get_width(b2), 200, 2);
    lv_obj_delete(row2);

    /* --- weighted column splits height 1:2:3 ------------------------------ */
    lv_obj_t *col = vqeaf_weighted_column(screen, 300);
    lv_obj_set_style_pad_row(col, 0, 0);
    lv_obj_t *c1 = vqeaf_obj(col);
    lv_obj_t *c2 = vqeaf_obj(col);
    lv_obj_t *c3 = vqeaf_obj(col);
    lv_obj_set_height(c1, 0);
    lv_obj_set_height(c2, 0);
    lv_obj_set_height(c3, 0);
    vqeaf_weight(c1, 1); vqeaf_weight(c2, 2); vqeaf_weight(c3, 3);
    lv_obj_update_layout(col);
    expect_near("col weight1 height", lv_obj_get_height(c1), 50, 2);
    expect_near("col weight2 height", lv_obj_get_height(c2), 100, 2);
    expect_near("col weight3 height", lv_obj_get_height(c3), 150, 2);
    lv_obj_delete(col);

    /* --- weight 0 keeps content size, weight 1 takes the rest ------------- */
    lv_obj_t *row3 = vqeaf_weighted_row(screen);
    lv_obj_set_width(row3, 400);
    lv_obj_set_style_pad_column(row3, 0, 0);
    lv_obj_t *fixed = vqeaf_obj(row3);
    lv_obj_set_height(fixed, 40);
    lv_obj_set_width(fixed, LV_SIZE_CONTENT);
    lv_obj_t *inner = vqeaf_label(fixed, "AB", VQEAF_FONT_BODY,
                                  lv_color_hex(0xFFFFFF));
    lv_obj_t *grow = vqeaf_obj(row3);
    lv_obj_set_width(grow, 0); lv_obj_set_height(grow, 40);
    vqeaf_weight(fixed, 0); vqeaf_weight(grow, 1);
    lv_obj_update_layout(row3);
    expect("weight0 keeps content width",
           lv_obj_get_width(fixed) == lv_obj_get_width(inner), 1);
    expect("weight1 fills the rest", lv_obj_get_width(grow) >
           lv_obj_get_width(fixed), 1);
    lv_obj_delete(row3);

    free(framebuffer);
    if(failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all weights tests passed\n");
    return 0;
}