#include "include/vqeaf/gfx.h"
#include "include/vqeaf/font_ubuntu.h"

static uint32_t next_utf8(const unsigned char **cursor) {
    const unsigned char *p = *cursor;
    uint32_t cp;
    if (!*p) return 0;
    if (p[0] < 0x80u) { *cursor = p + 1; return p[0]; }
    if (p[0] >= 0xC2u && p[0] <= 0xDFu && p[1] &&
        ((p[1] & 0xC0u) == 0x80u)) {
        cp = ((uint32_t)(p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
        *cursor = p + 2;
        return cp;
    }
    if (p[0] >= 0xE0u && p[0] <= 0xEFu && p[1] && p[2] &&
        ((p[1] & 0xC0u) == 0x80u) && ((p[2] & 0xC0u) == 0x80u)) {
        cp = ((uint32_t)(p[0] & 0x0Fu) << 12) |
             ((uint32_t)(p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
        if (cp >= 0x800u && (cp < 0xD800u || cp > 0xDFFFu)) {
            *cursor = p + 3;
            return cp;
        }
    }
    if (p[0] >= 0xF0u && p[0] <= 0xF4u && p[1] && p[2] && p[3] &&
        ((p[1] & 0xC0u) == 0x80u) && ((p[2] & 0xC0u) == 0x80u) &&
        ((p[3] & 0xC0u) == 0x80u)) {
        cp = ((uint32_t)(p[0] & 7u) << 18) |
             ((uint32_t)(p[1] & 0x3Fu) << 12) |
             ((uint32_t)(p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
        if (cp >= 0x10000u && cp <= 0x10FFFFu) {
            *cursor = p + 4;
            return cp;
        }
    }
    *cursor = p + 1;
    return (uint32_t)'?';
}

static const VqeafUbuntuGlyph *glyph_for(uint32_t cp) {
    uint32_t lo = 0;
    uint32_t hi = (uint32_t)VQEAF_UBUNTU_GLYPH_COUNT;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        uint32_t key = vqeaf_ubuntu_glyphs[mid].codepoint;
        if (key == cp) return &vqeaf_ubuntu_glyphs[mid];
        if (key < cp) lo = mid + 1u;
        else hi = mid;
    }
    if (cp != (uint32_t)'?') return glyph_for((uint32_t)'?');
    return 0;
}

int vqeaf_gfx_setup(VqeafFramebuffer *fb, uint64_t physical_base,
                    uint32_t width, uint32_t height, uint32_t stride,
                    uint32_t pixel_format) {
    if (!fb || !physical_base || !width || !height ||
        width > 4096u || height > 4096u) return -1;
    if (stride < width || stride > 8192u) return -1;
    if (pixel_format != VQEAF_GFX_FMT_RGB &&
        pixel_format != VQEAF_GFX_FMT_BGR) return -1;
    fb->pixels = (volatile uint32_t *)(uintptr_t)physical_base;
    fb->width = width;
    fb->height = height;
    fb->stride = stride;
    fb->format = pixel_format;
    return 0;
}

uint32_t vqeaf_gfx_rgb(const VqeafFramebuffer *fb, uint32_t rgb) {
    uint32_t r = (rgb >> 16) & 255u;
    uint32_t g = (rgb >> 8) & 255u;
    uint32_t b = rgb & 255u;
    if (fb->format == VQEAF_GFX_FMT_RGB)
        return r | (g << 8) | (b << 16) | 0xFF000000u;
    return b | (g << 8) | (r << 16) | 0xFF000000u;
}

void vqeaf_gfx_pixel(const VqeafFramebuffer *fb, uint32_t x,
                     uint32_t y, uint32_t color) {
    if (x >= fb->width || y >= fb->height) return;
    fb->pixels[(uint64_t)y * fb->stride + x] = color;
}

void vqeaf_gfx_clear(const VqeafFramebuffer *fb, uint32_t color) {
    for (uint32_t y = 0; y < fb->height; y++)
        for (uint32_t x = 0; x < fb->width; x++)
            fb->pixels[(uint64_t)y * fb->stride + x] = color;
}

void vqeaf_gfx_rect(const VqeafFramebuffer *fb, uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h, uint32_t color) {
    if (x >= fb->width || y >= fb->height || !w || !h) return;
    uint32_t xe = x + w;
    uint32_t ye = y + h;
    if (xe > fb->width || xe < x) xe = fb->width;
    if (ye > fb->height || ye < y) ye = fb->height;
    for (uint32_t row = y; row < ye; row++)
        for (uint32_t col = x; col < xe; col++)
            fb->pixels[(uint64_t)row * fb->stride + col] = color;
}

void vqeaf_gfx_border(const VqeafFramebuffer *fb, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, uint32_t thickness,
                      uint32_t color) {
    if (!thickness || !w || !h) return;
    vqeaf_gfx_rect(fb, x, y, w, thickness, color);
    if (h > thickness) vqeaf_gfx_rect(fb, x, y + h - thickness, w, thickness, color);
    vqeaf_gfx_rect(fb, x, y, thickness, h, color);
    if (w > thickness) vqeaf_gfx_rect(fb, x + w - thickness, y, thickness, h, color);
}

void vqeaf_gfx_line(const VqeafFramebuffer *fb, uint32_t x0, uint32_t y0,
                    uint32_t x1, uint32_t y1, uint32_t color) {
    /* Bresenham with the error term stepping toward zero; both deltas keep
     * their sign, otherwise the loop can walk past the endpoint forever. */
    int32_t ax = (int32_t)x0, ay = (int32_t)y0;
    int32_t bx = (int32_t)x1, by = (int32_t)y1;
    int32_t dx = bx > ax ? bx - ax : ax - bx;
    int32_t dy = by > ay ? by - ay : ay - by;
    int32_t sx = ax < bx ? 1 : -1;
    int32_t sy = ay < by ? 1 : -1;
    int32_t err = dx - dy;
    for (;;) {
        vqeaf_gfx_pixel(fb, (uint32_t)ax, (uint32_t)ay, color);
        if (ax == bx && ay == by) break;
        int32_t e2 = 2 * err;
        if (e2 >= -dy) { err -= dy; ax += sx; }
        if (e2 <= dx) { err += dx; ay += sy; }
    }
}

void vqeaf_gfx_circle(const VqeafFramebuffer *fb, uint32_t cx, uint32_t cy,
                      uint32_t radius, uint32_t thickness, uint32_t color) {
    if (!thickness || !radius) return;
    for (uint32_t t = 0; t < thickness && radius > t; t++) {
        int32_t r = (int32_t)(radius - t);
        int32_t x = r, y = 0, err = 1 - r;
        while (x >= y) {
            vqeaf_gfx_pixel(fb, (uint32_t)((int32_t)cx + x), (uint32_t)((int32_t)cy + y), color);
            vqeaf_gfx_pixel(fb, (uint32_t)((int32_t)cx + y), (uint32_t)((int32_t)cy + x), color);
            vqeaf_gfx_pixel(fb, (uint32_t)((int32_t)cx - y), (uint32_t)((int32_t)cy + x), color);
            vqeaf_gfx_pixel(fb, (uint32_t)((int32_t)cx - x), (uint32_t)((int32_t)cy + y), color);
            vqeaf_gfx_pixel(fb, (uint32_t)((int32_t)cx - x), (uint32_t)((int32_t)cy - y), color);
            vqeaf_gfx_pixel(fb, (uint32_t)((int32_t)cx - y), (uint32_t)((int32_t)cy - x), color);
            vqeaf_gfx_pixel(fb, (uint32_t)((int32_t)cx + y), (uint32_t)((int32_t)cy - x), color);
            vqeaf_gfx_pixel(fb, (uint32_t)((int32_t)cx + x), (uint32_t)((int32_t)cy - y), color);
            if (err < 0) {
                y++;
                err += 2 * y + 1;
            } else {
                x--;
                y++;
                err += 2 * (y - x) + 1;
            }
        }
    }
}

void vqeaf_gfx_disc(const VqeafFramebuffer *fb, uint32_t cx, uint32_t cy,
                    uint32_t radius, uint32_t color) {
    if (!radius) return;
    int32_t r2 = (int32_t)(radius * radius);
    for (int32_t dy = -(int32_t)radius; dy <= (int32_t)radius; dy++) {
        int32_t inside = r2 - dy * dy;
        if (inside < 0) continue;
        int32_t dx = 0;
        while ((dx + 1) * (dx + 1) <= inside) dx++;
        vqeaf_gfx_rect(fb, (uint32_t)((int32_t)cx - dx),
                       (uint32_t)((int32_t)cy + dy),
                       (uint32_t)(2 * dx + 1), 1, color);
    }
}

uint32_t vqeaf_gfx_blend(const VqeafFramebuffer *fb, uint32_t dst, uint32_t color,
                         uint32_t alpha) {
    if (!alpha) return dst;
    if (alpha >= 255u) return color;
    uint32_t dr, dg, db, sr, sg, sb;
    if (fb->format == VQEAF_GFX_FMT_RGB) {
        dr = dst & 255u; dg = (dst >> 8) & 255u; db = (dst >> 16) & 255u;
        sr = color & 255u; sg = (color >> 8) & 255u; sb = (color >> 16) & 255u;
    } else {
        db = dst & 255u; dg = (dst >> 8) & 255u; dr = (dst >> 16) & 255u;
        sb = color & 255u; sg = (color >> 8) & 255u; sr = (color >> 16) & 255u;
    }
    uint32_t ia = 255u - alpha;
    uint32_t r = (sr * alpha + dr * ia) / 255u;
    uint32_t g = (sg * alpha + dg * ia) / 255u;
    uint32_t b = (sb * alpha + db * ia) / 255u;
    if (fb->format == VQEAF_GFX_FMT_RGB)
        return r | (g << 8) | (b << 16) | 0xFF000000u;
    return b | (g << 8) | (r << 16) | 0xFF000000u;
}

void vqeaf_gfx_round_rect(const VqeafFramebuffer *fb, uint32_t x, uint32_t y,
                          uint32_t w, uint32_t h, uint32_t radius,
                          uint32_t color) {
    if (!w || !h) return;
    if (radius > w / 2u) radius = w / 2u;
    if (radius > h / 2u) radius = h / 2u;
    if (!radius) { vqeaf_gfx_rect(fb, x, y, w, h, color); return; }
    vqeaf_gfx_rect(fb, x + radius, y, w - 2u * radius, h, color);
    vqeaf_gfx_rect(fb, x, y + radius, radius, h - 2u * radius, color);
    vqeaf_gfx_rect(fb, x + w - radius, y + radius, radius, h - 2u * radius,
                   color);
    vqeaf_gfx_disc(fb, x + radius, y + radius, radius, color);
    vqeaf_gfx_disc(fb, x + w - 1u - radius, y + radius, radius, color);
    vqeaf_gfx_disc(fb, x + radius, y + h - 1u - radius, radius, color);
    vqeaf_gfx_disc(fb, x + w - 1u - radius, y + h - 1u - radius, radius, color);
}

uint32_t vqeaf_gfx_text_width(const char *text, uint32_t scale) {
    if (!scale) scale = 1;
    const unsigned char *p = (const unsigned char *)text;
    uint32_t count = 0;
    while (*p) { (void)next_utf8(&p); count++; }
    return count * VQEAF_UBUNTU_GLYPH_W * scale;
}

void vqeaf_gfx_text(const VqeafFramebuffer *fb, uint32_t x, uint32_t y,
                    const char *text, uint32_t scale, uint32_t color) {
    if (!scale) scale = 1;
    const unsigned char *p = (const unsigned char *)text;
    uint32_t index = 0;
    while (*p) {
        uint32_t cp = next_utf8(&p);
        const VqeafUbuntuGlyph *glyph = glyph_for(cp);
        if (glyph) {
            uint32_t gx = x + index * VQEAF_UBUNTU_GLYPH_W * scale;
            for (uint32_t row = 0; row < VQEAF_UBUNTU_GLYPH_H; row++) {
                for (uint32_t col = 0; col < VQEAF_UBUNTU_GLYPH_W; col++) {
                    uint32_t alpha = glyph->rows[row][col];
                    if (!alpha) continue;
                    uint32_t px = gx + col * scale;
                    uint32_t py = y + row * scale;
                    for (uint32_t dy = 0; dy < scale && py + dy < fb->height; dy++) {
                        if (px >= fb->width) continue;
                        uint32_t dxmax = scale;
                        if (dxmax > fb->width - px) dxmax = fb->width - px;
                        volatile uint32_t *dst = fb->pixels + (uint64_t)(py + dy) * fb->stride + px;
                        for (uint32_t dx = 0; dx < dxmax; dx++)
                            dst[dx] = vqeaf_gfx_blend(fb, dst[dx], color, alpha);
                    }
                }
            }
        }
        index++;
    }
}

void vqeaf_gfx_text_centered(const VqeafFramebuffer *fb, uint32_t y,
                             const char *text, uint32_t scale, uint32_t color) {
    uint32_t w = vqeaf_gfx_text_width(text, scale);
    uint32_t x = w < fb->width ? (fb->width - w) / 2u : 0;
    vqeaf_gfx_text(fb, x, y, text, scale, color);
}

void vqeaf_gfx_progress(const VqeafFramebuffer *fb, uint32_t x, uint32_t y,
                        uint32_t w, uint32_t h, uint32_t percent,
                        uint32_t fill, uint32_t track) {
    if (w < 4u || h < 4u) return;
    if (percent > 100u) percent = 100u;
    vqeaf_gfx_rect(fb, x, y, w, h, track);
    vqeaf_gfx_border(fb, x, y, w, h, 1, fill);
    if (percent)
        vqeaf_gfx_rect(fb, x + 2, y + 2,
                       (uint32_t)((uint64_t)(w - 4u) * percent / 100u),
                       h - 4u, fill);
}
