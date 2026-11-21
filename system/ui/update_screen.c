#include <stdint.h>
#include "include/vqeaf/update_screen.h"
#include "include/vqeaf/screen_common.h"

static const char *const stage_labels[VQEAF_UPDATE_STAGE_COUNT] = {
    "Ki\u1EC3m tra g\u00F3i c\u1EADp nh\u1EADt",
    "X\u00E1c minh ch\u1EEF k\u00FD v\u00E0 SHA-256",
    "Ki\u1EC3m tra pin v\u00E0 dung l\u01B0\u1EE3ng",
    "\u00C1p d\u1EE5ng v\u00E0 x\u00E1c minh sau ghi"
};

const char *vqeaf_update_stage_label(uint32_t index) {
    return index < VQEAF_UPDATE_STAGE_COUNT ? stage_labels[index] : 0;
}

static void put_num(char *out, uint32_t v) {
    char tmp[4];
    uint32_t n = 0;
    if (v > 100u) v = 100u;
    if (!v) { out[0] = '0'; out[1] = 0; return; }
    while (v && n < 3) { tmp[n++] = (char)('0' + v % 10u); v /= 10u; }
    uint32_t i = 0;
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

static void stage_row(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                      const VqeafPalette *p, uint32_t x, uint32_t y, uint32_t w,
                      uint32_t index, uint32_t active, int failed, int available,
                      uint32_t text_scale) {
    uint32_t s = f->scale;
    int done = available && index < active;
    int current = available && !failed && index == active;
    uint32_t color = failed && current ? p->danger : done ? p->accent
                   : current ? p->text : p->dim;
    uint32_t r = 5u * s;
    if (done) {
        vqeaf_gfx_disc(fb, x + r, y + 8u * s, r, p->accent);
        vqeaf_gfx_line(fb, x + r - 3u * s, y + 8u * s, x + r - 1u * s,
                       y + 10u * s, p->bg);
        vqeaf_gfx_line(fb, x + r - 1u * s, y + 10u * s, x + r + 3u * s,
                       y + 5u * s, p->bg);
    } else {
        vqeaf_gfx_circle(fb, x + r, y + 8u * s, r, 1u, color);
        if (current) vqeaf_gfx_disc(fb, x + r, y + 8u * s, r / 2u, color);
    }
    /* Keep the label on one line: a 2x label is ~24 px per glyph, which the
     * 720 px portrait panel cannot hold for the longest stages. */
    uint32_t ty = y;
    if (text_scale < s) {
        uint32_t th = 16u * text_scale;
        ty = y + (8u * s > th / 2u ? 8u * s - th / 2u : 0u);
    }
    vqeaf_gfx_text(fb, x + 18u * s, ty, stage_labels[index], text_scale, color);
    (void)w;
}

void vqeaf_update_screen_render_view(const VqeafFramebuffer *fb,
                                     const VqeafUpdateView *view) {
    if (!fb || !fb->pixels || !fb->width || !fb->height) return;
    if (!view) return;
    uint32_t percent = view->percent > 100u ? 100u : view->percent;
    int failed = view->failed ? 1 : 0;
    int available = view->available ? 1 : 0;

    VqeafScreenFrame f = vqeaf_screen_frame(fb);
    VqeafPalette p = vqeaf_screen_palette(fb);
    uint32_t s = f.scale;
    uint32_t accent = failed ? p.danger : p.accent;

    vqeaf_gfx_clear(fb, p.bg);
    vqeaf_screen_waves(fb, fb->height - 36u * s, &p);
    vqeaf_screen_header(fb, &f, &p, "System Update");
    vqeaf_gfx_rect(fb, f.x, 0, f.w, 3u, accent);

    uint32_t hdr = 28u * s;
    uint32_t cx = f.x + f.w / 2u;
    uint32_t r = 24u * s;
    uint32_t cy = hdr + 12u * s + r;
    vqeaf_screen_badge(fb, cx, cy, r, 2, &p);

    uint32_t title_y = cy + r + 10u * s;
    vqeaf_gfx_text_centered(fb, title_y, "INSTALLING", s + 2u, p.text);
    vqeaf_gfx_text_centered(fb, title_y + 16u * (s + 2u) + 2u,
                            "SYSTEM UPDATE", s, accent);

    uint32_t panel_y = title_y + 16u * (s + 2u) + 16u * s;
    uint32_t panel_h = 62u * s;
    uint32_t margin = f.w / 12u;
    vqeaf_gfx_rect(fb, f.x + margin, panel_y, f.w - 2u * margin, panel_h, p.panel);
    vqeaf_gfx_progress(fb, f.x + margin + 10u * s, panel_y + 12u * s,
                       f.w - 2u * margin - 20u * s, 10u * s, percent, accent, p.track);
    char number[4];
    put_num(number, percent);
    char pct[5];
    uint32_t i = 0;
    while (number[i]) { pct[i] = number[i]; i++; }
    pct[i++] = '%';
    pct[i] = 0;
    vqeaf_gfx_text_centered(fb, panel_y + 30u * s, pct, s + 1u,
                            available ? p.text : p.dim);

    uint32_t stage_y = panel_y + panel_h + 10u * s;
    uint32_t footer_h = 44u * s;
    uint32_t tail_h = 44u * s; /* warning + detail lines above the footer */
    uint32_t rows_bottom = fb->height > footer_h + tail_h
                         ? fb->height - footer_h - tail_h : stage_y;
    uint32_t row_h = 26u * s;
    if (rows_bottom > stage_y && row_h * VQEAF_UPDATE_STAGE_COUNT > rows_bottom - stage_y)
        row_h = (rows_bottom - stage_y) / VQEAF_UPDATE_STAGE_COUNT;
    if (row_h < 16u) row_h = 16u;
    uint32_t stage_x = f.x + margin;
    uint32_t label_avail = f.w > margin + 18u * s
                         ? f.w - margin - 18u * s : 0u;
    uint32_t stage_scale = s;
    for (uint32_t k = 0; k < VQEAF_UPDATE_STAGE_COUNT; k++) {
        if (vqeaf_gfx_text_width(stage_labels[k], s) > label_avail) {
            stage_scale = 1u;
            break;
        }
    }
    for (uint32_t k = 0; k < VQEAF_UPDATE_STAGE_COUNT; k++) {
        stage_row(fb, &f, &p, stage_x, stage_y + k * row_h,
                  f.w - 2u * margin, k, view->stage, failed, available,
                  stage_scale);
    }

    uint32_t warn_y = stage_y + VQEAF_UPDATE_STAGE_COUNT * row_h + 6u * s;
    const char *warn;
    const char *detail = view->detail;
    if (!available) {
        warn = "KH\u00D4NG C\u00D3 D\u1ECACH V\u1EE4";
        detail = "UPDATER UNAVAILABLE - KH\u00D4NG GHI";
    } else {
        warn = "KH\u00D4NG T\u1EAET NGU\u1ED2N";
        if (failed) detail = "C\u1EADp nh\u1EADt th\u1EA5t b\u1EA1i - Recovery";
    }
    vqeaf_gfx_text_centered(fb, warn_y, warn, s, failed ? p.danger : p.dim);
    if (detail && *detail)
        vqeaf_gfx_text_centered(fb, warn_y + 22u * s, detail, 1u,
                                failed || !available ? p.danger : p.dim);
    /* Footer bar: the wave band runs full width, so the version line needs a
     * solid strip under it instead of sitting on stripes. */
    uint32_t bar_h = 40u * s;
    vqeaf_gfx_rect(fb, f.x, fb->height - bar_h, f.w, bar_h, p.bg);
    vqeaf_gfx_rect(fb, f.x, fb->height - bar_h, f.w, 1u, p.accent_dim);
    vqeaf_gfx_text_centered(fb, fb->height - 22u * s,
                            "VQEAF OS  |  SYSTEM UPDATE v0.3", 1u, p.dim);
}

void vqeaf_update_screen_render(const VqeafFramebuffer *fb, uint32_t percent,
                                const char *stage, int failed) {
    VqeafUpdateView view;
    view.percent = percent;
    view.stage = percent >= 100u ? VQEAF_UPDATE_STAGE_COUNT : 0u;
    view.detail = stage;
    view.failed = failed;
    view.available = 1;
    vqeaf_update_screen_render_view(fb, &view);
}
