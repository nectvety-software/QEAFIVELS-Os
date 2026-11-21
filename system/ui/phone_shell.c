#include "include/vqeaf/phone_shell.h"
#include "include/vqeaf/screen_common.h"

#define PHONE_APP_COUNT ((uint32_t)VQEAF_APP_COUNT)
#define QUICK_ROWS 7u
#define SETTINGS_ROWS 4u

static const char *const app_labels[VQEAF_APP_COUNT] = {
    "H\u1EC6 TH\u1ED0NG",     /* ABOUT */
    "C\u00C0I \u0110\u1EB6T", /* SETTINGS */
    "L\u01AFU TR\u1EEEF",     /* STORAGE */
    "C\u1EACP NH\u1EACT",     /* UPDATE */
    "NGU\u1ED2N",             /* POWER */
    "FASTBOOT",
    "\u0110\u1ED2NG H\u1ED2", /* CLOCK */
    "KH\u00D3A"               /* LOCK */
};
static const VqeafIcon app_icons[VQEAF_APP_COUNT] = {
    VQEAF_ICON_INFO, VQEAF_ICON_GEAR, VQEAF_ICON_STORAGE, VQEAF_ICON_UPDATE,
    VQEAF_ICON_POWER, VQEAF_ICON_BOLT, VQEAF_ICON_CLOCK, VQEAF_ICON_LOCK
};
static const char *const timeout_labels[5] = {
    "15 GI\u00C2Y", "1 PH\u00DAT", "5 PH\u00DAT", "30 PH\u00DAT", "KH\u00D4NG"
};

static void append(char *d, uint32_t *n, uint32_t cap, const char *s) {
    while (*s && *n + 1u < cap) d[(*n)++] = *s++;
    d[*n] = 0;
}
static void append_u64(char *d, uint32_t *n, uint32_t cap, uint64_t v) {
    char tmp[21];
    uint32_t k = 0;
    if (!v) { append(d, n, cap, "0"); return; }
    while (v && k < 20) { tmp[k++] = (char)('0' + v % 10u); v /= 10u; }
    while (k && *n + 1u < cap) d[(*n)++] = tmp[--k];
    d[*n] = 0;
}
static void append_pad2(char *d, uint32_t *n, uint32_t cap, uint32_t v) {
    if (*n + 2u < cap) {
        d[(*n)++] = (char)('0' + (v / 10u) % 10u);
        d[(*n)++] = (char)('0' + v % 10u);
    }
    d[*n < cap ? *n : cap - 1u] = 0;
}
static void clock_text(char *out, uint32_t cap, const VqeafPhoneModel *m) {
    uint32_t n = 0;
    out[0] = 0;
    if (!m->clock_valid) { append(out, &n, cap, "--:--"); return; }
    append_pad2(out, &n, cap, m->hour % 24u);
    append(out, &n, cap, ":");
    append_pad2(out, &n, cap, m->minute % 60u);
}
static void date_text(char *out, uint32_t cap, const VqeafPhoneModel *m) {
    uint32_t n = 0;
    out[0] = 0;
    if (!m->clock_valid) { append(out, &n, cap, "--/--/----"); return; }
    append_pad2(out, &n, cap, m->day % 100u);
    append(out, &n, cap, "/");
    append_pad2(out, &n, cap, m->month % 100u);
    append(out, &n, cap, "/");
    append_u64(out, &n, cap, m->year % 10000u);
}
static void uptime_text(char *out, uint32_t cap, uint64_t ticks) {
    uint64_t sec = ticks / 100u;
    uint32_t n = 0;
    out[0] = 0;
    append_u64(out, &n, cap, sec / 60u);
    append(out, &n, cap, ":");
    uint64_t s = sec % 60u;
    if (s < 10u) append(out, &n, cap, "0");
    append_u64(out, &n, cap, s);
}

void vqeaf_phone_init(VqeafPhoneState *st, uint32_t cols) {
    if (!st) return;
    st->screen = VQEAF_PHONE_LOCK;
    st->app = VQEAF_APP_ABOUT;
    st->sel = 0;
    st->cols = cols < 2u ? 2u : cols > 4u ? 4u : cols;
    st->previous = VQEAF_PHONE_HOME;
    st->previous_sel = 0;
}

uint32_t vqeaf_phone_app_count(void) { return PHONE_APP_COUNT; }
const char *vqeaf_phone_app_label(uint32_t index) {
    return index < PHONE_APP_COUNT ? app_labels[index] : "";
}
const char *vqeaf_phone_timeout_label(uint32_t index) {
    return index < 5u ? timeout_labels[index] : timeout_labels[4];
}

static void move_list(uint32_t *sel, uint32_t count, int delta) {
    int v = (int)*sel + delta;
    if (v < 0) v = 0;
    if (v >= (int)count) v = (int)count - 1;
    *sel = (uint32_t)v;
}

static void move_grid(VqeafPhoneState *st, VqeafPhoneKey key) {
    uint32_t cols = st->cols ? st->cols : 3u;
    uint32_t row = st->sel / cols, col = st->sel % cols;
    if (st->sel >= PHONE_APP_COUNT) { st->sel = 0; return; }
    if (key == VQEAF_PHONE_KEY_LEFT && col > 0u) st->sel--;
    else if (key == VQEAF_PHONE_KEY_RIGHT && col + 1u < cols &&
             st->sel + 1u < PHONE_APP_COUNT) st->sel++;
    else if (key == VQEAF_PHONE_KEY_UP && row > 0u) st->sel -= cols;
    else if (key == VQEAF_PHONE_KEY_DOWN && st->sel + cols < PHONE_APP_COUNT)
        st->sel += cols;
}

static VqeafPhoneAction activate_home(VqeafPhoneState *st) {
    if (st->sel >= PHONE_APP_COUNT) { st->sel = 0; return VQEAF_PHONE_ACTION_NONE; }
    st->app = (VqeafPhoneApp)st->sel;
    switch (st->app) {
    case VQEAF_APP_UPDATE: return VQEAF_PHONE_ACTION_UPDATE;
    case VQEAF_APP_POWER: return VQEAF_PHONE_ACTION_POWER_MENU;
    case VQEAF_APP_FASTBOOT: return VQEAF_PHONE_ACTION_FASTBOOT;
    case VQEAF_APP_LOCK:
        st->screen = VQEAF_PHONE_LOCK;
        st->sel = 0;
        return VQEAF_PHONE_ACTION_LOCK;
    default:
        st->screen = VQEAF_PHONE_APP;
        st->sel = 0;
        return VQEAF_PHONE_ACTION_NONE;
    }
}

static VqeafPhoneAction activate_quick(VqeafPhoneState *st) {
    uint32_t i = st->sel;
    if (i > QUICK_ROWS - 1u) i = 0;
    switch (i) {
    case 0: return VQEAF_PHONE_ACTION_WAKELOCK_TOGGLE;
    case 1: return VQEAF_PHONE_ACTION_TIMEOUT_NEXT;
    case 2: return VQEAF_PHONE_ACTION_SLEEP;
    case 3:
        st->screen = VQEAF_PHONE_LOCK;
        st->sel = 0;
        return VQEAF_PHONE_ACTION_LOCK;
    case 4: return VQEAF_PHONE_ACTION_POWER_MENU;
    default: return VQEAF_PHONE_ACTION_NONE;
    }
}

static VqeafPhoneAction activate_app(VqeafPhoneState *st) {
    if (st->app == VQEAF_APP_SETTINGS) {
        if (st->sel == 0u) return VQEAF_PHONE_ACTION_TIMEOUT_NEXT;
        if (st->sel == 1u) return VQEAF_PHONE_ACTION_WAKELOCK_TOGGLE;
    }
    return VQEAF_PHONE_ACTION_NONE;
}

VqeafPhoneAction vqeaf_phone_key(VqeafPhoneState *st, VqeafPhoneKey key) {
    if (!st || key == VQEAF_PHONE_KEY_NONE) return VQEAF_PHONE_ACTION_NONE;
    if (key == VQEAF_PHONE_KEY_LOCK) {
        st->screen = VQEAF_PHONE_LOCK;
        st->sel = 0;
        return VQEAF_PHONE_ACTION_LOCK;
    }
    switch (st->screen) {
    case VQEAF_PHONE_LOCK:
        if (key == VQEAF_PHONE_KEY_ENTER) {
            st->screen = VQEAF_PHONE_HOME;
            st->sel = 0;
        }
        return VQEAF_PHONE_ACTION_NONE;
    case VQEAF_PHONE_HOME:
        if (key == VQEAF_PHONE_KEY_ENTER) return activate_home(st);
        if (key == VQEAF_PHONE_KEY_BACK) {
            st->screen = VQEAF_PHONE_LOCK;
            st->sel = 0;
            return VQEAF_PHONE_ACTION_LOCK;
        }
        if (key == VQEAF_PHONE_KEY_QUICK) {
            st->previous = VQEAF_PHONE_HOME;
            st->previous_sel = st->sel;
            st->screen = VQEAF_PHONE_QUICK;
            st->sel = 0;
            return VQEAF_PHONE_ACTION_NONE;
        }
        move_grid(st, key);
        return VQEAF_PHONE_ACTION_NONE;
    case VQEAF_PHONE_QUICK:
        if (key == VQEAF_PHONE_KEY_BACK) {
            st->screen = st->previous;
            st->sel = st->previous_sel;
            return VQEAF_PHONE_ACTION_NONE;
        }
        if (key == VQEAF_PHONE_KEY_QUICK) {
            st->screen = st->previous;
            st->sel = st->previous_sel;
            return VQEAF_PHONE_ACTION_NONE;
        }
        if (key == VQEAF_PHONE_KEY_ENTER) return activate_quick(st);
        if (key == VQEAF_PHONE_KEY_UP) move_list(&st->sel, QUICK_ROWS, -1);
        else if (key == VQEAF_PHONE_KEY_DOWN) move_list(&st->sel, QUICK_ROWS, 1);
        return VQEAF_PHONE_ACTION_NONE;
    case VQEAF_PHONE_APP:
        if (key == VQEAF_PHONE_KEY_BACK) {
            st->sel = (uint32_t)st->app;
            st->screen = VQEAF_PHONE_HOME;
            return VQEAF_PHONE_ACTION_NONE;
        }
        if (key == VQEAF_PHONE_KEY_QUICK) {
            st->previous = VQEAF_PHONE_APP;
            st->previous_sel = st->sel;
            st->screen = VQEAF_PHONE_QUICK;
            st->sel = 0;
            return VQEAF_PHONE_ACTION_NONE;
        }
        if (key == VQEAF_PHONE_KEY_ENTER) return activate_app(st);
        if (st->app == VQEAF_APP_SETTINGS) {
            if (key == VQEAF_PHONE_KEY_UP) move_list(&st->sel, SETTINGS_ROWS, -1);
            else if (key == VQEAF_PHONE_KEY_DOWN) move_list(&st->sel, SETTINGS_ROWS, 1);
        }
        return VQEAF_PHONE_ACTION_NONE;
    }
    return VQEAF_PHONE_ACTION_NONE;
}

static void draw_status_bar(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                            const VqeafPalette *p, const VqeafPhoneModel *m) {
    uint32_t s = f->scale, h = 30u * s;
    char buf[24];
    uint32_t n = 0;
    vqeaf_gfx_rect(fb, f->x, 0, f->w, h, p->bar);
    clock_text(buf, sizeof buf, m);
    vqeaf_gfx_text(fb, f->x + 16u * s, (h - 16u) / 2u, buf, 1u, p->text);
    if (m->battery_present) {
        uint32_t pct = m->battery_permille / 10u;
        if (pct > 100u) pct = 100u;
        n = 0; buf[0] = 0;
        append_u64(buf, &n, sizeof buf, pct);
        append(buf, &n, sizeof buf, "%");
        uint32_t bw = 22u * s, bh = 10u * s;
        uint32_t bx = f->x + f->w - 14u * s - bw, by = (h - bh) / 2u;
        vqeaf_gfx_border(fb, bx, by, bw, bh, 1u, p->dim);
        vqeaf_gfx_rect(fb, bx + bw, by + bh / 3u, 2u * s, bh / 3u, p->dim);
        uint32_t fill = (bw - 4u) * pct / 100u;
        if (fill) vqeaf_gfx_rect(fb, bx + 2u, by + 2u, fill, bh - 4u,
                                 m->charging ? p->accent : p->accent_dim);
        uint32_t tw = vqeaf_gfx_text_width(buf, 1u);
        vqeaf_gfx_text(fb, bx - tw - 8u * s, (h - 16u) / 2u, buf, 1u, p->dim);
    } else {
        const char *tag = "VQEAF";
        uint32_t tw = vqeaf_gfx_text_width(tag, 1u);
        vqeaf_gfx_text(fb, f->x + f->w - tw - 12u * s, (h - 16u) / 2u, tag, 1u, p->dim);
    }
    vqeaf_gfx_rect(fb, f->x, h, f->w, 1u, p->accent_dim);
}

static void draw_waves(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                       const VqeafPalette *p) {
    uint32_t s = f->scale;
    uint32_t baseline = fb->height > 24u * s ? fb->height - 24u * s : 0u;
    vqeaf_screen_waves_region(fb, f->x, f->w, baseline, p);
}

static void draw_lock(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                      const VqeafPalette *p, const VqeafPhoneModel *m) {
    uint32_t s = f->scale;
    uint32_t cx = f->x + f->w / 2u;
    uint32_t r = 26u * s;
    if (r > f->w / 6u) r = f->w / 6u;
    uint32_t cy = fb->height * 22u / 100u;
    char buf[24];
    vqeaf_screen_badge(fb, cx, cy, r, 2, p);
    clock_text(buf, sizeof buf, m);
    uint32_t scale = s >= 2u ? 5u : 3u;
    uint32_t y = cy + r + 16u * s;
    vqeaf_gfx_text_centered(fb, y, buf, scale, p->text);
    y += 16u * scale + 10u * s;
    date_text(buf, sizeof buf, m);
    vqeaf_gfx_text_centered(fb, y, buf, s, p->dim);
    y += 16u * s + 6u * s;
    if (m->clock_valid)
        vqeaf_gfx_text_centered(fb, y, "CMOS RTC", 1u, p->dim);
    y = fb->height > 110u * s ? fb->height - 110u * s : y + 24u * s;
    vqeaf_gfx_text_centered(fb, y, "NH\u1EA4N E \u0110\u1EC2 M\u1EDE KH\u00D3A",
                            1u, p->accent);
    draw_waves(fb, f, p);
    y = fb->height > 56u * s ? fb->height - 56u * s : y + 20u;
    vqeaf_gfx_text_centered(fb, y, "WASD DI CHUY\u1EC2N  E CH\u1ECCN", 1u, p->dim);
}

/* QeafdropOs reference home: vertical wash, a 4-column icon grid with the
 * label under each icon, and a glass dock above the home indicator. */
static uint32_t wash_rgb(const VqeafFramebuffer *fb, uint32_t top, uint32_t bottom,
                         uint32_t t, uint32_t span) {
    int32_t r0 = (int32_t)((top >> 16) & 255u), r1 = (int32_t)((bottom >> 16) & 255u);
    int32_t g0 = (int32_t)((top >> 8) & 255u), g1 = (int32_t)((bottom >> 8) & 255u);
    int32_t b0 = (int32_t)(top & 255u), b1 = (int32_t)(bottom & 255u);
    int32_t r = r0 + (r1 - r0) * (int32_t)t / (int32_t)span;
    int32_t g = g0 + (g1 - g0) * (int32_t)t / (int32_t)span;
    int32_t b = b0 + (b1 - b0) * (int32_t)t / (int32_t)span;
    return vqeaf_gfx_rgb(fb, ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
}

static void draw_home_backdrop(const VqeafFramebuffer *fb, const VqeafScreenFrame *f) {
    uint32_t span = fb->height > 1u ? fb->height - 1u : 1u;
    uint32_t top = 30u * f->scale;
    if (top >= fb->height) return;
    for (uint32_t y = top; y < fb->height; y++)
        vqeaf_gfx_rect(fb, f->x, y, f->w, 1u,
                       wash_rgb(fb, 0x050B12u, 0x0B2230u, y, span));
}

/* Shared home geometry: the renderer and the touch hit-test must agree on
 * every cell, so both read these numbers from one place. */
typedef struct {
    uint32_t s, cols, rows, gap;
    int32_t icon;
    uint32_t label_gap, label_part;
    uint32_t wide, dock_h, dock_x, dock_w, dock_y;
    uint32_t bar_h, bar_y, top, row_gap, bottom;
    uint32_t grid_w, x0, cell_h;
} HomeGeom;

static HomeGeom home_geom(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                          const VqeafPhoneState *st) {
    HomeGeom g;
    uint32_t s = f->scale;
    g.s = s;
    uint32_t cols = st->cols ? st->cols : 3u;
    if (cols > 4u) cols = 4u;
    g.cols = cols;
    g.rows = (PHONE_APP_COUNT + cols - 1u) / cols;
    uint32_t margin = 25u * s, gap = 30u * s;
    g.gap = gap;
    int32_t icon = (int32_t)((f->w - 2u * margin - (cols - 1u) * gap) / cols);
    uint32_t cap = 56u * s;
    if (icon > (int32_t)cap) icon = (int32_t)cap;
    if (icon < 12) icon = 12;
    g.label_gap = 4u * s;
    g.label_part = g.label_gap + 16u;
    g.wide = fb->height >= 560u ? 1u : 0u;
    g.dock_h = g.wide ? 74u * s : 0u;
    g.bar_h = 4u * s;
    g.bar_y = fb->height > 16u * s ? fb->height - 16u * s : 0u;
    g.dock_y = (g.wide && g.bar_y > 8u * s + g.dock_h)
                   ? g.bar_y - 8u * s - g.dock_h : 0u;
    g.top = 30u * s + (g.wide ? 42u * s : 8u * s);
    g.row_gap = g.wide ? 26u * s : 8u * s;
    g.bottom = g.dock_y ? g.dock_y - 8u * s
                        : (fb->height > 52u * s ? fb->height - 52u * s : g.top);
    if (g.bottom > g.top) {
        uint32_t avail = g.bottom - g.top;
        uint32_t fixed = g.rows * g.label_part + (g.rows - 1u) * g.row_gap;
        if (avail > fixed) {
            uint32_t by_h = (avail - fixed) / g.rows;
            if (icon > (int32_t)by_h) icon = (int32_t)by_h;
            if (icon < 12) icon = 12;
        }
    }
    g.icon = icon;
    g.grid_w = cols * (uint32_t)icon + (cols - 1u) * gap;
    if (g.grid_w > f->w) g.grid_w = f->w;
    g.x0 = f->x + (f->w > g.grid_w ? (f->w - g.grid_w) / 2u : 0u);
    g.cell_h = (uint32_t)icon + g.label_part;
    g.dock_x = f->x + 7u * s;
    g.dock_w = f->w > 14u * s ? f->w - 14u * s : 0u;
    return g;
}

static void draw_home(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                      const VqeafPalette *p, const VqeafPhoneState *st,
                      const VqeafPhoneModel *m) {
    (void)m;
    HomeGeom g = home_geom(fb, f, st);
    uint32_t s = g.s;
    uint32_t cols = g.cols, gap = g.gap;
    int32_t icon = g.icon;
    uint32_t label_gap = g.label_gap;
    uint32_t wide = g.wide, dock_h = g.dock_h;
    uint32_t bar_h = g.bar_h, bar_y = g.bar_y, dock_y = g.dock_y;
    uint32_t top = g.top, row_gap = g.row_gap;
    uint32_t x0 = g.x0, cell_h = g.cell_h;
    draw_home_backdrop(fb, f);
    uint32_t glyph = (uint32_t)icon * 73u / 100u;
    uint32_t radius = (uint32_t)icon * 22u / 100u;
    if (!radius) radius = 1u;
    for (uint32_t i = 0; i < PHONE_APP_COUNT; i++) {
        uint32_t row = i / cols, col = i % cols;
        uint32_t x = x0 + col * ((uint32_t)icon + gap);
        uint32_t iy = top + row * (cell_h + row_gap);
        if (iy + cell_h > fb->height) break;
        int sel = (int)i == (int)st->sel;
        if (sel)
            vqeaf_gfx_round_rect(fb, x - 2u, iy - 2u, (uint32_t)icon + 4u,
                                 (uint32_t)icon + 4u, radius + 2u, p->accent);
        vqeaf_gfx_round_rect(fb, x, iy, (uint32_t)icon, (uint32_t)icon, radius,
                             sel ? p->panel_hi : p->panel);
        vqeaf_screen_icon(fb, x + ((uint32_t)icon - glyph) / 2u,
                          iy + ((uint32_t)icon - glyph) / 2u, glyph, app_icons[i],
                          sel ? p->accent : p->text);
        uint32_t lw = vqeaf_gfx_text_width(app_labels[i], 1u);
        if (lw + 8u <= (uint32_t)icon + gap) {
            int32_t lx = (int32_t)x + ((int32_t)icon - (int32_t)lw) / 2;
            if (lx < (int32_t)f->x) lx = (int32_t)f->x;
            vqeaf_gfx_text(fb, (uint32_t)lx,
                           iy + (uint32_t)icon + label_gap, app_labels[i], 1u,
                           sel ? p->text : p->dim);
        }
    }
    if (wide && dock_h) {
        static const VqeafIcon dock_icons[4] = {
            VQEAF_ICON_GEAR, VQEAF_ICON_STORAGE, VQEAF_ICON_CLOCK, VQEAF_ICON_LOCK
        };
        uint32_t dock_x = f->x + 7u * s, dock_w = f->w - 14u * s;
        uint32_t dock_r = 30u * s;
        if (dock_r > dock_h / 2u) dock_r = dock_h / 2u;
        vqeaf_gfx_round_rect(fb, dock_x - 1u, dock_y - 1u, dock_w + 2u,
                             dock_h + 2u, dock_r + 1u, p->accent_dim);
        vqeaf_gfx_round_rect(fb, dock_x, dock_y, dock_w, dock_h, dock_r, p->bar);
        uint32_t dglyph = 40u * s;
        for (uint32_t i = 0; i < 4u; i++) {
            uint32_t cx = dock_x + dock_w * (2u * i + 1u) / 8u;
            vqeaf_screen_icon(fb, cx - dglyph / 2u,
                              dock_y + (dock_h - dglyph) / 2u, dglyph,
                              dock_icons[i], p->text);
        }
        uint32_t hint_y = dock_y > 8u * s + 16u ? dock_y - 8u * s - 16u : dock_y;
        const char *hint =
            "WASD DI CHUY\u1EC2N  E CH\u1ECCN  Q C\u00C0I \u0110\u1EB6T  P NGU\u1ED3N";
        if (vqeaf_gfx_text_width(hint, 1u) > f->w) hint = "WASD/E/Q/P";
        vqeaf_gfx_text_centered(fb, hint_y, hint, 1u, p->dim);
        if (bar_y && bar_y + bar_h <= fb->height) {
            uint32_t bar_w = 124u * s;
            if (bar_w > f->w) bar_w = f->w;
            vqeaf_gfx_round_rect(fb, f->x + (f->w - bar_w) / 2u, bar_y, bar_w,
                                 bar_h, bar_h / 2u, p->dim);
        }
    } else {
        uint32_t fy = fb->height > 30u * s ? fb->height - 30u * s : top;
        vqeaf_gfx_text_centered(fb, fy, "WASD/E/Q/P", 1u, p->dim);
    }
}

static void draw_quick(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                       const VqeafPalette *p, const VqeafPhoneState *st,
                       const VqeafPhoneModel *m) {
    static const VqeafIcon icons[QUICK_ROWS] = {
        VQEAF_ICON_KEEP_AWAKE, VQEAF_ICON_TIMER, VQEAF_ICON_DISPLAY,
        VQEAF_ICON_LOCK, VQEAF_ICON_POWER, VQEAF_ICON_WIFI, VQEAF_ICON_BLUETOOTH
    };
    static const char *const labels[QUICK_ROWS] = {
        "GI\u1EEEF S\u00C1NG", "H\u1EAET T\u1EAET",
        "T\u1EAET M\u00C0N H\u00CCNH", "KH\u00D3A M\u00C1Y",
        "MENU NGU\u1ED2N", "WIFI", "BLUETOOTH"
    };
    uint32_t s = f->scale;
    uint32_t y = 30u * s + 8u * s;
    vqeaf_gfx_text(fb, f->x + 16u * s, y, "C\u00C0I \u0110\u1EB6T NHANH",
                   s >= 2u ? 2u : 1u, p->text);
    y += 16u * (s >= 2u ? 2u : 1u) + 10u * s;
    uint32_t bottom = fb->height > (s >= 2u ? 64u : 42u)
                          ? fb->height - (s >= 2u ? 64u : 42u) : y + 20u;
    uint32_t gap = 4u * s;
    uint32_t avail = bottom > y ? bottom - y : 20u;
    uint32_t row_h = avail > (QUICK_ROWS - 1u) * gap
                         ? (avail - (QUICK_ROWS - 1u) * gap) / QUICK_ROWS : 18u;
    if (row_h > 36u * s) row_h = 36u * s;
    if (row_h < 18u) row_h = 18u;
    for (uint32_t i = 0; i < QUICK_ROWS; i++) {
        const char *status = 0;
        if (i == 0u) status = m->keep_awake ? "B\u1EACT" : "T\u1EAET";
        else if (i == 1u) status = vqeaf_phone_timeout_label(m->timeout_index);
        else if (i == 5u) status = "KH\u00D4NG H\u1ED6 TR\u1EE2";
        else if (i == 6u) status = "KH\u00D4NG";
        uint32_t ry = y + i * (row_h + gap);
        if (ry + row_h > fb->height) break;
        vqeaf_screen_menu_row_h(fb, f, p, ry, row_h, icons[i], labels[i], status,
                                (int)i == (int)st->sel, i < 5u);
    }
    vqeaf_gfx_text_centered(fb, fb->height > 30u * s ? fb->height - 30u * s : y,
                            "B QUAY L\u1EA0I", 1u, p->dim);
}

static void draw_line(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                      const VqeafPalette *p, uint32_t *y, const char *text,
                      uint32_t color) {
    (void)p;
    vqeaf_gfx_text(fb, f->x + 16u * f->scale, *y, text, 1u, color);
    *y += 22u;
}

static void draw_about(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                       const VqeafPalette *p, const VqeafPhoneModel *m,
                       uint32_t y) {
    char buf[64];
    uint32_t n;
    draw_line(fb, f, p, &y, "VQEAF OS v0.3.0", p->text);
    draw_line(fb, f, p, &y, "NH\u00C2N x86_64 LONG MODE", p->dim);
    n = 0; buf[0] = 0;
    append(buf, &n, sizeof buf, "T\u00C1C V\u1EE5: ");
    append_u64(buf, &n, sizeof buf, m->task_count);
    append(buf, &n, sizeof buf, "  PIT 100 HZ");
    draw_line(fb, f, p, &y, buf, p->dim);
    n = 0; buf[0] = 0;
    append(buf, &n, sizeof buf, "B\u1ED8 NH\u1EDA: ");
    append_u64(buf, &n, sizeof buf, m->free_pages);
    append(buf, &n, sizeof buf, " TRANG");
    draw_line(fb, f, p, &y, buf, p->dim);
    n = 0; buf[0] = 0;
    append(buf, &n, sizeof buf, "M\u00C0N H\u00CCNH: ");
    append_u64(buf, &n, sizeof buf, m->fb_width);
    append(buf, &n, sizeof buf, "x");
    append_u64(buf, &n, sizeof buf, m->fb_height);
    append(buf, &n, sizeof buf, " GOP");
    draw_line(fb, f, p, &y, buf, p->dim);
    n = 0; buf[0] = 0;
    append(buf, &n, sizeof buf, "HO\u1EA0T \u0110\u1ED9NG: ");
    uptime_text(buf + n, sizeof buf - n, m->uptime_ticks);
    draw_line(fb, f, p, &y, buf, p->dim);
    draw_line(fb, f, p, &y, "PIN: KH\u00D4NG C\u00D3 C\u1EA2M BI\u1EBEN", p->dim);
    draw_line(fb, f, p, &y, "WIFI/BT: KH\u00D4NG H\u1ED6 TR\u1EE2", p->dim);
    draw_line(fb, f, p, &y, "NH\u1EACC: SERIAL COM1", p->dim);
}

static void draw_settings(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                          const VqeafPalette *p, const VqeafPhoneState *st,
                          const VqeafPhoneModel *m) {
    static const VqeafIcon icons[SETTINGS_ROWS] = {
        VQEAF_ICON_TIMER, VQEAF_ICON_KEEP_AWAKE, VQEAF_ICON_WIFI,
        VQEAF_ICON_BLUETOOTH
    };
    static const char *const labels[SETTINGS_ROWS] = {
        "H\u1EAET T\u1EAET", "GI\u1EEEF S\u00C1NG", "WIFI", "BLUETOOTH"
    };
    uint32_t s = f->scale;
    uint32_t y = 30u * s + 50u * s;
    uint32_t row_h = 40u * s;
    uint32_t gap = 6u * s;
    for (uint32_t i = 0; i < SETTINGS_ROWS; i++) {
        const char *status = 0;
        if (i == 0u) status = vqeaf_phone_timeout_label(m->timeout_index);
        else if (i == 1u) status = m->keep_awake ? "B\u1EACT" : "T\u1EAET";
        else if (i == 2u) status = "KH\u00D4NG H\u1ED6 TR\u1EE2";
        else status = "KH\u00D4NG";
        if (y + row_h > fb->height) break;
        vqeaf_screen_menu_row_h(fb, f, p, y, row_h, icons[i], labels[i], status,
                                (int)i == (int)st->sel, i < 2u);
        y += row_h + gap;
    }
}

static void draw_storage(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                         const VqeafPalette *p, const VqeafPhoneModel *m,
                         uint32_t y) {
    char buf[64];
    uint32_t n;
    if (!m->storage_mounted) {
        draw_line(fb, f, p, &y, "CH\u01AFA G\u1EAEN KHO L\u01AFU TR\u1EEEF", p->text);
        draw_line(fb, f, p, &y, "KH\u00D4NG C\u00D3 THI\u1EBET B\u1ECB", p->dim);
        return;
    }
    n = 0; buf[0] = 0;
    append(buf, &n, sizeof buf, "G\u1EA6N: ");
    append(buf, &n, sizeof buf, m->storage_path ? m->storage_path : "?");
    draw_line(fb, f, p, &y, buf, p->text);
    n = 0; buf[0] = 0;
    append(buf, &n, sizeof buf, "T\u1EC6P: ");
    append(buf, &n, sizeof buf, m->storage_file ? m->storage_file : "?");
    draw_line(fb, f, p, &y, buf, p->dim);
    n = 0; buf[0] = 0;
    append(buf, &n, sizeof buf, "DUNG L\u01AF\u1EE2NG: ");
    append_u64(buf, &n, sizeof buf, m->storage_sectors / 2048u);
    append(buf, &n, sizeof buf, " MiB");
    draw_line(fb, f, p, &y, buf, p->dim);
    n = 0; buf[0] = 0;
    append(buf, &n, sizeof buf, "SECTOR: ");
    append_u64(buf, &n, sizeof buf, m->storage_sectors);
    draw_line(fb, f, p, &y, buf, p->dim);
    draw_line(fb, f, p, &y, "CH\u1EBE \u0110\u1ED8: CH\u1EC8 \u0110\u1ECCC", p->dim);
    draw_line(fb, f, p, &y, "N\u1ED8I DUNG:", p->dim);
    if (m->storage_summary && *m->storage_summary) {
        uint32_t len = 0;
        while (m->storage_summary[len] && len < 32u) len++;
        uint32_t keep = len < sizeof buf - 1u ? len : sizeof buf - 1u;
        for (uint32_t i = 0; i < keep; i++) buf[i] = m->storage_summary[i];
        buf[keep] = 0;
        draw_line(fb, f, p, &y, buf, p->accent);
    }
}

static void draw_clock_app(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                           const VqeafPalette *p, const VqeafPhoneModel *m) {
    uint32_t s = f->scale;
    char buf[24];
    clock_text(buf, sizeof buf, m);
    uint32_t scale = s >= 2u ? 5u : 3u;
    uint32_t y = fb->height * 30u / 100u;
    vqeaf_gfx_text_centered(fb, y, buf, scale, p->text);
    y += 16u * scale + 14u * s;
    date_text(buf, sizeof buf, m);
    vqeaf_gfx_text_centered(fb, y, buf, s, p->dim);
    y += 16u * s + 12u * s;
    uint32_t lw = f->w / 3u;
    vqeaf_gfx_rect(fb, f->x + (f->w - lw) / 2u, y, lw, 2u * s, p->accent);
    y += 14u * s;
    vqeaf_gfx_text_centered(fb, y, m->clock_valid ? "CMOS RTC"
                                                  : "RTC KH\u00D4NG \u0110\u1ECCC",
                            1u, m->clock_valid ? p->accent : p->danger);
    y += 24u;
    vqeaf_gfx_text_centered(fb, y, "GI\u1EDC PH\u1EA6N C\u1EE8NG", 1u, p->dim);
    y += 20u;
    vqeaf_gfx_text_centered(fb, y, "KH\u00D4NG \u0110\u1ED2NG B\u1ED8 M\u1EA0NG", 1u,
                            p->dim);
}

static void draw_app(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                     const VqeafPalette *p, const VqeafPhoneState *st,
                     const VqeafPhoneModel *m) {
    uint32_t s = f->scale;
    uint32_t by = 30u * s + 10u * s;
    uint32_t bx = f->x + 16u * s;
    uint32_t k = 6u * s;
    vqeaf_gfx_line(fb, bx + k, by + 12u * s - k, bx, by + 12u * s, p->accent);
    vqeaf_gfx_line(fb, bx, by + 12u * s, bx + k, by + 12u * s + k, p->accent);
    vqeaf_gfx_line(fb, bx + k, by + 12u * s - k + 1u, bx, by + 12u * s + 1u, p->accent);
    vqeaf_gfx_line(fb, bx, by + 12u * s + 1u, bx + k, by + 12u * s + k + 1u, p->accent);
    const char *title = vqeaf_phone_app_label((uint32_t)st->app);
    uint32_t title_h = 16u * (s >= 2u ? 2u : 1u);
    vqeaf_gfx_text(fb, f->x + 40u * s, by, title, s >= 2u ? 2u : 1u, p->text);
    vqeaf_gfx_rect(fb, f->x + 16u * s, by + title_h + 6u * s, f->w - 32u * s, 2u,
                   p->accent);
    uint32_t y = 30u * s + 52u * s;
    switch (st->app) {
    case VQEAF_APP_ABOUT: draw_about(fb, f, p, m, y); break;
    case VQEAF_APP_SETTINGS: draw_settings(fb, f, p, st, m); break;
    case VQEAF_APP_STORAGE: draw_storage(fb, f, p, m, y); break;
    case VQEAF_APP_CLOCK: draw_clock_app(fb, f, p, m); break;
    default:
        vqeaf_gfx_text(fb, f->x + 16u * s, y, "KH\u00D4NG C\u00D3 M\u00C0N H\u00CCNH",
                       1u, p->dim);
        break;
    }
    vqeaf_gfx_text_centered(fb, fb->height > 32u * s ? fb->height - 32u * s : y,
                            "B QUAY L\u1EA0I  Q NHANH", 1u, p->dim);
}

void vqeaf_phone_render(const VqeafFramebuffer *fb, const VqeafPhoneState *st,
                        const VqeafPhoneModel *model) {
    if (!fb || !fb->pixels || !fb->width || !fb->height || !st) return;
    VqeafPhoneModel empty = {0};
    if (!model) model = &empty;
    VqeafScreenFrame f = vqeaf_screen_frame(fb);
    VqeafPalette p = vqeaf_screen_palette(fb);
    vqeaf_gfx_clear(fb, vqeaf_gfx_rgb(fb, 0x010409));
    vqeaf_gfx_rect(fb, f.x, 0, f.w, fb->height, p.bg);
    draw_status_bar(fb, &f, &p, model);
    switch (st->screen) {
    case VQEAF_PHONE_LOCK: draw_lock(fb, &f, &p, model); break;
    case VQEAF_PHONE_HOME: draw_home(fb, &f, &p, st, model); break;
    case VQEAF_PHONE_QUICK: draw_quick(fb, &f, &p, st, model); break;
    case VQEAF_PHONE_APP: draw_app(fb, &f, &p, st, model); break;
    }
    vqeaf_gfx_border(fb, f.x, 0, f.w, fb->height, 2u, p.accent_dim);
}

VqeafPhoneAction vqeaf_phone_tap(VqeafPhoneState *st, uint32_t fb_w, uint32_t fb_h,
                                 uint32_t x, uint32_t y) {
    if (!st || !fb_w || !fb_h) return VQEAF_PHONE_ACTION_NONE;
    VqeafFramebuffer fb = {0};
    fb.width = fb_w;
    fb.height = fb_h;
    fb.stride = fb_w;
    VqeafScreenFrame f = vqeaf_screen_frame(&fb);
    uint32_t s = f.scale;
    if (x < f.x || x >= f.x + f.w) return VQEAF_PHONE_ACTION_NONE;

    switch (st->screen) {
    case VQEAF_PHONE_LOCK:
        st->screen = VQEAF_PHONE_HOME;
        st->sel = 0;
        return VQEAF_PHONE_ACTION_NONE;
    case VQEAF_PHONE_HOME: {
        if (y < 30u * s) {
            st->previous = VQEAF_PHONE_HOME;
            st->previous_sel = st->sel;
            st->screen = VQEAF_PHONE_QUICK;
            st->sel = 0;
            return VQEAF_PHONE_ACTION_NONE;
        }
        HomeGeom g = home_geom(&fb, &f, st);
        if (g.wide && g.dock_h && g.dock_w && y >= g.dock_y &&
            y < g.dock_y + g.dock_h && x >= g.dock_x && x < g.dock_x + g.dock_w) {
            static const VqeafPhoneApp dock_apps[4] = {
                VQEAF_APP_SETTINGS, VQEAF_APP_STORAGE, VQEAF_APP_CLOCK, VQEAF_APP_LOCK
            };
            uint32_t slot = (x - g.dock_x) * 4u / g.dock_w;
            if (slot > 3u) slot = 3u;
            st->sel = (uint32_t)dock_apps[slot];
            return activate_home(st);
        }
        for (uint32_t i = 0; i < PHONE_APP_COUNT; i++) {
            uint32_t row = i / g.cols, col = i % g.cols;
            uint32_t cx = g.x0 + col * ((uint32_t)g.icon + g.gap);
            uint32_t iy = g.top + row * (g.cell_h + g.row_gap);
            if (iy + g.cell_h > fb_h) break;
            if (x >= cx && x < cx + (uint32_t)g.icon && y >= iy &&
                y < iy + g.cell_h) {
                st->sel = i;
                return activate_home(st);
            }
        }
        return VQEAF_PHONE_ACTION_NONE;
    }
    case VQEAF_PHONE_QUICK: {
        if (y < 30u * s || (fb_h > 30u * s && y >= fb_h - 30u * s)) {
            st->screen = st->previous;
            st->sel = st->previous_sel;
            return VQEAF_PHONE_ACTION_NONE;
        }
        /* Row geometry mirrors draw_quick so a tap lands on the drawn row. */
        uint32_t ry0 = 30u * s + 8u * s + 16u * (s >= 2u ? 2u : 1u) + 10u * s;
        uint32_t bottom = fb_h > (s >= 2u ? 64u : 42u)
                              ? fb_h - (s >= 2u ? 64u : 42u) : ry0 + 20u;
        uint32_t gap = 4u * s;
        uint32_t avail = bottom > ry0 ? bottom - ry0 : 20u;
        uint32_t row_h = avail > (QUICK_ROWS - 1u) * gap
                             ? (avail - (QUICK_ROWS - 1u) * gap) / QUICK_ROWS : 18u;
        if (row_h > 36u * s) row_h = 36u * s;
        if (row_h < 18u) row_h = 18u;
        for (uint32_t i = 0; i < QUICK_ROWS; i++) {
            uint32_t ry = ry0 + i * (row_h + gap);
            if (ry + row_h > fb_h) break;
            if (y >= ry && y < ry + row_h) {
                if (i >= 5u) return VQEAF_PHONE_ACTION_NONE;
                st->sel = i;
                return activate_quick(st);
            }
        }
        return VQEAF_PHONE_ACTION_NONE;
    }
    case VQEAF_PHONE_APP: {
        if (y < 30u * s) {
            st->previous = VQEAF_PHONE_APP;
            st->previous_sel = st->sel;
            st->screen = VQEAF_PHONE_QUICK;
            st->sel = 0;
            return VQEAF_PHONE_ACTION_NONE;
        }
        if ((x < f.x + 40u * s && y < 30u * s + 34u * s) ||
            (fb_h > 32u * s && y >= fb_h - 32u * s)) {
            st->sel = (uint32_t)st->app;
            st->screen = VQEAF_PHONE_HOME;
            return VQEAF_PHONE_ACTION_NONE;
        }
        if (st->app == VQEAF_APP_SETTINGS) {
            uint32_t sy = 30u * s + 50u * s, row_h = 40u * s, gap = 6u * s;
            for (uint32_t i = 0; i < SETTINGS_ROWS; i++) {
                if (sy + row_h > fb_h) break;
                if (y >= sy && y < sy + row_h) {
                    if (i >= 2u) return VQEAF_PHONE_ACTION_NONE;
                    st->sel = i;
                    return activate_app(st);
                }
                sy += row_h + gap;
            }
        }
        return VQEAF_PHONE_ACTION_NONE;
    }
    }
    return VQEAF_PHONE_ACTION_NONE;
}
