/* SPDX-License-Identifier: MIT */
/* platform/violet/splash.c - terminal-style boot log for the VQEAF-OS ports.
 *
 * Renders into an XRGB8888 canvas (0x00RRGGBB, top byte ignored) using the
 * Ubuntu 12x16 anti-aliased coverage atlas generated from
 * assets/fonts/Ubuntu-Regular.ttf (UFL-1.0, see assets/fonts/LICENCE.txt).
 * The font has a fixed 12 px advance, so ASCII boot lines read as monospaced.
 * Both violet and mt689x call this renderer with their own boot lines. */
#include "splash.h"

#include <stddef.h>
#include <string.h>

#include "include/vqeaf/font8x8.h"

#define SPLASH_BG     0xFF050A0Bu
#define SPLASH_SCAN   0xFF0A1716u
#define SPLASH_ACCENT 0xFF35E0C0u   /* terminal phosphor teal, XRGB8888 */
#define SPLASH_WHITE  0xFFD8F5ECu
#define SPLASH_GREY   0xFF5E8F85u

static void fill(uint32_t *buf, int w, int h, uint32_t color)
{
    for (int i = 0; i < w * h; i++) buf[i] = color;
}

static void pixel_blend(uint32_t *buf, int w, int h, int x, int y,
                       uint32_t fg, uint8_t alpha)
{
    if (x < 0 || y < 0 || x >= w || y >= h || !alpha) return;
    uint32_t dst = buf[(size_t)y * (size_t)w + (size_t)x];
    if (alpha >= 255) {
        buf[(size_t)y * (size_t)w + (size_t)x] = fg | 0xFF000000u;
        return;
    }
    uint32_t dr = (dst >> 16) & 0xFFu;
    uint32_t dg = (dst >> 8) & 0xFFu;
    uint32_t db = dst & 0xFFu;
    uint32_t fr = (fg >> 16) & 0xFFu;
    uint32_t fgg = (fg >> 8) & 0xFFu;
    uint32_t fb = fg & 0xFFu;
    uint32_t ia = 255u - alpha;
    uint32_t r = (fr * alpha + dr * ia) / 255u;
    uint32_t g = (fgg * alpha + dg * ia) / 255u;
    uint32_t b = (fb * alpha + db * ia) / 255u;
    buf[(size_t)y * (size_t)w + (size_t)x] =
        0xFF000000u | (r << 16) | (g << 8) | b;
}

static void rect(uint32_t *buf, int w, int h,
                 int x, int y, int rw, int rh, uint32_t color)
{
    for (int py = y; py < y + rh; py++)
        for (int px = x; px < x + rw; px++)
            pixel_blend(buf, w, h, px, py, color, 255);
}

/* Terminal glyph = the embedded VGA 8x8 font at its real 8 px cell. The
 * Ubuntu atlas has narrow ink in a 12 px cell, which looked letter-spaced. */
static void rect(uint32_t *buf, int w, int h,
                 int x, int y, int rw, int rh, uint32_t color);

static void terminal_glyph(uint32_t *buf, int w, int h, int x, int y,
                           unsigned char ch, int scale, uint32_t color)
{
    const uint8_t *g = vqeaf_font8x8[ch < 128 ? ch : (unsigned char)'?'];
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++)
            if (g[row] & (1u << col))
                rect(buf, w, h, x + col * scale, y + row * scale,
                     scale, scale, color);
}

static int text_width_px(const char *s, int scale)
{
    int n = 0;
    for (const char *p = s; *p; p++) n++;
    return n * 8 * scale;
}

static void text_left(uint32_t *buf, int w, int h, int x, int y,
                      const char *s, int scale, uint32_t color)
{
    if (!s) return;
    for (const char *p = s; *p; p++) {
        terminal_glyph(buf, w, h, x, y, (unsigned char)*p, scale, color);
        x += 8 * scale;
    }
}

static void text(uint32_t *buf, int w, int h, int cx, int y,
                 const char *s, int scale, uint32_t color)
{
    if (!s) return;
    int tw = text_width_px(s, scale);
    text_left(buf, w, h, cx - tw / 2, y, s, scale, color);
}

#define VIOLET_TERM_SCALE 2
static void terminal_box_edge(uint32_t *buf, int w, int h,
                              int x, int y, int cells, uint32_t color)
{
    text_left(buf, w, h, x, y, "+", VIOLET_TERM_SCALE, color);
    for (int i = 1; i < cells - 1; i++)
        text_left(buf, w, h, x + i * 8 * VIOLET_TERM_SCALE, y,
                  "-", VIOLET_TERM_SCALE, color);
    text_left(buf, w, h, x + (cells - 1) * 8 * VIOLET_TERM_SCALE, y,
              "+", VIOLET_TERM_SCALE, color);
}

static void terminal_box_row(uint32_t *buf, int w, int h,
                             int x, int y, int cells, const char *s,
                             uint32_t color)
{
    text_left(buf, w, h, x, y, "|", VIOLET_TERM_SCALE, SPLASH_GREY);
    text_left(buf, w, h, x + 8 * VIOLET_TERM_SCALE, y, s,
              VIOLET_TERM_SCALE, color);
    text_left(buf, w, h, x + (cells - 1) * 8 * VIOLET_TERM_SCALE, y,
              "|", VIOLET_TERM_SCALE, SPLASH_GREY);
}

void vio_splash_render(uint32_t *buf, int w, int h,
                       const char *title, const char *subtitle,
                       const char *const *lines)
{
    int y;

    fill(buf, w, h, SPLASH_BG);

    /* CRT scanlines: subtle, not a decorative panel. */
    for (int sy = 0; sy < h; sy += 4)
        rect(buf, w, h, 0, sy, w, 1, SPLASH_SCAN);
    rect(buf, w, h, 0, 0, w, 3, SPLASH_ACCENT);
    rect(buf, w, h, 0, h - 3, w, 3, SPLASH_ACCENT);

    /* Portrait telephones centre an 800-pixel terminal; landscape starts at
     * the top so the log stays readable on a 1560x720 panel. */
    int oy = (h > 800) ? (h - 800) / 2 : 0;
    /* 8x8 terminal face: title at 5x, log rows at 2x (16 px cell). */
    int title_scale = (w >= 480) ? 5 : 3;
    text(buf, w, h, w / 2, 150 + oy, title ? title : "VQEAF OS",
         title_scale, SPLASH_ACCENT);
    y = 150 + oy + 8 * title_scale + 20;
    if (subtitle && *subtitle) {
        text(buf, w, h, w / 2, y, subtitle, 2, SPLASH_GREY);
        y += 32;
    }

    int cells = (w >= 640) ? 40 : (w / 16 - 1);
    if (cells < 20) cells = 20;
    int bx = (w - cells * 8 * 2) / 2;
    if (bx < 8) bx = 8;
    y += 16;
    terminal_box_edge(buf, w, h, bx, y, cells, SPLASH_GREY);
    y += 20;
    if (lines) {
        for (int i = 0; lines[i] && i < 4; i++) {
            terminal_box_row(buf, w, h, bx, y, cells, lines[i],
                             i == 0 ? SPLASH_ACCENT : SPLASH_WHITE);
            y += 24;
        }
    }
    terminal_box_edge(buf, w, h, bx, y, cells, SPLASH_GREY);

    /* Text progress: no animated bar, just the boot terminal's own grammar. */
    text(buf, w, h, w / 2, y + 80, "[###########---------] 060%",
         3, SPLASH_ACCENT);
    text(buf, w, h, w / 2, y + 128, "BOOT> _", 2, SPLASH_ACCENT);
}