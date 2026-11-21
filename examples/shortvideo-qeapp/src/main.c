/* main.c - Short Video: a vertical short-clip feed as a native QEAPP.
 *
 * Framework API v5 (QEAPP native ABI 5) through the table only: the newest
 * revision adds `layout_split_weighted`, used here for the action rail and the
 * bottom tab bar; the v4 prefix supplies `draw_text_weighted`, the v3 prefix
 * supplies `read_file_part`/`blit_pixels`, and the frozen v2 prefix supplies
 * the log/draw_text/read_file calls. No libc, no syscall, no hardware.
 *
 * Canvas 720x1560 RGB565, the shell composites it 1:1. The status band
 * (y 0..55) is left a single flat colour so the shell's clock/battery overlay
 * lands there, exactly like examples/shellui-qeapp.
 *
 * A clip is real data, never a drawn placeholder: the app streams
 *   apps/org.vqeaf.shortvideo/@media/CLIP1.QVF  (CLIP2, CLIP3)
 * frame by frame with `read_file_part` and decodes the QVF1 LZ4 block format
 * (docs/QEAF_VIDEO_FORMAT.md). With no clip present the feed says so on screen
 * and keeps the chrome usable; it never substitutes a picture for video.
 *
 * Shape learned on hardware: all state is file-static with no static const
 * arrays, buffers are aligned(16) and written through a volatile pointer,
 * because qenlink resolves PC-relative references only and the arm64 gate runs
 * with the MMU off.
 */
#include <stddef.h>
#include <stdint.h>

#include "qeaf_api.h"
#include "qeaf_native_abi.h"

#define CANVAS_W 720
#define CANVAS_H 1560

#define STATUS_H 56
#define VIDEO_Y  STATUS_H                 /* 56   */
#define VIDEO_H  1236                     /* video band 56..1291 */
#define SHEET_Y  (VIDEO_Y + VIDEO_H)      /* 1292 */
#define SHEET_H  (CANVAS_H - SHEET_Y)     /* 268  */

#define TAB_Y      76
#define RAIL_X     672
#define CAPTION_X  32

/* Shell palette (platform/violet/ui/vqeaf_theme.c) as RGB565. */
#define COL_BG      0x0081u   /* 0x06100F */
#define COL_SURFACE 0x08C3u   /* 0x0C191B */
#define COL_RAISE   0x1125u   /* 0x112629 */
#define COL_LINE    0x19C7u   /* 0x1D3A3D */
#define COL_ACCENT  0x1F19u   /* 0x1EE0C8 */
#define COL_TEXT    0xFFFFu
#define COL_DIM     0x8C71u   /* 0x8A9694 */

#define BAND_ROWS 8
#define MAX_CLIPS 3
#define MAX_FRAMES 256
#define MAX_W 240
#define MAX_H 426
#define COMP_MAX (128u * 1024u)
#define QVF_HEADER 32u
#define QVF_ENTRY  8u

typedef struct {
    uint32_t off[MAX_FRAMES];
    int frames;
    int w, h, fps;
} clip_t;

typedef struct {
    const qeaf_app_api_v5 *api;
    int api_ok;
    int clip;                 /* index of the clip on screen       */
    int loaded;               /* clip_t entries that opened        */
    int frame;                /* frame index inside the clip       */
    int liked[MAX_CLIPS];
    int draw_count;
    clip_t clips[MAX_CLIPS];
    char line[64];
} feed_t;

static feed_t g_feed;
static uint16_t g_band[CANVAS_W * BAND_ROWS] __attribute__((aligned(16)));
static uint16_t g_raw[MAX_W * MAX_H] __attribute__((aligned(16)));
static uint16_t g_row[MAX_W * 4] __attribute__((aligned(16)));
static uint8_t g_comp[COMP_MAX] __attribute__((aligned(16)));
static uint32_t g_weights[4] __attribute__((aligned(16)));
static qeaf_rect_t g_rects[4] __attribute__((aligned(16)));

/* ---- small utility ------------------------------------------------------- */

static void feed_log(const feed_t *f, const char *message)
{
    if(f != NULL && f->api != NULL && f->api->log_utf8 != NULL)
        f->api->log_utf8(message);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t rd16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static int isqrt32(uint32_t v)
{
    uint32_t lo = 0, hi = 65535u, best = 0;
    while(lo <= hi) {
        uint32_t mid = (lo + hi) / 2u;
        if(mid * mid <= v) { best = mid; lo = mid + 1u; }
        else { if(mid == 0u) break; hi = mid - 1u; }
    }
    return (int)best;
}

static int text_width(const char *s)
{
    int n = 0;
    if(s == NULL) return 0;
    while(s[n] != '\0') n++;
    return n * 8;
}

/* ---- pixels -------------------------------------------------------------- */

static void fill_rect(int x, int y, int w, int h, uint16_t colour)
{
    volatile uint16_t *band = g_band;
    int i, row, rows;

    if(g_feed.api == NULL || g_feed.api->blit_pixels == NULL) return;
    if(x < 0) { w += x; x = 0; }
    if(y < 0) { h += y; y = 0; }
    if(x + w > CANVAS_W) w = CANVAS_W - x;
    if(y + h > CANVAS_H) h = CANVAS_H - y;
    if(w <= 0 || h <= 0) return;

    for(i = 0; i < CANVAS_W * BAND_ROWS; i++) band[i] = colour;
    for(row = 0; row < h; row += BAND_ROWS) {
        rows = h - row;
        if(rows > BAND_ROWS) rows = BAND_ROWS;
        g_feed.api->blit_pixels(x, y + row, w, rows, CANVAS_W, g_band);
    }
}

static void fill_circle(int cx, int cy, int r, uint16_t colour)
{
    int dy;
    for(dy = -r; dy <= r; dy++) {
        int dx = isqrt32((uint32_t)(r * r - dy * dy));
        fill_rect(cx - dx, cy + dy, dx * 2 + 1, 1, colour);
    }
}

static void fill_round_rect(int x, int y, int w, int h, int r, uint16_t colour)
{
    int row;
    if(r * 2 > w) r = w / 2;
    if(r * 2 > h) r = h / 2;
    fill_rect(x, y + r, w, h - r * 2, colour);
    for(row = 0; row < r; row++) {
        int dy = r - row;
        int inset = r - isqrt32((uint32_t)(r * r - dy * dy));
        fill_rect(x + inset, y + row, w - inset * 2, 1, colour);
        fill_rect(x + inset, y + h - 1 - row, w - inset * 2, 1, colour);
    }
}

static void fill_triangle_down(int cx, int y, int half, int height, uint16_t colour)
{
    int row;
    for(row = 0; row < height; row++) {
        int half_w = half - (half * row) / height;
        fill_rect(cx - half_w, y + row, half_w * 2, 1, colour);
    }
}

static void fill_triangle_right(int x, int cy, int height, uint16_t colour)
{
    int row;
    for(row = 0; row < height; row++) {
        int half = height / 2;
        int dist = row - half;
        if(dist < 0) dist = -dist;
        fill_rect(x + (half - dist), cy - half + row, (dist * 2), 1, colour);
    }
}

/* ---- text ---------------------------------------------------------------- */

static void text_at(int x, int y, const char *s, uint32_t weight)
{
    if(g_feed.api == NULL || s == NULL) return;
    if(g_feed.api->draw_text_weighted != NULL)
        g_feed.api->draw_text_weighted(x, y, s, weight);
    else
        g_feed.api->draw_text(x, y, s);
}

static void text_centred(int cx, int y, const char *s, uint32_t weight)
{
    text_at(cx - text_width(s) / 2, y, s, weight);
}

/* Draws at most `lines` rows of `chars` characters, breaking on spaces.
 * The caption strings are ASCII, which is all draw_text can rasterise. */
static int text_wrapped(int x, int y, const char *s, int chars, int lines,
                        uint32_t weight)
{
    int used = 0, row = 0;
    while(s != NULL && s[0] != '\0' && row < lines) {
        int n = 0, cut = 0;
        while(s[n] != '\0' && n < chars) {
            if(s[n] == ' ') cut = n;
            n++;
        }
        if(s[n] != '\0' && cut > 0) n = cut;
        used = 0;
        while(used < n && used < (int)sizeof(g_feed.line) - 1) {
            g_feed.line[used] = s[used];
            used++;
        }
        g_feed.line[used] = '\0';
        text_at(x, y + row * 20, g_feed.line, weight);
        s += n;
        while(s[0] == ' ') s++;
        row++;
    }
    return row;
}

/* Formats a view count the way a feed shows it: 12.4K / 1.2M. */
static void format_count(uint32_t v, const char *suffix, char *out)
{
    int i = 0;
    uint32_t whole, tenth;
    if(v >= 1000000u) {
        whole = v / 1000000u;
        tenth = (v / 100000u) % 10u;
        out[i++] = (char)('0' + (whole % 10u));
        out[i++] = '.';
        out[i++] = (char)('0' + tenth);
        out[i++] = 'M';
    }
    else if(v >= 1000u) {
        whole = v / 1000u;
        tenth = (v / 100u) % 10u;
        if(whole >= 100u)
            out[i++] = (char)('0' + ((whole / 100u) % 10u));
        if(whole >= 10u)
            out[i++] = (char)('0' + ((whole / 10u) % 10u));
        out[i++] = (char)('0' + (whole % 10u));
        out[i++] = '.';
        out[i++] = (char)('0' + tenth);
        out[i++] = 'K';
    }
    else {
        if(v >= 100u) out[i++] = (char)('0' + ((v / 100u) % 10u));
        if(v >= 10u)  out[i++] = (char)('0' + ((v / 10u) % 10u));
        out[i++] = (char)('0' + (v % 10u));
    }
    while(suffix != NULL && suffix[0] != '\0') { out[i++] = *suffix++; }
    out[i] = '\0';
}

/* ---- clip metadata (app data, ASCII: draw_text is an 8x8 bitmap face) ---- */

static const char *clip_path(int i)
{
    switch(i) {
    case 0: return "apps/org.vqeaf.shortvideo/@media/CLIP1.QVF";
    case 1: return "apps/org.vqeaf.shortvideo/@media/CLIP2.QVF";
    case 2: return "apps/org.vqeaf.shortvideo/@media/CLIP3.QVF";
    default: return NULL;
    }
}

static const char *clip_author(int i)
{
    switch(i) {
    case 0: return "@qeaf.builds";
    case 1: return "@violet.dev";
    default: return "@os.daily";
    }
}

static const char *clip_caption(int i)
{
    switch(i) {
    case 0: return "Build the OS, then ship the app on top of it.";
    case 1: return "720x1560 of shell, every boot from p77.";
    default: return "One feed, no dependencies beyond the ABI.";
    }
}

static const char *clip_sound(int i)
{
    switch(i) {
    case 0: return "original sound - qeaf.builds";
    case 1: return "original sound - violet.dev";
    default: return "original sound - os.daily";
    }
}

static uint32_t clip_likes(int i)
{
    switch(i) {
    case 0: return 128400u;
    case 1: return 42900u;
    default: return 9813u;
    }
}

static uint32_t clip_comments(int i)
{
    switch(i) {
    case 0: return 3021u;
    case 1: return 874u;
    default: return 210u;
    }
}

static uint32_t clip_shares(int i)
{
    switch(i) {
    case 0: return 8914u;
    case 1: return 1204u;
    default: return 331u;
    }
}

/* ---- QVF ---------------------------------------------------------------- */

static int lz4_decode(const uint8_t *src, uint32_t src_len, uint8_t *dst,
                      uint32_t dst_len)
{
    uint32_t sp = 0, dp = 0;
    while(sp < src_len) {
        uint32_t token = src[sp++];
        uint32_t lit = token >> 4;
        uint32_t match_len, offset, i;
        if(lit == 15u) {
            uint32_t add;
            do {
                if(sp >= src_len) return -1;
                add = src[sp++];
                lit += add;
            } while(add == 255u);
        }
        if(lit > 0) {
            if(sp + lit > src_len || dp + lit > dst_len) return -1;
            for(i = 0; i < lit; i++) dst[dp + i] = src[sp + i];
            sp += lit;
            dp += lit;
        }
        if(sp >= src_len) break;
        if(sp + 2 > src_len) return -1;
        offset = (uint32_t)src[sp] | ((uint32_t)src[sp + 1] << 8);
        sp += 2;
        if(offset == 0u || offset > dp) return -1;
        match_len = token & 0x0Fu;
        if(match_len == 15u) {
            uint32_t add;
            do {
                if(sp >= src_len) return -1;
                add = src[sp++];
                match_len += add;
            } while(add == 255u);
        }
        match_len += 4u;
        if(dp + match_len > dst_len) return -1;
        for(i = 0; i < match_len; i++) {
            dst[dp + i] = dst[dp - offset + i];
        }
        dp += match_len;
    }
    return dp == dst_len ? 0 : -1;
}

/* Opens `path`, validates the QVF1 header and records every frame offset.
 * Returns 1 when the clip is usable, 0 when the file is absent or rejected. */
static int clip_open(feed_t *f, int index)
{
    uint8_t header[QVF_HEADER];
    uint8_t entry[QVF_ENTRY];
    clip_t *c = &f->clips[index];
    uint32_t w, h, count, off, i;

    c->frames = 0;
    c->w = c->h = c->fps = 0;
    if(f->api->read_file_part == NULL) return 0;
    if(f->api->read_file_part(clip_path(index), 0, QVF_HEADER, header) != 0)
        return 0;
    if(header[0] != 'Q' || header[1] != 'V' || header[2] != 'F' ||
       header[3] != '1')
        return 0;
    if(rd16(header + 4) != 1u || rd16(header + 6) != 1u) return 0;
    w = rd32(header + 8);
    h = rd32(header + 12);
    count = rd32(header + 20);
    if(rd32(header + 24) != 0u) return 0;
    if(w == 0u || h == 0u || w > MAX_W || h > MAX_H) return 0;
    if(count == 0u || count > MAX_FRAMES) return 0;

    off = QVF_HEADER;
    for(i = 0; i < count; i++) {
        uint32_t raw_size, comp_size;
        if(f->api->read_file_part(clip_path(index), off, QVF_ENTRY, entry) != 0)
            return 0;
        raw_size = rd32(entry);
        comp_size = rd32(entry + 4);
        if(raw_size != w * h * 2u || comp_size == 0u || comp_size > COMP_MAX)
            return 0;
        c->off[i] = off;
        off += QVF_ENTRY + comp_size;
    }
    c->frames = (int)count;
    c->w = (int)w;
    c->h = (int)h;
    c->fps = (int)rd16(header + 16);
    return 1;
}

/* Blits frame `index` of the current clip, scaled to the full 720 width. */
static int clip_blit_frame(feed_t *f)
{
    clip_t *c = &f->clips[f->clip];
    uint8_t entry[QVF_ENTRY];
    uint32_t raw_size, comp_size, scale, sy;
    int scaled_w, scaled_h, x, y, col, px;

    if(c->frames <= 0) return -1;
    if(f->frame < 0) f->frame = 0;
    if(f->frame >= c->frames) f->frame = c->frames - 1;

    if(f->api->read_file_part(clip_path(f->clip), c->off[f->frame],
                              QVF_ENTRY, entry) != 0) return -1;
    raw_size = rd32(entry);
    comp_size = rd32(entry + 4);
    if(raw_size != (uint32_t)c->w * (uint32_t)c->h * 2u ||
       comp_size == 0u || comp_size > COMP_MAX) return -1;
    if(f->api->read_file_part(clip_path(f->clip), c->off[f->frame] + QVF_ENTRY,
                              comp_size, g_comp) != 0) return -1;
    if(lz4_decode(g_comp, comp_size, (uint8_t *)g_raw,
                  (uint32_t)c->w * (uint32_t)c->h * 2u) != 0) return -1;

    /* Cover-fit: the clip fills the whole video band like a feed, the excess
     * is cropped (never letterboxed). */
    scale = (uint32_t)CANVAS_W / (uint32_t)c->w;
    if((uint32_t)VIDEO_H / (uint32_t)c->h > scale)
        scale = (uint32_t)VIDEO_H / (uint32_t)c->h;
    if(scale == 0u) scale = 1u;
    scaled_w = (int)((uint32_t)c->w * scale);
    scaled_h = (int)((uint32_t)c->h * scale);
    if(scaled_w > CANVAS_W) scaled_w = CANVAS_W;
    if(scaled_h > VIDEO_H) scaled_h = VIDEO_H;
    x = (CANVAS_W - (int)((uint32_t)c->w * scale)) / 2;
    y = VIDEO_Y + (VIDEO_H - (int)((uint32_t)c->h * scale)) / 2;

    for(sy = 0; sy < (uint32_t)c->h; sy++) {
        const uint16_t *src = &g_raw[sy * (uint32_t)c->w];
        int srow;
        for(srow = 0; srow < (int)scale; srow++) {
            int row = (int)(sy * scale) + srow;
            if(y + row < VIDEO_Y || y + row >= VIDEO_Y + VIDEO_H) continue;
            px = 0;
            for(col = 0; col < c->w; col++) {
                int rep;
                for(rep = 0; rep < (int)scale && px < MAX_W * 4; rep++)
                    g_row[px++] = src[col];
            }
            if(f->api->blit_pixels(x, y + row, px, 1, px, g_row) != 0)
                return -1;
        }
    }
    return 0;
}

/* ---- chrome -------------------------------------------------------------- */

static void draw_tabs(void)
{
    text_at(CANVAS_W / 2 - 96, TAB_Y, "Following", QEAF_TEXT_WEIGHT_REGULAR);
    text_at(CANVAS_W / 2 + 24, TAB_Y, "For You", QEAF_TEXT_WEIGHT_BOLD);
    fill_rect(CANVAS_W / 2 + 40, TAB_Y + 14, 56, 3, COL_TEXT);
}

static void draw_avatar(int cx, int cy, int r)
{
    fill_circle(cx, cy, r, COL_TEXT);
    fill_circle(cx, cy, r - 3, COL_SURFACE);
    fill_circle(cx, cy - 4, r / 2 - 1, COL_DIM);
    fill_rect(cx - r / 2, cy + 2, r, r - 2, COL_DIM);
}

static void draw_heart(int cx, int cy, int s, uint16_t colour)
{
    int r = s / 2;
    fill_circle(cx - r + 1, cy - r / 2, r - 1, colour);
    fill_circle(cx + r - 1, cy - r / 2, r - 1, colour);
    fill_triangle_down(cx, cy - r / 2, r, r + 2, colour);
}

static void draw_bubble(int cx, int cy, int s, uint16_t colour)
{
    int w = s, h = s - 2;
    fill_round_rect(cx - w / 2, cy - h / 2, w, h, 5, colour);
    fill_triangle_down(cx - w / 4, cy + h / 2 - 1, 4, 5, colour);
    fill_rect(cx - w / 2 + 4, cy - h / 2 + 3, w - 8, 2, COL_BG);
    fill_rect(cx - w / 2 + 4, cy - h / 2 + 7, w - 12, 2, COL_BG);
}

static void draw_share_arrow(int cx, int cy, int s, uint16_t colour)
{
    fill_triangle_right(cx + s / 2 - 7, cy - 3, 12, colour);
    fill_rect(cx - s / 2 + 2, cy - 4, s - 8, 3, colour);
}

static void draw_rail_button(int cx, int y, int kind, uint32_t count,
                             uint16_t colour)
{
    char buf[24];
    fill_circle(cx, y, 30, COL_RAISE);
    if(kind == 0) draw_heart(cx, y - 1, 26, colour);
    else if(kind == 1) draw_bubble(cx, y - 1, 24, colour);
    else draw_share_arrow(cx, y - 1, 24, colour);
    if(count > 0u) {
        format_count(count, NULL, buf);
        text_centred(cx, y + 34, buf, QEAF_TEXT_WEIGHT_REGULAR);
    }
}

static void draw_rail(feed_t *f)
{
    int c = f->clip;
    draw_avatar(RAIL_X, 900, 26);
    draw_rail_button(RAIL_X, 1000, 0, clip_likes(c), f->liked[c] ? COL_ACCENT : COL_TEXT);
    draw_rail_button(RAIL_X, 1096, 1, clip_comments(c), COL_TEXT);
    draw_rail_button(RAIL_X, 1192, 2, clip_shares(c), COL_TEXT);
}

static void draw_sheet(feed_t *f)
{
    int c = f->clip;
    char buf[24];
    int progress = 0;

    fill_rect(0, SHEET_Y, CANVAS_W, SHEET_H, COL_BG);
    if(f->clips[c].frames > 0)
        progress = (CANVAS_W * (f->frame + 1)) / f->clips[c].frames;
    if(progress > 0) fill_rect(0, SHEET_Y, progress, 3, COL_ACCENT);

    text_at(CAPTION_X, SHEET_Y + 20, clip_author(c), QEAF_TEXT_WEIGHT_BOLD);
    text_wrapped(CAPTION_X, SHEET_Y + 42, clip_caption(c), 52, 2,
                 QEAF_TEXT_WEIGHT_REGULAR);
    text_at(CAPTION_X, SHEET_Y + 92, clip_sound(c), QEAF_TEXT_WEIGHT_REGULAR);

    format_count(clip_likes(c), " likes", buf);
    text_at(CAPTION_X, SHEET_Y + 116, buf, QEAF_TEXT_WEIGHT_REGULAR);

    /* v5: the four bottom tabs are laid out with the shared weight split, the
     * same proportional semantics as the shell's weighted_row. */
    g_weights[0] = 1u; g_weights[1] = 1u; g_weights[2] = 1u; g_weights[3] = 1u;
    if(f->api != NULL && f->api->layout_split_weighted != NULL &&
       f->api->layout_split_weighted(0, SHEET_Y + 168, CANVAS_W, 96,
                                         QEAF_LAYOUT_AXIS_HORIZONTAL,
                                         g_weights, 4u, 8u, g_rects) == 0) {
        text_centred(g_rects[0].x + g_rects[0].w / 2, g_rects[0].y + 12,
                     "Home", QEAF_TEXT_WEIGHT_BOLD);
        text_centred(g_rects[1].x + g_rects[1].w / 2, g_rects[1].y + 12,
                     "Discover", QEAF_TEXT_WEIGHT_REGULAR);
        text_centred(g_rects[2].x + g_rects[2].w / 2, g_rects[2].y + 12,
                     "Friends", QEAF_TEXT_WEIGHT_REGULAR);
        text_centred(g_rects[3].x + g_rects[3].w / 2, g_rects[3].y + 12,
                     "Profile", QEAF_TEXT_WEIGHT_REGULAR);
        fill_rect(g_rects[0].x + g_rects[0].w / 2 - 12, g_rects[0].y + 30,
                  24, 3, COL_ACCENT);
    }
    else {
        text_centred(CANVAS_W / 2, SHEET_Y + 190, "Home", QEAF_TEXT_WEIGHT_BOLD);
    }
}

static void draw_empty(feed_t *f)
{
    uint16_t band;
    int row;

    /* A vertical gradient, drawn band by band: honest "no media" surface. */
    for(row = 0; row < VIDEO_H; row += BAND_ROWS) {
        int t = (row * 5) / VIDEO_H;
        band = (uint16_t)(COL_SURFACE + (uint16_t)(t * 0x0210u));
        fill_rect(0, VIDEO_Y + row, CANVAS_W, BAND_ROWS, band);
    }
    fill_circle(CANVAS_W / 2, VIDEO_Y + 420, 64, COL_RAISE);
    fill_triangle_right(CANVAS_W / 2 - 14, VIDEO_Y + 420, 44, COL_DIM);
    text_centred(CANVAS_W / 2, VIDEO_Y + 520, "NO CLIP YET",
                 QEAF_TEXT_WEIGHT_BOLD);
    text_centred(CANVAS_W / 2, VIDEO_Y + 556,
                 "Copy a QVF clip to app storage:", QEAF_TEXT_WEIGHT_REGULAR);
    text_centred(CANVAS_W / 2, VIDEO_Y + 580,
                 "apps/org.vqeaf.shortvideo/@media/CLIP1.QVF",
                 QEAF_TEXT_WEIGHT_REGULAR);
    text_centred(CANVAS_W / 2, VIDEO_Y + 612,
                 "Keys: UP/DOWN switch clip, L like", QEAF_TEXT_WEIGHT_REGULAR);
    feed_log(f, "shortvideo: no clip in app storage");
}

static void feed_draw(feed_t *f)
{
    fill_rect(0, 0, CANVAS_W, STATUS_H, COL_BG);
    if(f->clips[f->clip].frames > 0 && clip_blit_frame(f) == 0) {
        f->loaded = 1;
    }
    else {
        f->loaded = 0;
        draw_empty(f);
    }
    draw_tabs();
    draw_rail(f);
    draw_sheet(f);
    f->draw_count++;
}

/* ---- lifecycle ----------------------------------------------------------- */

void *create(const qeaf_app_api_v2 *api)
{
    feed_t *f = &g_feed;
    int i;

    if(api == NULL || api->abi_version != VQEAF_APP_API_V5_VERSION) return NULL;
    f->api = (const qeaf_app_api_v5 *)api;
    f->api_ok = 1;
    f->clip = 0;
    f->loaded = 0;
    f->frame = 0;
    f->draw_count = 0;
    for(i = 0; i < MAX_CLIPS; i++) {
        f->liked[i] = 0;
        f->clips[i].frames = 0;
        f->clips[i].w = f->clips[i].h = f->clips[i].fps = 0;
    }
    feed_log(f, "shortvideo: create (api v5)");
    return f;
}

int start(void *handle)
{
    feed_t *f = (feed_t *)handle;
    int i, opened = 0;

    if(f == NULL || f->api == NULL) return -1;
    for(i = 0; i < MAX_CLIPS; i++) {
        if(clip_open(f, i)) opened++;
    }
    if(opened == 0) f->clip = 0;
    else {
        while(f->clip < MAX_CLIPS && f->clips[f->clip].frames <= 0) f->clip++;
        if(f->clip >= MAX_CLIPS) f->clip = 0;
    }
    f->frame = 0;
    feed_draw(f);
    {
        char note[48];
        int n = 0;
        const char *base = "shortvideo: clips=";
        while(base[n] != '\0' && n < 40) { note[n] = base[n]; n++; }
        note[n++] = (char)('0' + opened);
        note[n] = '\0';
        feed_log(f, note);
    }
    return 0;
}

int pause(void *handle)
{
    (void)handle;
    return 0;
}

int resume(void *handle)
{
    feed_t *f = (feed_t *)handle;
    if(f == NULL) return -1;
    feed_draw(f);
    return 0;
}

int stop(void *handle)
{
    (void)handle;
    return 0;
}

void on_input(void *handle, const qeaf_input_event_t *event)
{
    feed_t *f = (feed_t *)handle;
    int next;

    if(f == NULL || event == NULL || !event->pressed) return;
    if(event->type != QEAF_INPUT_KEY) return;

    switch(event->code) {
    case 108u:   /* DOWN */
    case 4u:     /* RIGHT */
    case 5u:     /* OK */
        next = f->clip;
        do {
            next = (next + 1) % MAX_CLIPS;
        } while(f->clips[next].frames <= 0 && next != f->clip);
        if(f->clips[next].frames > 0) { f->clip = next; f->frame = 0; }
        feed_draw(f);
        break;
    case 103u:   /* UP */
    case 3u:     /* LEFT */
        next = f->clip;
        do {
            next = (next + MAX_CLIPS - 1) % MAX_CLIPS;
        } while(f->clips[next].frames <= 0 && next != f->clip);
        if(f->clips[next].frames > 0) { f->clip = next; f->frame = 0; }
        feed_draw(f);
        break;
    case 38u:    /* 'l' */
        f->liked[f->clip] = !f->liked[f->clip];
        feed_draw(f);
        break;
    default:
        break;
    }
}

void destroy(void *handle)
{
    feed_t *f = (feed_t *)handle;
    if(f == NULL) return;
    feed_log(f, "shortvideo: destroy");
    f->api_ok = 0;
}
