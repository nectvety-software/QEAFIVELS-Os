#include "include/vqeaf/screen_common.h"
#include "include/vqeaf/font_icons.h"

static void thick_line(const VqeafFramebuffer *fb, uint32_t x0, uint32_t y0,
                       uint32_t x1, uint32_t y1, uint32_t thickness,
                       uint32_t color) {
    if (!thickness) thickness = 1;
    int32_t half = (int32_t)thickness / 2;
    for (int32_t o = -half; o <= half; o++) {
        vqeaf_gfx_line(fb, (uint32_t)((int32_t)x0 + o), (uint32_t)((int32_t)y0 + o),
                       (uint32_t)((int32_t)x1 + o), (uint32_t)((int32_t)y1 + o), color);
    }
}

VqeafScreenFrame vqeaf_screen_frame(const VqeafFramebuffer *fb) {
    VqeafScreenFrame f = {0, 0, 1};
    if (!fb) return f;
    f.w = fb->width;
    if (fb->width < fb->height) {
        /* A portrait panel already has the shipped 9:16 shape: the shell owns
         * the whole framebuffer instead of drawing a centred column. */
        f.w = fb->width;
    } else if (fb->width >= 760u) {
        uint32_t cw = fb->height * 9u / 16u;
        if (cw > 620u) cw = 620u;
        if (cw < 400u) cw = 400u;
        if (cw > fb->width) cw = fb->width;
        f.w = cw;
        f.x = (fb->width - cw) / 2u;
    }
    f.scale = f.w >= 420u ? 2u : 1u;
    return f;
}

VqeafPalette vqeaf_screen_palette(const VqeafFramebuffer *fb) {
    VqeafPalette p;
    p.bg = vqeaf_gfx_rgb(fb, 0x050B12);
    p.bar = vqeaf_gfx_rgb(fb, 0x0A1420);
    p.panel = vqeaf_gfx_rgb(fb, 0x0E1A26);
    p.panel_hi = vqeaf_gfx_rgb(fb, 0x12293A);
    p.accent = vqeaf_gfx_rgb(fb, 0x22E0D0);
    p.accent_dim = vqeaf_gfx_rgb(fb, 0x146C74);
    p.text = vqeaf_gfx_rgb(fb, 0xEAFBFF);
    p.dim = vqeaf_gfx_rgb(fb, 0x7E97A3);
    p.track = vqeaf_gfx_rgb(fb, 0x18323C);
    p.danger = vqeaf_gfx_rgb(fb, 0xFF5A70);
    return p;
}

uint32_t vqeaf_screen_row_height(const VqeafScreenFrame *f) {
    return 32u * (f ? f->scale : 1u);
}

void vqeaf_screen_header(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                         const VqeafPalette *p, const char *mode) {
    uint32_t h = 28u * f->scale;
    uint32_t r = 9u * f->scale;
    vqeaf_gfx_rect(fb, f->x, 0, f->w, h, p->bar);
    vqeaf_screen_badge(fb, f->x + 18u * f->scale, h / 2u, r, 0, p);
    vqeaf_gfx_text(fb, f->x + (18u + 12u) * f->scale, (h - 16u) / 2u,
                   "VQEAF OS", 1u, p->text);
    if (mode && *mode) {
        uint32_t tw = vqeaf_gfx_text_width(mode, 1u);
        vqeaf_gfx_text(fb, f->x + f->w - tw - 14u * f->scale, (h - 16u) / 2u,
                       mode, 1u, p->text);
    }
    vqeaf_gfx_rect(fb, f->x, h, f->w, 1u, p->accent_dim);
}

/* Decorative sea waves at the bottom, drawn as column height offsets. */
void vqeaf_screen_waves_region(const VqeafFramebuffer *fb, uint32_t x0,
                               uint32_t w, uint32_t baseline,
                               const VqeafPalette *p) {
    static const uint8_t shape[32] = {
        0, 1, 3, 6, 10, 15, 20, 25, 29, 32, 34, 35, 35, 34, 32, 29,
        25, 20, 15, 10, 6, 3, 1, 0, 0, 1, 2, 4, 7, 11, 16, 22
    };
    if (!fb || !fb->width || !w || baseline >= fb->height) return;
    if (x0 >= fb->width) return;
    if (x0 + w > fb->width) w = fb->width - x0;
    for (uint32_t i = 0; i < w; i++) {
        uint32_t x = x0 + i;
        uint32_t k = (i * 32u) / w;
        uint32_t y = baseline + shape[k] / 2u;
        if (y >= fb->height) continue;
        vqeaf_gfx_rect(fb, x, y, 1, fb->height - y,
                       ((x / 24u) % 2u) ? p->accent_dim : p->track);
    }
}

void vqeaf_screen_waves(const VqeafFramebuffer *fb, uint32_t baseline,
                        const VqeafPalette *p) {
    if (!fb) return;
    vqeaf_screen_waves_region(fb, 0, fb->width, baseline, p);
}

void vqeaf_screen_badge(const VqeafFramebuffer *fb, uint32_t cx, uint32_t cy,
                        uint32_t radius, int rings, const VqeafPalette *p) {
    if (!radius) return;
    if (rings) {
        for (int i = 0; i < rings; i++) {
            uint32_t r = radius + (uint32_t)i * (radius / 5u + 4u);
            vqeaf_gfx_circle(fb, cx, cy, r, 1u, p->accent_dim);
        }
    }
    vqeaf_gfx_disc(fb, cx, cy, radius, p->accent);
    uint32_t t = radius / 4u;
    if (!t) t = 1;
    thick_line(fb, cx - radius / 2u, cy - radius / 2u, cx, cy + radius / 2u, t, p->bg);
    thick_line(fb, cx, cy + radius / 2u, cx + radius / 2u, cy - radius / 2u, t, p->bg);
}

static const uint32_t icon_codepoints[] = {
    0u,          /* VQEAF_ICON_NONE */
    0xE037u,     /* PLAY: play_arrow */
    0xF053u,     /* REBOOT: restart_alt */
    0xE2C4u,     /* DOWNLOAD: file_download */
    0xE872u,     /* TRASH: delete */
    0xE8B8u,     /* GEAR: settings */
    0xE8ACu,     /* POWER: power_settings_new */
    0xE88Eu,     /* INFO: info */
    0xE5CAu,     /* CHECK: check */
    0xE8B5u,     /* CLOCK: schedule */
    0xE897u,     /* LOCK: lock */
    0xE63Eu,     /* WIFI: wifi */
    0xE1A7u,     /* BLUETOOTH: bluetooth */
    0xE1DBu,     /* STORAGE: storage */
    0xE88Au,     /* HOME: home */
    0xE5C4u,     /* BACK: arrow_back */
    0xE425u,     /* TIMER: timer */
    0xE8F4u,     /* KEEP_AWAKE: visibility */
    0xE32Cu,     /* DISPLAY: smartphone */
    0xE62Au,     /* UPDATE: system_update */
    0xEA0Bu,     /* BOLT: bolt */
    0xEF44u      /* SLEEP: bedtime */
};

static const VqeafIconGlyph *icon_glyph(VqeafIcon icon) {
    uint32_t i = (uint32_t)icon;
    if (i >= sizeof icon_codepoints / sizeof icon_codepoints[0]) return 0;
    uint32_t cp = icon_codepoints[i];
    if (!cp) return 0;
    for (uint32_t k = 0; k < VQEAF_ICON_GLYPH_COUNT; k++)
        if (vqeaf_icon_glyphs[k].codepoint == cp) return &vqeaf_icon_glyphs[k];
    return 0;
}

void vqeaf_screen_icon(const VqeafFramebuffer *fb, uint32_t x, uint32_t y,
                       uint32_t size, VqeafIcon icon, uint32_t color) {
    const VqeafIconGlyph *glyph = icon_glyph(icon);
    if (!glyph || !size || !fb || !fb->pixels) return;
    const uint32_t src = VQEAF_ICON_GLYPH_SIZE;
    for (uint32_t py = 0; py < size; py++) {
        uint32_t dy = y + py;
        if (dy >= fb->height) break;
        uint32_t sy = py * src / size;
        const uint8_t *row = glyph->rows[sy];
        volatile uint32_t *dst = fb->pixels + (uint64_t)dy * fb->stride;
        for (uint32_t px = 0; px < size; px++) {
            uint32_t dx = x + px;
            if (dx >= fb->width) break;
            uint32_t alpha = row[px * src / size];
            if (!alpha) continue;
            dst[dx] = vqeaf_gfx_blend(fb, dst[dx], color, alpha);
        }
    }
}

uint32_t vqeaf_screen_menu_row(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                               const VqeafPalette *p, uint32_t y, VqeafIcon icon,
                               const char *label, const char *status,
                               int selected, int enabled) {
    return vqeaf_screen_menu_row_h(fb, f, p, y, vqeaf_screen_row_height(f), icon,
                                   label, status, selected, enabled);
}

uint32_t vqeaf_screen_menu_row_h(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                                 const VqeafPalette *p, uint32_t y, uint32_t h,
                                 VqeafIcon icon, const char *label, const char *status,
                                 int selected, int enabled) {
    uint32_t s = f->scale;
    uint32_t panel = selected ? p->panel_hi : p->panel;
    uint32_t text = enabled ? p->text : p->dim;
    uint32_t accent = enabled ? p->accent : p->accent_dim;
    uint32_t icon_size = 20u * s;
    if (h < icon_size + 8u) icon_size = h > 16u ? h - 8u : h / 2u;
    vqeaf_gfx_rect(fb, f->x, y, f->w, h, panel);
    if (selected) vqeaf_gfx_border(fb, f->x, y, f->w, h, 2u, accent);
    vqeaf_screen_icon(fb, f->x + 14u * s, y + (h - icon_size) / 2u, icon_size,
                      icon, accent);
    uint32_t tx = f->x + 44u * s;
    uint32_t ty = y + (h > 16u * s ? (h - 16u * s) / 2u : 0u);
    vqeaf_gfx_text(fb, tx, ty, label, s, text);
    /* Row status ("TAT" for disabled items) stays at 1x: the 2x value column
     * is narrower than label + status and would collide with the chevron. */
    if (status && *status) {
        uint32_t sw = vqeaf_gfx_text_width(status, 1u);
        uint32_t lw = vqeaf_gfx_text_width(label, s);
        uint32_t sy = ty + (16u * s > 16u ? (16u * s - 16u) / 2u : 0u);
        uint32_t sx = f->x + f->w - sw - 34u * s;
        if (sx > tx + lw + 8u * s)
            vqeaf_gfx_text(fb, sx, sy, status, 1u, p->dim);
        else if (tx + lw + 10u * s + sw + 26u * s < f->x + f->w)
            vqeaf_gfx_text(fb, tx + lw + 10u * s, sy, status, 1u, p->dim);
    }
    uint32_t cxp = f->x + f->w - 18u * s, cyp = y + h / 2u;
    thick_line(fb, cxp - 2u * s, cyp - 5u * s, cxp + 2u * s, cyp, s, accent);
    thick_line(fb, cxp + 2u * s, cyp, cxp - 2u * s, cyp + 5u * s, s, accent);
    return h;
}
