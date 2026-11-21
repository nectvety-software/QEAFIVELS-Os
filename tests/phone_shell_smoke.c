#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../system/ui/include/vqeaf/gfx.h"
#include "../system/ui/include/vqeaf/phone_shell.h"
#include "../system/ui/include/vqeaf/screen_common.h"

#define BIG_W 1280u
#define BIG_H 800u
#define PORTRAIT_W 720u
#define PORTRAIT_H 1280u
static uint32_t small_px[240u * 320u];
static uint32_t big_px[BIG_W * BIG_H];
static uint32_t portrait_px[PORTRAIT_W * PORTRAIT_H];
static uint32_t lock_copy[BIG_W * BIG_H];
static uint32_t home_copy[BIG_W * BIG_H];

static uint32_t teal_pixels(const uint32_t *px, uint32_t count) {
    uint32_t teal = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t c = px[i];
        uint32_t r = (c >> 16) & 255u, g = (c >> 8) & 255u, b = c & 255u;
        if (g > 120u && b > 100u && r < 120u) teal++;
    }
    return teal;
}

static uint32_t frame_diff(const uint32_t *a, const uint32_t *b, uint32_t count) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i += 7u)
        if (a[i] != b[i]) n++;
    return n;
}

static void dump_ppm(const char *path, const uint32_t *px, uint32_t w, uint32_t h) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (uint32_t i = 0; i < w * h; i++) {
        uint32_t c = px[i];
        unsigned char rgb[3] = {(unsigned char)((c >> 16) & 255u),
                                (unsigned char)((c >> 8) & 255u),
                                (unsigned char)(c & 255u)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void fill_model(VqeafPhoneModel *m, uint32_t w, uint32_t h) {
    memset(m, 0, sizeof *m);
    m->clock_valid = 1;
    m->year = 2026u;
    m->month = 11u;
    m->day = 10u;
    m->hour = 9u;
    m->minute = 41u;
    m->second = 5u;
    m->timeout_index = 1u;
    m->keep_awake = 0;
    m->battery_present = 0;
    m->storage_mounted = 1;
    m->storage_sectors = 32768u;
    m->storage_path = "/storage";
    m->storage_file = "AHCI.TXT";
    m->storage_summary = "VQEAF AHCI storage verified";
    m->uptime_ticks = 12345u;
    m->free_pages = 50000u;
    m->task_count = 7u;
    m->fb_width = w;
    m->fb_height = h;
}

static void render_big(VqeafFramebuffer *fb, VqeafPhoneState *st,
                       VqeafPhoneModel *m, const char *name, uint32_t min_teal) {
    memset(big_px, 0, sizeof big_px);
    vqeaf_phone_render(fb, st, m);
    uint32_t teal = teal_pixels(big_px, BIG_W * BIG_H);
    if (teal < min_teal)
        fprintf(stderr, "render %s: teal=%u min=%u\n", name ? name : "(none)",
                teal, min_teal);
    assert(teal >= min_teal);
    if (name) {
        char path[96];
        snprintf(path, sizeof path, "screens/%s.ppm", name);
        dump_ppm(path, big_px, BIG_W, BIG_H);
    }
}

static void render_small(VqeafFramebuffer *fb, VqeafPhoneState *st,
                         VqeafPhoneModel *m, uint32_t min_teal) {
    memset(small_px, 0, sizeof small_px);
    vqeaf_phone_render(fb, st, m);
    assert(teal_pixels(small_px, 240u * 320u) >= min_teal);
}

static VqeafPhoneAction key(VqeafPhoneState *st, VqeafPhoneKey k) {
    return vqeaf_phone_key(st, k);
}

/* Independent replica of the shipped home geometry. The touch hit-test must
 * land on these same cell boxes, so the gate derives centres from first
 * principles instead of calling the renderer's private helper. */
static void home_cell_center(const VqeafFramebuffer *fb, uint32_t cols,
                             uint32_t i, uint32_t *cx, uint32_t *cy) {
    VqeafScreenFrame f = vqeaf_screen_frame(fb);
    uint32_t s = f.scale;
    if (cols > 4u) cols = 4u;
    if (cols < 2u) cols = 2u;
    uint32_t rows = (VQEAF_APP_COUNT + cols - 1u) / cols;
    uint32_t margin = 25u * s, gap = 30u * s;
    int32_t icon = (int32_t)((f.w - 2u * margin - (cols - 1u) * gap) / cols);
    uint32_t cap = 56u * s;
    if (icon > (int32_t)cap) icon = (int32_t)cap;
    if (icon < 12) icon = 12;
    uint32_t label_part = 4u * s + 16u;
    uint32_t wide = fb->height >= 560u ? 1u : 0u;
    uint32_t dock_h = wide ? 74u * s : 0u;
    uint32_t bar_y = fb->height > 16u * s ? fb->height - 16u * s : 0u;
    uint32_t dock_y = (wide && bar_y > 8u * s + dock_h) ? bar_y - 8u * s - dock_h : 0u;
    uint32_t top = 30u * s + (wide ? 42u * s : 8u * s);
    uint32_t row_gap = wide ? 26u * s : 8u * s;
    uint32_t bottom = dock_y ? dock_y - 8u * s
                             : (fb->height > 52u * s ? fb->height - 52u * s : top);
    if (bottom > top) {
        uint32_t avail = bottom - top;
        uint32_t fixed = rows * label_part + (rows - 1u) * row_gap;
        if (avail > fixed) {
            uint32_t by_h = (avail - fixed) / rows;
            if (icon > (int32_t)by_h) icon = (int32_t)by_h;
            if (icon < 12) icon = 12;
        }
    }
    uint32_t grid_w = cols * (uint32_t)icon + (cols - 1u) * gap;
    if (grid_w > f.w) grid_w = f.w;
    uint32_t x0 = f.x + (f.w > grid_w ? (f.w - grid_w) / 2u : 0u);
    uint32_t cell_h = (uint32_t)icon + label_part;
    uint32_t row = i / cols, col = i % cols;
    *cx = x0 + col * ((uint32_t)icon + gap) + (uint32_t)icon / 2u;
    *cy = top + row * (cell_h + row_gap) + (uint32_t)icon / 2u;
}

static VqeafPhoneAction tap(VqeafPhoneState *st, const VqeafFramebuffer *fb,
                            uint32_t x, uint32_t y) {
    return vqeaf_phone_tap(st, fb->width, fb->height, x, y);
}

int main(void) {
    VqeafFramebuffer small, big;
    assert(vqeaf_gfx_setup(&small, (uint64_t)(uintptr_t)small_px, 240, 320, 240,
                           VQEAF_GFX_FMT_BGR) == 0);
    assert(vqeaf_gfx_setup(&big, (uint64_t)(uintptr_t)big_px, BIG_W, BIG_H, BIG_W,
                           VQEAF_GFX_FMT_BGR) == 0);

    /* Shipped geometry: a 9:16 portrait panel is filled edge to edge, while a
     * landscape desktop still gets the centred 450 px phone column. */
    {
        VqeafFramebuffer shape;
        memset(&shape, 0, sizeof shape);
        shape.width = PORTRAIT_W;
        shape.height = PORTRAIT_H;
        VqeafScreenFrame f = vqeaf_screen_frame(&shape);
        assert(f.w == PORTRAIT_W && f.x == 0 && f.scale == 2);
        shape.width = 1080;
        shape.height = 1920;
        f = vqeaf_screen_frame(&shape);
        assert(f.w == 1080 && f.x == 0);
        shape.width = BIG_W;
        shape.height = BIG_H;
        f = vqeaf_screen_frame(&shape);
        assert(f.w == 450 && f.x == 415 && f.scale == 2);
    }

    assert(vqeaf_phone_app_count() == VQEAF_APP_COUNT);
    for (uint32_t i = 0; i < VQEAF_APP_COUNT; i++)
        assert(vqeaf_phone_app_label(i) && *vqeaf_phone_app_label(i));
    assert(vqeaf_phone_app_label(VQEAF_APP_COUNT)[0] == 0);
    for (uint32_t i = 0; i < 5u; i++)
        assert(vqeaf_phone_timeout_label(i) && *vqeaf_phone_timeout_label(i));

    VqeafPhoneState st;
    VqeafPhoneModel model;
    fill_model(&model, BIG_W, BIG_H);
    vqeaf_phone_init(&st, 3);
    assert(st.screen == VQEAF_PHONE_LOCK && st.sel == 0 && st.cols == 3);

    assert(vqeaf_phone_key(0, VQEAF_PHONE_KEY_ENTER) == VQEAF_PHONE_ACTION_NONE);

    /* Lock screen: only ENTER unlocks. */
    assert(key(&st, VQEAF_PHONE_KEY_DOWN) == VQEAF_PHONE_ACTION_NONE);
    assert(st.screen == VQEAF_PHONE_LOCK);
    assert(key(&st, VQEAF_PHONE_KEY_ENTER) == VQEAF_PHONE_ACTION_NONE);
    assert(st.screen == VQEAF_PHONE_HOME && st.sel == 0);

    /* Home grid navigation wraps per column and clamps at the last tile. */
    key(&st, VQEAF_PHONE_KEY_RIGHT); assert(st.sel == 1);
    key(&st, VQEAF_PHONE_KEY_DOWN); assert(st.sel == 4);
    key(&st, VQEAF_PHONE_KEY_LEFT); assert(st.sel == 3);
    key(&st, VQEAF_PHONE_KEY_UP); assert(st.sel == 0);
    key(&st, VQEAF_PHONE_KEY_LEFT); assert(st.sel == 0);

    /* ABOUT/SETTINGS are regular app screens. */
    assert(key(&st, VQEAF_PHONE_KEY_ENTER) == VQEAF_PHONE_ACTION_NONE);
    assert(st.screen == VQEAF_PHONE_APP && st.app == VQEAF_APP_ABOUT);
    key(&st, VQEAF_PHONE_KEY_BACK);
    assert(st.screen == VQEAF_PHONE_HOME && st.sel == VQEAF_APP_ABOUT);
    key(&st, VQEAF_PHONE_KEY_RIGHT);
    assert(key(&st, VQEAF_PHONE_KEY_ENTER) == VQEAF_PHONE_ACTION_NONE);
    assert(st.app == VQEAF_APP_SETTINGS && st.sel == 0);
    key(&st, VQEAF_PHONE_KEY_DOWN);
    assert(key(&st, VQEAF_PHONE_KEY_ENTER) == VQEAF_PHONE_ACTION_WAKELOCK_TOGGLE);
    key(&st, VQEAF_PHONE_KEY_UP);
    assert(key(&st, VQEAF_PHONE_KEY_ENTER) == VQEAF_PHONE_ACTION_TIMEOUT_NEXT);
    key(&st, VQEAF_PHONE_KEY_BACK);
    assert(st.screen == VQEAF_PHONE_HOME && st.sel == VQEAF_APP_SETTINGS);

    /* Quick settings rows map to real actions; WiFi/BT stay disabled. */
    memset(&st, 0, sizeof st);
    vqeaf_phone_init(&st, 3);
    key(&st, VQEAF_PHONE_KEY_ENTER);
    assert(key(&st, VQEAF_PHONE_KEY_QUICK) == VQEAF_PHONE_ACTION_NONE);
    assert(st.screen == VQEAF_PHONE_QUICK && st.sel == 0);
    assert(key(&st, VQEAF_PHONE_KEY_ENTER) == VQEAF_PHONE_ACTION_WAKELOCK_TOGGLE);
    key(&st, VQEAF_PHONE_KEY_DOWN);
    assert(key(&st, VQEAF_PHONE_KEY_ENTER) == VQEAF_PHONE_ACTION_TIMEOUT_NEXT);
    key(&st, VQEAF_PHONE_KEY_DOWN);
    assert(key(&st, VQEAF_PHONE_KEY_ENTER) == VQEAF_PHONE_ACTION_SLEEP);
    key(&st, VQEAF_PHONE_KEY_DOWN);
    assert(key(&st, VQEAF_PHONE_KEY_ENTER) == VQEAF_PHONE_ACTION_LOCK);
    assert(st.screen == VQEAF_PHONE_LOCK);
    key(&st, VQEAF_PHONE_KEY_ENTER);
    key(&st, VQEAF_PHONE_KEY_QUICK);
    key(&st, VQEAF_PHONE_KEY_DOWN);
    key(&st, VQEAF_PHONE_KEY_DOWN);
    key(&st, VQEAF_PHONE_KEY_DOWN);
    key(&st, VQEAF_PHONE_KEY_DOWN);
    assert(st.sel == 4);
    assert(key(&st, VQEAF_PHONE_KEY_ENTER) == VQEAF_PHONE_ACTION_POWER_MENU);
    assert(st.screen == VQEAF_PHONE_QUICK);
    key(&st, VQEAF_PHONE_KEY_QUICK);
    assert(st.screen == VQEAF_PHONE_HOME);
    key(&st, VQEAF_PHONE_KEY_QUICK);
    key(&st, VQEAF_PHONE_KEY_DOWN);
    key(&st, VQEAF_PHONE_KEY_DOWN);
    key(&st, VQEAF_PHONE_KEY_DOWN);
    key(&st, VQEAF_PHONE_KEY_DOWN);
    key(&st, VQEAF_PHONE_KEY_DOWN);
    assert(st.sel == 5);
    assert(key(&st, VQEAF_PHONE_KEY_ENTER) == VQEAF_PHONE_ACTION_NONE);
    assert(key(&st, VQEAF_PHONE_KEY_BACK) == VQEAF_PHONE_ACTION_NONE);

    /* Home tiles for update, fastboot and lock report their actions. */
    memset(&st, 0, sizeof st);
    vqeaf_phone_init(&st, 3);
    key(&st, VQEAF_PHONE_KEY_ENTER);
    st.sel = VQEAF_APP_UPDATE;
    assert(key(&st, VQEAF_PHONE_KEY_ENTER) == VQEAF_PHONE_ACTION_UPDATE);
    st.screen = VQEAF_PHONE_HOME;
    st.sel = VQEAF_APP_POWER;
    assert(key(&st, VQEAF_PHONE_KEY_ENTER) == VQEAF_PHONE_ACTION_POWER_MENU);
    st.screen = VQEAF_PHONE_HOME;
    st.sel = VQEAF_APP_FASTBOOT;
    assert(key(&st, VQEAF_PHONE_KEY_ENTER) == VQEAF_PHONE_ACTION_FASTBOOT);
    st.screen = VQEAF_PHONE_HOME;
    st.sel = VQEAF_APP_LOCK;
    assert(key(&st, VQEAF_PHONE_KEY_ENTER) == VQEAF_PHONE_ACTION_LOCK);
    assert(st.screen == VQEAF_PHONE_LOCK);
    key(&st, VQEAF_PHONE_KEY_ENTER);
    key(&st, VQEAF_PHONE_KEY_LOCK);
    assert(st.screen == VQEAF_PHONE_LOCK);

    /* The iOS-style home used on the 720x1280 panel is a 4x2 grid: rows are
     * 4 wide and DOWN lands on index+4. */
    {
        VqeafPhoneState g;
        vqeaf_phone_init(&g, 5);
        assert(g.cols == 4);
        key(&g, VQEAF_PHONE_KEY_ENTER);
        key(&g, VQEAF_PHONE_KEY_RIGHT); assert(g.sel == 1);
        key(&g, VQEAF_PHONE_KEY_DOWN); assert(g.sel == 5);
        key(&g, VQEAF_PHONE_KEY_LEFT); assert(g.sel == 4);
        key(&g, VQEAF_PHONE_KEY_RIGHT);
        key(&g, VQEAF_PHONE_KEY_RIGHT);
        key(&g, VQEAF_PHONE_KEY_RIGHT); assert(g.sel == 7);
        key(&g, VQEAF_PHONE_KEY_RIGHT); assert(g.sel == 7);
        key(&g, VQEAF_PHONE_KEY_UP); assert(g.sel == 3);
        key(&g, VQEAF_PHONE_KEY_LEFT); assert(g.sel == 2);
    }

    /* Render every screen at 1280x800 and 240x320. */
    vqeaf_phone_init(&st, 3);
    render_big(&big, &st, &model, "phone_lock", 1200u);
    memcpy(lock_copy, big_px, sizeof lock_copy);
    uint32_t border = vqeaf_gfx_rgb(&big, 0x146C74);
    assert(big_px[400u * BIG_W + 415u] == border);
    key(&st, VQEAF_PHONE_KEY_ENTER);
    render_big(&big, &st, &model, "phone_home", 1500u);
    assert(frame_diff(lock_copy, big_px, BIG_W * BIG_H) > 3000u);
    memcpy(home_copy, big_px, sizeof home_copy);
    key(&st, VQEAF_PHONE_KEY_QUICK);
    render_big(&big, &st, &model, "phone_quick", 1500u);
    assert(frame_diff(home_copy, big_px, BIG_W * BIG_H) > 3000u);
    key(&st, VQEAF_PHONE_KEY_BACK);
    key(&st, VQEAF_PHONE_KEY_RIGHT);
    key(&st, VQEAF_PHONE_KEY_ENTER);
    render_big(&big, &st, &model, "phone_settings", 500u);
    key(&st, VQEAF_PHONE_KEY_BACK);
    st.sel = VQEAF_APP_STORAGE;
    key(&st, VQEAF_PHONE_KEY_ENTER);
    render_big(&big, &st, &model, "phone_storage", 300u);
    key(&st, VQEAF_PHONE_KEY_BACK);
    st.sel = VQEAF_APP_CLOCK;
    key(&st, VQEAF_PHONE_KEY_ENTER);
    render_big(&big, &st, &model, "phone_clock", 300u);
    key(&st, VQEAF_PHONE_KEY_BACK);
    st.sel = VQEAF_APP_ABOUT;
    key(&st, VQEAF_PHONE_KEY_ENTER);
    render_big(&big, &st, &model, "phone_about", 300u);

    /* The shipped reference framebuffer: 720x1280 is exactly 9:16 and the
     * shell must paint every column, with no landscape side margins. */
    {
        VqeafFramebuffer portrait;
        assert(vqeaf_gfx_setup(&portrait, (uint64_t)(uintptr_t)portrait_px,
                               PORTRAIT_W, PORTRAIT_H, PORTRAIT_W,
                               VQEAF_GFX_FMT_BGR) == 0);
        fill_model(&model, PORTRAIT_W, PORTRAIT_H);
        vqeaf_phone_init(&st, 4);
        memset(portrait_px, 0, sizeof portrait_px);
        vqeaf_phone_render(&portrait, &st, &model);
        uint32_t edge = vqeaf_gfx_rgb(&portrait, 0x146C74);
        assert(teal_pixels(portrait_px, PORTRAIT_W * PORTRAIT_H) >= 1200u);
        assert(portrait_px[400u * PORTRAIT_W] == edge);
        assert(portrait_px[400u * PORTRAIT_W + 1u] == edge);
        assert(portrait_px[400u * PORTRAIT_W + PORTRAIT_W - 1u] == edge);
        dump_ppm("screens/phone_portrait_lock.ppm", portrait_px, PORTRAIT_W,
                 PORTRAIT_H);
        memcpy(lock_copy, portrait_px, sizeof portrait_px);

        key(&st, VQEAF_PHONE_KEY_ENTER);
        memset(portrait_px, 0, sizeof portrait_px);
        vqeaf_phone_render(&portrait, &st, &model);
        assert(teal_pixels(portrait_px, PORTRAIT_W * PORTRAIT_H) >= 1500u);
        assert(frame_diff(lock_copy, portrait_px, PORTRAIT_W * PORTRAIT_H) > 3000u);
        dump_ppm("screens/phone_portrait_home.ppm", portrait_px, PORTRAIT_W,
                 PORTRAIT_H);
    }

    vqeaf_phone_init(&st, 1);
    assert(st.cols == 2);
    fill_model(&model, 240u, 320u);
    render_small(&small, &st, &model, 150u);
    key(&st, VQEAF_PHONE_KEY_ENTER);
    render_small(&small, &st, &model, 150u);
    key(&st, VQEAF_PHONE_KEY_QUICK);
    render_small(&small, &st, &model, 150u);
    key(&st, VQEAF_PHONE_KEY_BACK);
    st.sel = VQEAF_APP_STORAGE;
    key(&st, VQEAF_PHONE_KEY_ENTER);
    render_small(&small, &st, &model, 150u);

    /* An unknown clock and absent storage must render honestly, not crash. */
    VqeafPhoneModel empty;
    memset(&empty, 0, sizeof empty);
    vqeaf_phone_init(&st, 3);
    render_big(&big, &st, &empty, 0, 300u);

    /* Touch: a tap must select and activate exactly like the keys. Cell
     * centres come from the independent geometry replica, so a hit-test that
     * drifts from the renderer fails here. */
    assert(vqeaf_phone_tap(0, PORTRAIT_W, PORTRAIT_H, 10, 10) == VQEAF_PHONE_ACTION_NONE);
    {
        VqeafFramebuffer portrait;
        assert(vqeaf_gfx_setup(&portrait, (uint64_t)(uintptr_t)portrait_px,
                               PORTRAIT_W, PORTRAIT_H, PORTRAIT_W,
                               VQEAF_GFX_FMT_BGR) == 0);
        VqeafPhoneState t;
        vqeaf_phone_init(&t, 4);
        assert(tap(&t, &portrait, 360, 640) == VQEAF_PHONE_ACTION_NONE);
        assert(t.screen == VQEAF_PHONE_HOME && t.sel == 0);

        for (uint32_t i = 0; i < VQEAF_APP_COUNT; i++) {
            uint32_t cx, cy;
            home_cell_center(&portrait, 4, i, &cx, &cy);
            t.screen = VQEAF_PHONE_HOME;
            t.sel = 0;
            VqeafPhoneAction a = tap(&t, &portrait, cx, cy);
            assert(t.app == (VqeafPhoneApp)i);
            VqeafPhoneState k;
            vqeaf_phone_init(&k, 4);
            key(&k, VQEAF_PHONE_KEY_ENTER);
            k.sel = i;
            VqeafPhoneAction ka = key(&k, VQEAF_PHONE_KEY_ENTER);
            assert(a == ka && t.screen == k.screen && t.sel == k.sel);
        }

        /* Dock slots pinned to settings/storage/clock/lock. */
        t.screen = VQEAF_PHONE_HOME; t.sel = 0;
        assert(tap(&t, &portrait, 100, 1158) == VQEAF_PHONE_ACTION_NONE);
        assert(t.screen == VQEAF_PHONE_APP && t.app == VQEAF_APP_SETTINGS);
        t.screen = VQEAF_PHONE_HOME; t.sel = 0;
        assert(tap(&t, &portrait, 619, 1158) == VQEAF_PHONE_ACTION_LOCK);
        assert(t.screen == VQEAF_PHONE_LOCK);

        /* Status bar opens quick settings; rows map to actions; WiFi/BT and
         * the bottom hint behave like the keys. */
        t.screen = VQEAF_PHONE_HOME; t.sel = 0;
        assert(tap(&t, &portrait, 360, 10) == VQEAF_PHONE_ACTION_NONE);
        assert(t.screen == VQEAF_PHONE_QUICK && t.sel == 0);
        assert(tap(&t, &portrait, 360, 164) == VQEAF_PHONE_ACTION_WAKELOCK_TOGGLE);
        assert(t.screen == VQEAF_PHONE_QUICK);
        assert(tap(&t, &portrait, 360, 560) == VQEAF_PHONE_ACTION_NONE);
        assert(t.screen == VQEAF_PHONE_QUICK);
        assert(tap(&t, &portrait, 360, 1265) == VQEAF_PHONE_ACTION_NONE);
        assert(t.screen == VQEAF_PHONE_HOME);

        /* Settings rows are tappable; the back arrow returns home. */
        t.screen = VQEAF_PHONE_HOME; t.sel = VQEAF_APP_SETTINGS;
        uint32_t sx, sy;
        home_cell_center(&portrait, 4, VQEAF_APP_SETTINGS, &sx, &sy);
        tap(&t, &portrait, sx, sy);
        assert(t.screen == VQEAF_PHONE_APP && t.app == VQEAF_APP_SETTINGS);
        assert(tap(&t, &portrait, 360, 200) == VQEAF_PHONE_ACTION_TIMEOUT_NEXT);
        assert(tap(&t, &portrait, 360, 290) == VQEAF_PHONE_ACTION_WAKELOCK_TOGGLE);
        assert(tap(&t, &portrait, 360, 380) == VQEAF_PHONE_ACTION_NONE);
        assert(tap(&t, &portrait, 20, 100) == VQEAF_PHONE_ACTION_NONE);
        assert(t.screen == VQEAF_PHONE_HOME && t.sel == VQEAF_APP_SETTINGS);
    }

    /* Landscape desktop: taps outside the centred column are ignored, taps
     * inside it activate the same tile the keys would. */
    {
        VqeafPhoneState t;
        vqeaf_phone_init(&t, 3);
        key(&t, VQEAF_PHONE_KEY_ENTER);
        assert(tap(&t, &big, 100, 400) == VQEAF_PHONE_ACTION_NONE);
        assert(t.screen == VQEAF_PHONE_HOME);
        uint32_t cx, cy;
        home_cell_center(&big, 3, VQEAF_APP_ABOUT, &cx, &cy);
        assert(tap(&t, &big, cx, cy) == VQEAF_PHONE_ACTION_NONE);
        assert(t.screen == VQEAF_PHONE_APP && t.app == VQEAF_APP_ABOUT);
    }

    puts("phone shell smoke: lock/home/quick/settings/storage/clock/touch OK");
    return 0;
}
