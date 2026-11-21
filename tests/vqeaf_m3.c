/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_m3.c - host gate for the Material Design 3 core (phase 2).
 *
 * Proves, with real LVGL layout, that the M3 type scale resolves to the
 * rasterised faces (display pinned bold, text roles honour bold-text mode),
 * and that the phase-2 components carry the M3 geometry/roles: top app bar
 * 64h, navigation bar 80h with the active-indicator pill, list item 56/72h,
 * dialog 40% scrim + radius-XL card, snackbar inverse surface, slider and
 * linear progress map 0..100 onto track/thumb geometry.
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

static int ceq(lv_color_t a, lv_color_t b)
{
    return a.red == b.red && a.green == b.green && a.blue == b.blue;
}

static uint32_t *framebuffer;
static lv_display_t *display;
static int width = 720;
static int height = 1200;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area; (void)px_map;
    lv_display_flush_ready(disp);
}

static uint32_t tick_cb(void) { return 0; }

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
    vqeaf_theme_set(VQEAF_THEME_DARK);
    vqeaf_type_set_bold(0);

    /* --- type scale ------------------------------------------------------- */
    expect("display large pinned to bold 96",
           vqeaf_m3_type(VQEAF_M3_TYPE_DISPLAY_LARGE) == &vqeaf_font_bold_96, 1);
    expect("display small pinned to bold 56",
           vqeaf_m3_type(VQEAF_M3_TYPE_DISPLAY_SMALL) == &vqeaf_font_bold_56, 1);
    expect("label large is bold caption",
           vqeaf_m3_type(VQEAF_M3_TYPE_LABEL_LARGE) == &vqeaf_font_bold_20, 1);
    expect("label medium is regular caption",
           vqeaf_m3_type(VQEAF_M3_TYPE_LABEL_MEDIUM) == &vqeaf_font_20, 1);
    expect("label large != label medium",
           vqeaf_m3_type(VQEAF_M3_TYPE_LABEL_LARGE) !=
           vqeaf_m3_type(VQEAF_M3_TYPE_LABEL_MEDIUM), 1);
    vqeaf_type_set_bold(1);
    expect("headline large honours bold mode",
           vqeaf_m3_type(VQEAF_M3_TYPE_HEADLINE_LARGE) == &vqeaf_font_bold_34, 1);
    expect("body large honours bold mode",
           vqeaf_m3_type(VQEAF_M3_TYPE_BODY_LARGE) == &vqeaf_font_bold_24, 1);
    vqeaf_type_set_bold(0);
    expect("headline large regular after bold off",
           vqeaf_m3_type(VQEAF_M3_TYPE_HEADLINE_LARGE) == &vqeaf_font_34, 1);
    /* M3 sizes are non-increasing within each family (display/headline/title/
     * body/label); across families body-large may exceed title-small. */
    int mono = 1;
    static const int families[][2] = {
        { VQEAF_M3_TYPE_DISPLAY_LARGE, VQEAF_M3_TYPE_DISPLAY_SMALL },
        { VQEAF_M3_TYPE_HEADLINE_LARGE, VQEAF_M3_TYPE_HEADLINE_SMALL },
        { VQEAF_M3_TYPE_TITLE_LARGE, VQEAF_M3_TYPE_TITLE_SMALL },
        { VQEAF_M3_TYPE_BODY_LARGE, VQEAF_M3_TYPE_BODY_SMALL },
        { VQEAF_M3_TYPE_LABEL_LARGE, VQEAF_M3_TYPE_LABEL_SMALL }
    };
    for(unsigned f = 0; f < sizeof(families) / sizeof(families[0]); f++)
        for(int t = families[f][0]; t < families[f][1]; t++)
            if(vqeaf_m3_type_token(t)->size < vqeaf_m3_type_token(t + 1)->size)
                mono = 0;
    expect("type token sizes non-increasing within each family", mono, 1);
    expect("display large token 57dp",
           vqeaf_m3_type_token(VQEAF_M3_TYPE_DISPLAY_LARGE)->size, 57);

    /* --- top app bar ------------------------------------------------------ */
    lv_obj_t *bar = vqeaf_m3_top_app_bar(screen, "Cài đặt",
                                         VQEAF_SYM_ARROW_BACK,
                                         VQEAF_SYM_MORE_VERT, NULL, NULL);
    lv_obj_update_layout(screen);
    expect("top app bar height 64", lv_obj_get_height(bar), 64);
    expect("top app bar surface-container fill",
           ceq(lv_obj_get_style_bg_color(bar, 0), VQEAF_C_SURFACE_CONTAINER), 1);
    expect("top app bar has nav + title + action",
           lv_obj_get_child_count(bar), 3);
    expect("nav icon button clickable",
           lv_obj_has_flag(lv_obj_get_child(bar, 0), LV_OBJ_FLAG_CLICKABLE), 1);

    /* --- navigation bar + active indicator -------------------------------- */
    lv_obj_t *nav = vqeaf_m3_navigation_bar(screen);
    lv_obj_t *sel = vqeaf_m3_nav_item(nav, VQEAF_SYM_HOME, "Home", 1);
    lv_obj_t *unsel = vqeaf_m3_nav_item(nav, VQEAF_SYM_SETTINGS, "Cài đặt", 0);
    lv_obj_update_layout(screen);
    expect("navigation bar height 80", lv_obj_get_height(nav), 80);
    lv_obj_t *pill_sel = lv_obj_get_child(sel, 1);
    lv_obj_t *pill_un = lv_obj_get_child(unsel, 1);
    expect("selected pill opaque",
           lv_obj_get_style_bg_opa(pill_sel, 0), LV_OPA_COVER);
    expect("selected pill secondary-container",
           ceq(lv_obj_get_style_bg_color(pill_sel, 0),
               VQEAF_C_SECONDARY_CONTAINER), 1);
    expect("unselected pill transparent",
           lv_obj_get_style_bg_opa(pill_un, 0), LV_OPA_TRANSP);
    expect("selected pill full radius",
           lv_obj_get_style_radius(pill_sel, 0), LV_RADIUS_CIRCLE);

    /* --- list item -------------------------------------------------------- */
    lv_obj_t *one = vqeaf_m3_list_item(screen, VQEAF_SYM_WIFI, "Wi-Fi", NULL, NULL);
    lv_obj_t *trail = NULL;
    lv_obj_t *two = vqeaf_m3_list_item(screen, VQEAF_SYM_BLUETOOTH, "Bluetooth",
                                       "Đang tắt", &trail);
    lv_obj_update_layout(screen);
    expect("one-line list item 56", lv_obj_get_height(one), 56);
    expect("two-line list item 72", lv_obj_get_height(two), 72);
    expect("trailing slot returned", trail != NULL, 1);

    /* --- dialog ----------------------------------------------------------- */
    lv_obj_t *confirm = NULL, *dismiss = NULL;
    lv_obj_t *scrim = vqeaf_m3_dialog(screen, "Đặt lại chủ đề?",
                                      "Màu động sẽ trở về mặc định.",
                                      "Đặt lại", "Hủy", &confirm, &dismiss);
    lv_obj_update_layout(screen);
    expect("dialog scrim 40%", lv_obj_get_style_bg_opa(scrim, 0), 102);
    lv_obj_t *card = lv_obj_get_child(scrim, 0);
    expect("dialog card radius XL",
           lv_obj_get_style_radius(card, 0), VQEAF_M3_SHAPE_XL);
    expect("dialog card surface-container-high",
           ceq(lv_obj_get_style_bg_color(card, 0),
               VQEAF_C_SURFACE_CONTAINER_HIGH), 1);
    expect("dialog confirm button returned", confirm != NULL, 1);
    expect("dialog dismiss button returned", dismiss != NULL, 1);

    /* --- snackbar --------------------------------------------------------- */
    lv_obj_t *action = NULL;
    lv_obj_t *snack = vqeaf_m3_snackbar(screen, "Đã lưu phiên", "Hoàn tác",
                                        &action);
    lv_obj_update_layout(screen);
    expect("snackbar inverse surface",
           ceq(lv_obj_get_style_bg_color(snack, 0), VQEAF_C_INVERSE_SURFACE), 1);
    expect("snackbar radius XS",
           lv_obj_get_style_radius(snack, 0), VQEAF_M3_SHAPE_XS);
    expect("snackbar action returned", action != NULL, 1);
    lv_obj_t *albl = lv_obj_get_child(action, 1);
    expect("snackbar action accent colour",
           ceq(lv_obj_get_style_text_color(albl, 0), VQEAF_C_PRIMARY), 1);

    /* --- slider ----------------------------------------------------------- */
    lv_obj_t *sl = vqeaf_m3_slider(screen, 50);
    lv_obj_update_layout(screen);
    int sw = lv_obj_get_width(sl);
    lv_obj_t *act = lv_obj_get_child(sl, 1);
    lv_obj_t *th = lv_obj_get_child(sl, 2);
    expect_near("slider 50 active half track", lv_obj_get_width(act), sw / 2, 2);
    vqeaf_m3_slider_set(sl, 0);
    lv_obj_update_layout(screen);
    expect("slider 0 active empty", lv_obj_get_width(act), 0);
    expect("slider 0 thumb at start", lv_obj_get_x(th), 0);
    vqeaf_m3_slider_set(sl, 100);
    lv_obj_update_layout(screen);
    expect("slider 100 active full", lv_obj_get_width(act), sw);
    expect("slider 100 thumb at end", lv_obj_get_x(th), sw - 24);
    vqeaf_m3_slider_set(sl, 150);  /* clamps */
    lv_obj_update_layout(screen);
    expect("slider clamps >100", lv_obj_get_width(act), sw);

    /* --- linear progress -------------------------------------------------- */
    lv_obj_t *pg = vqeaf_m3_progress_linear(screen, 25);
    lv_obj_update_layout(screen);
    int pw = lv_obj_get_width(pg);
    lv_obj_t *ind = lv_obj_get_child(pg, 0);
    expect_near("progress 25 quarter track", lv_obj_get_width(ind), pw / 4, 2);
    vqeaf_m3_progress_set(pg, 0);
    lv_obj_update_layout(screen);
    expect("progress 0 empty", lv_obj_get_width(ind), 0);
    vqeaf_m3_progress_set(pg, 100);
    lv_obj_update_layout(screen);
    expect("progress 100 full", lv_obj_get_width(ind), pw);

    /* Render one composed frame so the gate also proves the components draw. */
    lv_obj_update_layout(screen);
    lv_refr_now(NULL);
    unsigned long ink = 0;
    for(int i = 0; i < width * height; i++) {
        uint32_t p = framebuffer[i];
        ink += ((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF);
    }
    expect("composed M3 frame has ink", ink > 0, 1);

    if(failures) {
        printf("vqeaf_m3: %d FAILURES\n", failures);
        return 1;
    }
    printf("vqeaf_m3: ALL OK\n");
    return 0;
}