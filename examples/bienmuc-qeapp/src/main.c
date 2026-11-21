/* main.c - "Bien Muc" (Ink Seas), native QEAPP port.
 *
 * Direct port of the Java ME game in D:\Program\gamejava\Chetaslua-JAR
 * (bienmuc.Art / Ocean / G / Save / Ui / screens.PlayScreen) to the QEAPP
 * native ABI. The Java design canvas is 240x320, so this module keeps a real
 * 240x320 RGB565 framebuffer, draws the same doodle-on-notebook-paper scene
 * with its own raster primitives, then upscales it 3x into the 720x1560 app
 * canvas with blit_pixels.
 *
 * What is ported 1:1 from the Java source:
 *   - Ocean.height / slope / draw / splash (same constants and phases)
 *   - Art: stroke jitter (same LCG), fill, outline, hatch, o_dot, gon,
 *     polygon shade (span sweep), heart, compass, meter, pen, scribble,
 *     banner, ship, fish, tentacle, rock, cloud, rain, bolt, blob, ball, paper
 *   - PlayScreen: player physics, ink economy, shield/rail/strike, enemy table
 *     and waves, aimed arcs, collisions, drops, fx, score, grade, pause and
 *     game-over screens, HUD
 *   - Save.grade_for and the pen unlock rules (kept in RAM: a QEAPP cannot
 *     write storage on this OS yet, so high scores/currency do not persist).
 *
 * Deliberate differences, stated instead of hidden:
 *   - Text uses the OS 8x8 ASCII face (embedding it keeps the module
 *     freestanding). Vietnamese labels are therefore written without
 *     diacritics, exactly like the other native QEAPP samples.
 *   - No audio: the ABI has no audio call and the Java build used MIDI/WAV.
 *   - The QEAPP ABI has no frame/timer callback, so one on_input event
 *     advances the simulation by one 1/30 s step and redraws. Keyboard
 *     auto-repeat in the host simulator's window therefore acts as the frame
 *     clock. A plain launch renders the starting scene only.
 */
#include <stddef.h>
#include <stdint.h>

#include "qeaf_api.h"
#include "qeaf_native_abi.h"
#include "font8x8.h"

/* ---- canvas -------------------------------------------------------------- */

#define GAME_W 240
#define GAME_H 320
#define SCALE  3
#define CANVAS_W 720
#define CANVAS_H 1560
#define OY ((CANVAS_H - GAME_H * SCALE) / 2)   /* 300 */

#define SEA_BASE 190.0f
#define SHIP_X   74.0f
#define GFORCE   130.0f
#define SHOT_V   150.0f
#define INK_MAX  100.0f
#define INK_COST 10.0f
#define INK_REGEN 10.0f
#define SHIELD_COST 45.0f
#define RAIL_COST   35.0f
#define SHIELD_TIME 8.0f
#define RAIL_TIME   6.5f
#define BASE_SPEED  34.0f
#define MAX_FOES    9
#define DT (1.0f / 30.0f)

#define PI_F 3.14159265358979f

/* Palette, RGB565 of the Java Art.rgb() constants. */
#define COL_PAPER  0xFFDDu
#define COL_RULE   0xBE9Cu
#define COL_MARGIN 0xE555u
#define COL_HOLE   0xE6FAu
#define COL_PEN    0x19B1u
#define COL_PEN2   0x5398u
#define COL_INK    0x10C4u
#define COL_LEAD   0x6B6Fu
#define COL_LEAD2  0xA515u
#define COL_RED    0xC125u
#define COL_RED2   0xEC2Fu
#define COL_WATER  0xD73Eu
#define COL_WATER2 0xB67Cu
#define COL_WATER3 0x8D9Au
#define COL_GREEN  0x2C2Au
#define COL_YELLOW 0xEDA6u
#define COL_WHITE  0xFFFFu
#define COL_SKY    0xEF9Eu
#define COL_SHADOW 0xCEDCu

/* ---- framebuffer --------------------------------------------------------- */

static const qeaf_app_api_v5 *g_api;

/* Both buffers are written through volatile pointers: with the MMU off (the
 * arm64 QEMU gate, and early boot) memory is Device type and an unaligned
 * access faults, so the compiler must never fuse these 16-bit stores into a
 * wider vector store. Same lesson as examples/terminal-qeapp. */
static uint16_t g_fb[GAME_W * GAME_H] __attribute__((aligned(16)));
static uint16_t g_band[CANVAS_W * 3] __attribute__((aligned(16)));

static void fb_px(int x, int y, uint16_t c)
{
    volatile uint16_t *fb = g_fb;

    if((unsigned)x >= GAME_W || (unsigned)y >= GAME_H) return;
    fb[y * GAME_W + x] = c;
}

static void fb_rect(int x, int y, int w, int h, uint16_t c)
{
    int row, col;

    if(w <= 0 || h <= 0) return;
    if(x < 0) { w += x; x = 0; }
    if(y < 0) { h += y; y = 0; }
    if(x + w > GAME_W) w = GAME_W - x;
    if(y + h > GAME_H) h = GAME_H - y;
    if(w <= 0 || h <= 0) return;
    for(row = 0; row < h; row++) {
        volatile uint16_t *line = &g_fb[(y + row) * GAME_W + x];
        for(col = 0; col < w; col++) line[col] = c;
    }
}

static void fb_line_i(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int err, guard = 0;

    if(dx < 0) dx = -dx;
    if(dy < 0) dy = -dy;
    err = (dx > dy ? dx : -dy) / 2;
    for(;;) {
        fb_px(x0, y0, c);
        if(x0 == x1 && y0 == y1) break;
        if(++guard > 4096) break;
        {
            int e2 = err;
            if(e2 >= -dx) { err -= dy; x0 += sx; }
            if(e2 <= dy)  { err += dx; y0 += sy; }
        }
    }
}

/* ---- math (no libm in a freestanding module) ----------------------------- */

static float qabsf(float v) { return v < 0.0f ? -v : v; }

static float qsin(float x)
{
    float x2;
    int k = (int)(x / (2.0f * PI_F));

    x -= (float)k * 2.0f * PI_F;
    if(x > PI_F) x -= 2.0f * PI_F;
    else if(x < -PI_F) x += 2.0f * PI_F;
    x2 = x * x;
    /* 9th-order Taylor on [-pi,pi]: max error ~6e-3, enough for 1 px art. */
    return x * (1.0f - x2 / 6.0f + x2 * x2 / 120.0f -
                x2 * x2 * x2 / 5040.0f + x2 * x2 * x2 * x2 / 362880.0f);
}

static float qcos(float x) { return qsin(x + PI_F * 0.5f); }

static float qsqrt_f(float v)
{
    float r;
    int i;

    if(v <= 0.0f) return 0.0f;
    r = v > 1.0f ? v * 0.5f : 1.0f;
    for(i = 0; i < 16; i++) r = 0.5f * (r + v / r);
    return r;
}

static int iround(float v) { return (int)(v + (v >= 0.0f ? 0.5f : -0.5f)); }

static float clampf(float v, float a, float b)
{
    if(v < a) return a;
    if(v > b) return b;
    return v;
}

/* ---- text ---------------------------------------------------------------- */

static int g_adv = 6;
static int g_bold;

static int slen(const char *s)
{
    int n = 0;
    if(s == NULL) return 0;
    while(s[n]) n++;
    return n;
}

static void text_at(int x, int y, const char *s, uint16_t c)
{
    const unsigned char *p = (const unsigned char *)s;
    int cx = x;

    if(s == NULL) return;
    for(; *p; p++) {
        unsigned char ch = *p < 128 ? *p : (unsigned char)'?';
        const uint8_t *glyph = vqeaf_font8x8[ch];
        int row, col;

        for(row = 0; row < 8; row++) {
            for(col = 0; col < 8 && col < g_adv; col++) {
                if(!(glyph[row] & (1u << col))) continue;
                fb_px(cx + col, y + row, c);
                if(g_bold) fb_px(cx + col + 1, y + row, c);
            }
        }
        cx += g_adv;
    }
}

static int tw(const char *s) { return slen(s) * g_adv; }

static void ctext(int cx, int y, const char *s, uint16_t c)
{
    text_at(cx - tw(s) / 2, y, s, c);
}

static void rtext(int rx, int y, const char *s, uint16_t c)
{
    text_at(rx - tw(s), y, s, c);
}

static void set_font(int n)
{
    if(n <= 8) { g_adv = 6; g_bold = 0; }
    else       { g_adv = 8; g_bold = 1; }
}

/* ---- Art ----------------------------------------------------------------- */

static int   g_jcount;
static int   g_jseed = 7;
static int   g_scr[32];
static int   g_scr_n;

static float jr(void)
{
    uint32_t x;

    g_jcount++;
    x = (uint32_t)g_jcount * 1103515245u + (uint32_t)g_jseed * 12345u + 2531011u;
    x %= 2147483648u;
    x = (x / 65536u) % 32768u;
    return (float)x / 16384.0f - 1.0f;
}

static void jit_reset(int seed)
{
    g_jcount = 0;
    g_jseed = seed;
}

static void stroke(float x1, float y1, float x2, float y2, uint16_t c, float amp)
{
    float dx = x2 - x1, dy = y2 - y1;

    if((x1 < 0.0f && x2 < 0.0f) || (x1 > GAME_W - 1 && x2 > GAME_W - 1) ||
       (y1 < 0.0f && y2 < 0.0f) || (y1 > GAME_H - 1 && y2 > GAME_H - 1))
        return;
    if(amp <= 0.0f) {
        fb_line_i(iround(x1), iround(y1), iround(x2), iround(y2), c);
        return;
    }
    {
        float len = qsqrt_f(dx * dx + dy * dy);
        int n = (int)(len / 13.0f) + 1;
        float px = x1, py = y1;
        int i;

        if(n > 5) n = 5;
        if(n <= 1) {
            fb_line_i(iround(x1), iround(y1), iround(x2), iround(y2), c);
            return;
        }
        for(i = 1; i <= n; i++) {
            float tt = (float)i / (float)n;
            float nx = x1 + dx * tt;
            float ny = y1 + dy * tt;
            if(i < n) {
                nx += jr() * amp;
                ny += jr() * amp;
            }
            fb_line_i(iround(px), iround(py), iround(nx), iround(ny), c);
            px = nx;
            py = ny;
        }
    }
}

static void outline(float x, float y, float w, float h, uint16_t c, float amp)
{
    float x2 = x + w, y2 = y + h;

    stroke(x, y, x2, y, c, amp);
    stroke(x2, y, x2, y2, c, amp);
    stroke(x2, y2, x, y2, c, amp);
    stroke(x, y2, x, y, c, amp);
}

static void afill(float x, float y, float w, float h, uint16_t c)
{
    float x2 = x + w, y2 = y + h;

    if(w <= 0.0f || h <= 0.0f) return;
    if(x < 0.0f) x = 0.0f;
    if(y < 0.0f) y = 0.0f;
    if(x2 > GAME_W) x2 = GAME_W;
    if(y2 > GAME_H) y2 = GAME_H;
    if(x2 <= x || y2 <= y) return;
    {
        int sx = iround(x), sy = iround(y);
        int sw = iround(x2) - sx, sh = iround(y2) - sy;

        if(sw < 1) sw = 1;
        if(sh < 1) sh = 1;
        fb_rect(sx, sy, sw, sh, c);
    }
}

static void hatch(float x, float y, float w, float h, uint16_t c, int step, int dir)
{
    int ix = (int)x, iy = (int)y, iw = (int)w, ih = (int)h;
    int i;

    if(step < 1) step = 4;
    if(iw <= 1 || ih <= 1) return;
    for(i = -ih; i < iw; i += step) {
        int xa, ya, xb, yb;

        if(dir > 0) { xa = ix + i; ya = iy; xb = ix + i + ih; yb = iy + ih; }
        else        { xa = ix + i + ih; ya = iy; xb = ix + i; yb = iy + ih; }
        if(xa < ix) { ya = iy + (ix - xa); xa = ix; }
        if(xb > ix + iw) { yb = iy + ih - (xb - (ix + iw)); xb = ix + iw; }
        if(xa < xb && xa >= 0 && xb <= GAME_W && ya >= 0 && yb <= GAME_H)
            fb_line_i(xa, ya, xb, yb, c);
    }
}

static void o_dot(float cx, float cy, float r, uint16_t c)
{
    int n = 6, i;
    float px = 0.0f, py = 0.0f, first_x = 0.0f, first_y = 0.0f;

    for(i = 0; i <= n; i++) {
        float a = (float)i / (float)n * 6.2831853f;
        float x = cx + qcos(a) * r;
        float y = cy + qsin(a) * r;

        if(i == 0) { px = x; py = y; first_x = x; first_y = y; }
        else if(i == n) { stroke(px, py, first_x, first_y, c, 0.0f); }
        else { stroke(px, py, x, y, c, 0.0f); px = x; py = y; }
    }
}

static void gon(float cx, float cy, float r, int n, uint16_t c, float rot)
{
    float px = 0.0f, py = 0.0f;
    int i;

    for(i = 0; i <= n; i++) {
        float a = rot + (float)i / (float)n * 6.2831853f;
        float x = cx + qcos(a) * r;
        float y = cy + qsin(a) * r;

        if(i > 0) stroke(px, py, x, y, c, 0.0f);
        px = x;
        py = y;
    }
}

/* Transform a point list (x,y pairs): same as Art.tf. */
static int *tf(const int *pts, int count, float cx, float cy, float dir, float shear)
{
    int i;

    for(i = 0; i < count; i += 2) {
        float x = (float)pts[i] * dir;
        g_scr[i] = (int)(cx + x);
        g_scr[i + 1] = (int)(cy + (float)pts[i + 1] + x * shear);
    }
    g_scr_n = count / 2;
    return g_scr;
}

static void polyline(const int *pts, int n_verts, uint16_t c, float amp, int closed)
{
    int lim = closed ? n_verts + 1 : n_verts;
    int i;

    if(n_verts < 2) return;
    for(i = 1; i < lim; i++) {
        int a = (i - 1) * 2;
        int j = i + 1;
        int b;

        if(j > n_verts) j = 1;
        b = (j - 1) * 2;
        stroke((float)pts[a], (float)pts[a + 1],
               (float)pts[b], (float)pts[b + 1], c, amp);
    }
}

static void span(const int *pts, int n_verts, float y, float *out)
{
    float xmin = 0.0f, xmax = 0.0f;
    int any = 0, i;

    for(i = 1; i <= n_verts; i++) {
        int j = i + 1;
        float ax, ay, bx, by;

        if(j > n_verts) j = 1;
        ax = (float)pts[(i - 1) * 2];
        ay = (float)pts[(i - 1) * 2 + 1];
        bx = (float)pts[(j - 1) * 2];
        by = (float)pts[(j - 1) * 2 + 1];
        if((ay <= y && by > y) || (by <= y && ay > y)) {
            float t = (y - ay) / (by - ay);
            float x = ax + (bx - ax) * t;

            if(!any) { xmin = x; xmax = x; any = 1; }
            else { if(x < xmin) xmin = x; if(x > xmax) xmax = x; }
        }
    }
    out[0] = any ? xmin : -99999.0f;
    out[1] = any ? xmax : -99999.0f;
}

static void shade(const int *pts, int n_verts, float y0, float y1, int step,
                  uint16_t c, int dir)
{
    float out[2];
    float y;

    if(step < 1) step = 3;
    for(y = y0; y <= y1; y += (float)step) {
        span(pts, n_verts, y, out);
        if(out[0] > -90000.0f && (out[1] - out[0]) > 2.0f) {
            float xa = out[0], xb = out[1];
            float len = xb - xa;
            float k = xa + 1.0f + (dir > 0 ? 0.0f : len * 0.5f);
            float seg = len * 0.55f;
            float xe;

            if(seg > (float)step * 2.0f) seg = (float)step * 2.0f;
            xe = k + seg;
            if(xe > xb - 1.0f) xe = xb - 1.0f;
            if(k < xa + 0.5f) k = xa + 0.5f;
            stroke(k, y, xe, y + 3.0f, c, 0.0f);
        }
    }
}

static void heart(float cx, float cy, float s, uint16_t c, int filled, int broken)
{
    int pts[26];
    int *p;
    int nv;

    pts[0] = -6;  pts[1] = 0;
    pts[2] = -5;  pts[3] = -3;
    pts[4] = -3;  pts[5] = -5;
    pts[6] = -1;  pts[7] = -5;
    pts[8] = 0;   pts[9] = -3;
    pts[10] = 1;  pts[11] = -5;
    pts[12] = 3;  pts[13] = -5;
    pts[14] = 5;  pts[15] = -3;
    pts[16] = 6;  pts[17] = 0;
    pts[18] = 3;  pts[19] = 4;
    pts[20] = 0;  pts[21] = 7;
    pts[22] = -3; pts[23] = 4;
    pts[24] = -6; pts[25] = 0;

    p = tf(pts, 26, cx, cy, s, 0.0f);
    nv = g_scr_n;
    if(filled) shade(p, nv, cy - 4.0f * s, cy + 6.0f * s, 3, c, 1);
    polyline(p, nv, c, 0.4f * s, 1);
    if(broken) {
        stroke(cx - 1.0f, cy - 4.0f, cx + 3.0f, cy, COL_INK, 0.0f);
        stroke(cx + 3.0f, cy, cx - 2.0f, cy + 5.0f, COL_INK, 0.0f);
    }
}

static void hearts(float x, float y, int hp, int max_hp, float s)
{
    int i;

    for(i = 1; i <= max_hp; i++) {
        if(i <= hp) heart(x + (float)(i - 1) * (14.0f * s), y, s, COL_RED, 1, 0);
        else        heart(x + (float)(i - 1) * (14.0f * s), y, s, COL_LEAD2, 0, 0);
    }
}

static void compass(float cx, float cy, float r, float wind)
{
    float dx;

    gon(cx, cy, r, 8, COL_INK, 0.2f);
    stroke(cx, cy - r, cx, cy + r, COL_LEAD2, 0.0f);
    stroke(cx - r, cy, cx + r, cy, COL_LEAD2, 0.0f);
    text_at(iround(cx - 2.0f), iround(cy - r - 2.0f), "N", COL_LEAD);
    dx = wind * r * 0.8f;
    stroke(cx, cy, cx + dx, cy - 2.0f, COL_RED, 0.0f);
    {
        float hx = cx + dx;
        float tip = wind >= 0.0f ? 1.0f : -1.0f;
        stroke(hx, cy - 2.0f, hx - 3.0f * tip, cy - 5.0f, COL_RED, 0.0f);
        stroke(hx, cy - 2.0f, hx - 3.0f * tip, cy + 1.0f, COL_RED, 0.0f);
    }
}

static void meter(float x, float y, float w, float h, float pct,
                  uint16_t fill_c, uint16_t edge_c, uint16_t hatch_c)
{
    int fw;
    float i;

    afill(x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, COL_PAPER);
    outline(x, y, w, h, edge_c, 0.7f);
    fw = (int)((w - 2.0f) * pct + 0.5f);
    if(fw > 0) {
        afill(x + 1.0f, y + 1.0f, (float)fw, h - 2.0f, fill_c);
        for(i = x + 2.0f; i < x + 1.0f + (float)fw; i += 5.0f)
            fb_line_i((int)i, iround(y + h - 2.0f), (int)i + 3, iround(y + 2.0f),
                      hatch_c);
    }
}

static void pen_icon(float x, float y, float up, uint16_t c)
{
    float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
    int has = 0, i;

    if(up == 0.0f) up = 1.0f;
    if(c == 0) c = COL_PEN;
    for(i = 1; i <= 6; i++) {
        float yy = y - (float)i * 5.0f * up;
        float dx = ((float)i - 3.0f) * 0.5f * up;
        float w = 1.0f + (float)i * 0.45f;

        if(has) {
            stroke(lx, ly, x + dx - w, yy, c, 0.0f);
            stroke(rx, ry, x + dx + w, yy, c, 0.0f);
        }
        lx = x + dx - w; ly = yy;
        rx = x + dx + w; ry = yy;
        has = 1;
        if(i == 2 || i == 4) stroke(lx, ly, rx, ry, COL_LEAD2, 0.0f);
    }
    stroke(x - 1.5f, y - 4.0f * up, x, y, c, 0.0f);
    stroke(x + 1.5f, y - 4.0f * up, x, y, c, 0.0f);
    afill(x - 1.0f, y, 2.0f, 2.0f, COL_PEN);
    stroke(lx, ly, rx, ry, c, 0.0f);
}

static void scribble(float x, float y, float w, float h, uint16_t c, float seed)
{
    int gen[10];
    int i;

    for(i = 0; i < 4; i++) {
        float yy = y + h * (float)i / 4.0f;

        gen[0] = (int)x;
        gen[1] = (int)(yy + 2.0f);
        gen[2] = (int)(x + w * 0.3f);
        gen[3] = (int)(yy - 4.0f + seed);
        gen[4] = (int)(x + w * 0.55f);
        gen[5] = (int)(yy + 5.0f);
        gen[6] = (int)(x + w * 0.8f);
        gen[7] = (int)(yy - 3.0f);
        gen[8] = (int)(x + w);
        gen[9] = (int)(yy + 1.0f);
        polyline(gen, 5, c, 1.6f, 0);
    }
    gon(x + 2.0f, y + h / 2.0f, 5.0f, 6, c, 0.5f);
    gon(x + w - 4.0f, y + h / 2.0f, 5.0f, 6, c, 1.1f);
}

static void banner(float y, const char *s, uint16_t c, uint16_t bg)
{
    float w;

    if(bg != 0) afill(8.0f, y - 4.0f, (float)GAME_W - 16.0f, 20.0f, bg);
    set_font(8);
    w = (float)slen(s) * 11.0f;
    text_at(iround((float)GAME_W / 2.0f - w / 2.0f), iround(y), s, COL_PAPER);
    text_at(iround((float)GAME_W / 2.0f - w / 2.0f) + 1, iround(y) + 1, s, c);
}

static void paper(void)
{
    int y;

    fb_rect(0, 0, GAME_W, GAME_H, COL_PAPER);
    for(y = 16; y < GAME_H; y += 16)
        fb_line_i(0, y, GAME_W - 1, y, COL_RULE);
    fb_line_i(26, 0, 26, GAME_H - 1, COL_MARGIN);
    fb_line_i(29, 0, 29, GAME_H - 1, COL_MARGIN);
}

static void draw_ship(float cx, float base, float dir, float slope,
                      uint16_t hull_c, uint16_t flag_c, float barrel,
                      int damaged, int smoke)
{
    int hull[12], bridge[8], turret[8], funnel[8];
    int *p;
    int nv;

    hull[0] = -27; hull[1] = -11; hull[2] = 22;  hull[3] = -11;
    hull[4] = 31;  hull[5] = -5;  hull[6] = 29;  hull[7] = 2;
    hull[8] = -22; hull[9] = 2;   hull[10] = -27; hull[11] = -4;

    p = tf(hull, 12, cx, base, dir, slope);
    nv = g_scr_n;
    shade(p, nv, base - 9.0f, base + 1.0f, 4, COL_LEAD2, 1);
    polyline(p, nv, hull_c, 0.7f, 1);
    if(damaged) {
        stroke(cx - 6.0f * dir, base - 8.0f, cx + 2.0f * dir, base - 1.0f, COL_RED, 0.0f);
        stroke(cx + 2.0f * dir, base - 1.0f, cx - 2.0f * dir, base - 6.0f, COL_RED, 0.0f);
    }
    stroke(cx - 27.0f * dir, base - 11.0f + (-27.0f * slope),
           cx + 22.0f * dir, base - 11.0f + (22.0f * slope), COL_INK, 0.6f);

    bridge[0] = -15; bridge[1] = -11; bridge[2] = -1; bridge[3] = -11;
    bridge[4] = -1;  bridge[5] = -20; bridge[6] = -15; bridge[7] = -20;
    p = tf(bridge, 8, cx, base, dir, slope);
    nv = g_scr_n;
    shade(p, nv, base - 19.0f, base - 12.0f, 3, COL_LEAD2, -1);
    polyline(p, nv, COL_INK, 0.6f, 1);

    turret[0] = -4; turret[1] = -11; turret[2] = 7; turret[3] = -11;
    turret[4] = 7;  turret[5] = -17; turret[6] = -4; turret[7] = -17;
    p = tf(turret, 8, cx, base, dir, slope);
    nv = g_scr_n;
    shade(p, nv, base - 16.0f, base - 12.0f, 3, COL_LEAD2, 1);
    polyline(p, nv, COL_INK, 0.5f, 1);

    {
        float bx = cx + 5.0f * dir;
        float by = base - 14.0f + (5.0f * slope);

        stroke(bx, by, bx + 16.0f * dir, by - barrel * 16.0f, COL_INK, 0.5f);
    }

    funnel[0] = -21; funnel[1] = -11; funnel[2] = -16; funnel[3] = -11;
    funnel[4] = -16; funnel[5] = -19; funnel[6] = -21; funnel[7] = -19;
    p = tf(funnel, 8, cx, base, dir, slope);
    polyline(p, g_scr_n, COL_INK, 0.5f, 1);

    if(smoke) {
        float sx = cx - 18.0f * dir;
        stroke(sx, base - 20.0f, sx - 3.0f * dir, base - 25.0f, COL_LEAD2, 1.2f);
        stroke(sx - 3.0f * dir, base - 25.0f, sx + 1.0f * dir, base - 30.0f,
               COL_LEAD2, 1.2f);
    }
    stroke(cx - 24.0f * dir, base - 11.0f, cx - 26.0f * dir, base - 27.0f,
           COL_INK, 0.4f);
    {
        float fy = base - 27.0f;
        stroke(cx - 26.0f * dir, fy, cx - 14.0f * dir, fy + 3.0f, flag_c, 0.6f);
        stroke(cx - 14.0f * dir, fy + 3.0f, cx - 26.0f * dir, fy + 6.0f, flag_c, 0.6f);
    }
    afill(cx - 9.0f * dir - 1.0f, base - 8.0f, 3.0f, 3.0f, COL_INK);
    afill(cx - 3.0f * dir - 1.0f, base - 8.0f, 3.0f, 3.0f, COL_INK);
    stroke(cx + 30.0f * dir, base, cx + 34.0f * dir, base - 3.0f, COL_WHITE, 0.0f);
    stroke(cx + 30.0f * dir, base + 1.0f, cx + 36.0f * dir, base - 1.0f,
           COL_WATER2, 0.0f);
}

static void draw_fish(float cx, float cy, float dir, uint16_t c)
{
    int pts[10];
    int *p;

    pts[0] = 0; pts[1] = -4; pts[2] = 6; pts[3] = -6; pts[4] = 12;
    pts[5] = 0; pts[6] = 6;  pts[7] = 6; pts[8] = 0;  pts[9] = 4;
    p = tf(pts, 10, cx, cy, dir, 0.0f);
    polyline(p, g_scr_n, c, 0.5f, 1);
    stroke(cx, cy - 2.0f, cx - 7.0f * dir, cy - 7.0f, c, 0.6f);
    stroke(cx, cy + 2.0f, cx - 7.0f * dir, cy + 7.0f, c, 0.6f);
    stroke(cx - 7.0f * dir, cy - 7.0f, cx - 7.0f * dir, cy + 7.0f, c, 0.6f);
    afill(cx + 6.0f * dir, cy - 3.0f, 2.0f, 2.0f, COL_INK);
}

static void draw_tentacle(float x, float base_y, float h, float phase, float w,
                          uint16_t c)
{
    int segs = 6, i;
    float py = 0.0f, px0 = 0.0f, px1 = 0.0f;

    for(i = 0; i <= segs; i++) {
        float t = (float)i / (float)segs;
        float sway = qsin(phase + t * 2.6f) * (5.0f + 10.0f * t);
        float y = base_y - h * t;
        float x0 = x + sway - w * (1.0f - t) * 0.5f;
        float x1 = x + sway + w * (1.0f - t) * 0.5f;

        if(i > 0) {
            stroke(px0, py, x0, y, c, 0.4f);
            stroke(px1, py, x1, y, c, 0.4f);
        }
        px0 = x0;
        px1 = x1;
        py = y;
    }
    {
        float tipx = x + qsin(phase + 2.6f) * 15.0f;
        float tipy = base_y - h;

        gon(tipx, tipy, 3.0f, 6, c, 0.3f);
        afill(tipx - 1.0f, tipy - 1.0f, 2.0f, 2.0f, COL_RED2);
        afill(x + qsin(phase + 1.3f) * 7.0f - 1.0f, base_y - h * 0.5f, 2.0f, 2.0f,
              COL_LEAD2);
    }
}

static void draw_rock(float cx, float base, float w, float h, uint16_t c)
{
    float hw = w / 2.0f;
    int pts[12];
    int *p;
    int nv;

    pts[0] = (int)(-hw);         pts[1] = 0;
    pts[2] = (int)(-hw * 0.7f);  pts[3] = (int)(-h * 0.55f);
    pts[4] = (int)(-hw * 0.2f);  pts[5] = (int)(-h * 0.8f);
    pts[6] = (int)(hw * 0.25f);  pts[7] = (int)(-h);
    pts[8] = (int)(hw * 0.8f);   pts[9] = (int)(-h * 0.5f);
    pts[10] = (int)hw;           pts[11] = 0;

    p = tf(pts, 12, cx, base, 1.0f, 0.0f);
    nv = g_scr_n;
    shade(p, nv, base - h + 1.0f, base - 1.0f, 4, COL_LEAD2, 1);
    polyline(p, nv, c, 0.9f, 1);
    stroke(cx - hw, base, cx + hw, base, COL_WATER3, 0.8f);
    stroke(cx - hw * 0.5f, base + 2.0f, cx + hw * 0.5f, base + 2.0f,
           COL_WATER2, 0.6f);
}

static void cloud(float cx, float cy, float s, uint16_t c, int dark)
{
    int n = 4, i;
    float px = 0.0f, py = 0.0f;

    for(i = 0; i <= n; i++) {
        float x = cx - s + (2.0f * s) * (float)i / (float)n;
        float y = cy - qsin((float)i / (float)n * 3.14159f) * s * 0.6f;

        if(i > 0) stroke(px, py, x, y, c, 1.0f);
        px = x;
        py = y;
    }
    stroke(cx - s, cy, cx + s, cy, c, 1.2f);
    if(dark) hatch(cx - s + 2.0f, cy, 2.0f * s - 4.0f, 6.0f, COL_LEAD2, 3, 1);
}

static void rain(int count, float t, float wind)
{
    int i;

    for(i = 0; i < count; i++) {
        int x = (i * 53 + (int)(t * 40.0f) * 17) % GAME_W;
        int y = (i * 97 + (int)(t * 120.0f) * 29) % 150 + 26;
        float dx = 2.0f + wind * 3.0f;

        stroke((float)x, (float)y, (float)x - dx, (float)y + 7.0f, COL_WATER3, 0.0f);
    }
}

static void bolt(float x, float y, float h, uint16_t c)
{
    float px = x, py = y;
    int i;

    for(i = 0; i < 4; i++) {
        float nx = x + (((i % 2) == 0) ? 6.0f : -6.0f);
        float ny = y + h * (float)(i + 1) / 4.0f;

        stroke(px, py, nx, ny, c, 0.5f);
        px = nx;
        py = ny;
    }
}

static void blob(float cx, float cy, float r, uint16_t c)
{
    gon(cx, cy, r, 7, c, 0.4f);
    afill(cx - r * 0.4f, cy - r * 0.4f, r, r, COL_PEN2);
    o_dot(cx + r * 0.5f, cy + r * 0.6f, 2.0f, c);
}

static void ball(float x, float y, uint16_t c, int big)
{
    if(big) {
        o_dot(x, y, 3.0f, c);
        afill(x - 1.0f, y - 1.0f, 2.0f, 2.0f, COL_INK);
    } else {
        afill(x - 1.0f, y - 1.0f, 2.0f, 2.0f, c);
        afill(x, y, 1.0f, 1.0f, COL_INK);
    }
}

/* ---- Ocean --------------------------------------------------------------- */

#define OC_K1 0.022f
#define OC_K2 0.051f
#define OC_W1 1.05f
#define OC_W2 1.62f

static float ocean_height(float wx, float t, float amp, float wind, float base_y)
{
    float h = 0.64f * qsin(wx * OC_K1 + t * OC_W1) +
              0.36f * qsin(wx * OC_K2 - t * OC_W2 + wind * 1.7f);
    return base_y + h * amp;
}

static float ocean_slope(float wx, float t, float amp, float wind)
{
    return amp * (0.64f * OC_K1 * qcos(wx * OC_K1 + t * OC_W1) +
                  0.36f * OC_K2 * qcos(wx * OC_K2 - t * OC_W2 + wind * 1.7f));
}

static void ocean_splash(float x, float y, float r, uint16_t c)
{
    stroke(x, y, x - r, y - r * 0.7f, c, 0.6f);
    stroke(x, y, x - r * 0.6f, y - r * 1.1f, c, 0.6f);
    stroke(x, y, x + r * 0.8f, y - r * 0.6f, c, 0.6f);
}

static void ocean_draw(float cam, float t, float amp, float wind, float base_y,
                       float bottom)
{
    float prev_x = 0.0f, prev_y = 0.0f, prev2 = 0.0f, prev3 = 0.0f;
    int has = 0, i;
    float x, y2;
    float px = 0.0f, py = 0.0f;
    int has2 = 0;

    for(x = 0.0f; x <= (float)GAME_W; x += 6.0f) {
        float wx = cam + x;
        float y = ocean_height(wx, t, amp, wind, base_y);

        if(has) {
            stroke(prev_x, prev_y, x, y, COL_PEN2, 0.9f);
            stroke(prev2, prev3, x, y + 4.0f, COL_WATER2, 0.6f);
        }
        prev_x = x; prev_y = y;
        prev2 = x;  prev3 = y + 4.0f;
        has = 1;
    }
    for(i = 2; i <= GAME_W - 4; i += 18) {
        float y = ocean_height(cam + (float)i, t, amp, wind, base_y);
        stroke((float)i, y + 3.0f, (float)i + 5.0f, y + 11.0f, COL_WATER3, 0.0f);
    }
    for(x = 0.0f; x <= (float)GAME_W; x += 12.0f) {
        float y = ocean_height(cam + x + 40.0f, t * 0.8f, amp * 0.7f, wind,
                               base_y + 14.0f);
        if(has2) stroke(px, py, x, y, COL_WATER, 0.6f);
        px = x;
        py = y;
        has2 = 1;
    }
    y2 = base_y + amp + 16.0f;
    while(y2 < bottom) {
        stroke(0.0f, y2, (float)GAME_W - 1.0f, y2, COL_WATER, 0.0f);
        y2 += 22.0f;
    }
}

/* ---- save rules (RAM only) ----------------------------------------------- */

/* Save.pens: colour triples become RGB565; need_total / need_grade. */
static int g_cfg_sea = 2;
static int g_cfg_hi;
static int g_cfg_total;
static int g_cfg_grade;
static int g_cfg_pen = 1;

static int pen_unlocked(int idx)
{
    static const int need_total[4] = { 0, 1500, 0, 5000 };
    static const int need_grade[4] = { 0, 0, 3, 0 };

    if(idx < 1 || idx > 4) return 0;
    if(g_cfg_total < need_total[idx - 1]) return 0;
    return g_cfg_grade >= need_grade[idx - 1];
}

static uint16_t active_pen_colour(void)
{
    static const uint16_t pens[4] = { COL_PEN, COL_LEAD, COL_RED, 0x7A15u };

    int idx = g_cfg_pen;
    if(!pen_unlocked(idx)) idx = 1;
    return pens[idx - 1];
}

static int grade_for(int score)
{
    if(score >= 1500) return 3;
    if(score >= 700) return 2;
    return 1;
}

/* ---- game state ---------------------------------------------------------- */

typedef struct {
    int kind;
    float x, hp, max_hp, t, fire, dir, sy;
    int live;
} foe_t;

typedef struct {
    float x, y, vx, vy, life, rail;
    int live;
} shot_t;

typedef struct {
    float x, y, vx, vy, life, r;
    int bomb, live;
} ball_t;

typedef struct {
    float x, y, bob;
    int kind, live;
} drop_t;

typedef struct {
    int kind;
    float x, y, life, r;
    int live;
} fx_t;

typedef struct {
    float x, y, life;
    char t[12];
    uint16_t c;
    int live;
} pop_t;

#define CAP_FOES 12
#define CAP_SHOTS 16
#define CAP_BALLS 24
#define CAP_DROPS 12
#define CAP_FX 16
#define CAP_POP 8

#define KIND_BOAT 0
#define KIND_FISH 1
#define KIND_ARM 2
#define KIND_ROCK 3
#define KIND_BLIMP 4
#define KIND_BOSS 5

static foe_t  g_foes[CAP_FOES];
static shot_t g_shots[CAP_SHOTS];
static ball_t g_balls[CAP_BALLS];
static drop_t g_drops[CAP_DROPS];
static fx_t   g_fx[CAP_FX];
static pop_t  g_pop[CAP_POP];

static float g_hp, g_max_hp, g_ink, g_angle, g_cool, g_inv;
static float g_world, g_throttle, g_score;
static int   g_wave;
static float g_next_wave;
static float g_strike, g_shake, g_hit, g_dip;
static float g_shield, g_shield_hp, g_rail, g_rail_y;
static float g_spawn_x, g_banner;
static int   g_new_record;
static float g_time;
static float g_amp = 7.5f, g_wind;
static float g_storm_flash;
static int   g_grade;
static uint32_t g_rng = 20240520u;
static char  g_mode[8] = "run";
static int   g_pause_sel = 1;
static int   g_ready;
static int   g_pen_c = COL_PEN;

static const char *g_mode_run = "run";
static const char *g_mode_pause = "pause";
static const char *g_mode_over = "over";

static int mode_is(const char *m)
{
    int i = 0;

    while(g_mode[i] && m[i] && g_mode[i] == m[i]) i++;
    return g_mode[i] == '\0' && m[i] == '\0';
}

static void mode_set(const char *m)
{
    int i = 0;

    while(m[i] && i < 7) { g_mode[i] = m[i]; i++; }
    g_mode[i] = '\0';
}

static int irnd(int n)
{
    g_rng = (uint32_t)(g_rng * 1103515245u + 12345u) % 2147483648u;
    return (int)(((g_rng / 65536u) % 32768u) % (uint32_t)n) + 1;
}

static int kind_hp(int kind)
{
    switch(kind) {
    case KIND_BOAT: return 2;
    case KIND_FISH: return 1;
    case KIND_ARM:  return 2;
    case KIND_ROCK: return 0;
    case KIND_BLIMP: return 2;
    default: return 26;
    }
}

static int kind_spd(int kind)
{
    switch(kind) {
    case KIND_BOAT: return 8;
    case KIND_FISH: return 30;
    case KIND_ROCK: return 0;
    case KIND_ARM:  return 0;
    case KIND_BLIMP: return 12;
    default: return 0;
    }
}

static int kind_sc(int kind)
{
    switch(kind) {
    case KIND_BOAT: return 100;
    case KIND_FISH: return 40;
    case KIND_ARM:  return 80;
    case KIND_ROCK: return 0;
    case KIND_BLIMP: return 120;
    default: return 700;
    }
}

static float kind_w(int kind)
{
    switch(kind) {
    case KIND_BOAT: return 26.0f;
    case KIND_FISH: return 12.0f;
    case KIND_ARM:  return 14.0f;
    case KIND_ROCK: return 22.0f;
    case KIND_BLIMP: return 28.0f;
    default: return 54.0f;
    }
}

static float kind_h(int kind)
{
    switch(kind) {
    case KIND_BOAT: return 12.0f;
    case KIND_FISH: return 10.0f;
    case KIND_ARM:  return 42.0f;
    case KIND_ROCK: return 30.0f;
    case KIND_BLIMP: return 20.0f;
    default: return 56.0f;
    }
}

static int box(float ax, float ay, float aw, float ah,
               float bx, float by, float bw, float bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

/* ---- score formatting (qenlink links no division helper) ----------------- */

static char *append_uint(char *out, int value)
{
    unsigned v = value < 0 ? 0u : (unsigned)value;
    int pos, started = 0;

    if(v == 0u) { *out++ = '0'; return out; }
    if(v > 999999u) v = 999999u;
    for(pos = 6; pos >= 0; pos--) {
        unsigned scale = 1u;
        int digit = 0, k;

        for(k = 0; k < pos; k++) scale *= 10u;
        while(v >= scale && digit < 9) { v -= scale; digit++; }
        if(digit > 0 || started) { *out++ = (char)('0' + digit); started = 1; }
    }
    return out;
}

static char g_num[16];
static char g_line[48];

static const char *num(int v)
{
    char *p = append_uint(g_num, v);
    *p = '\0';
    return g_num;
}

/* ---- entities ------------------------------------------------------------ */

static void clear_all(void)
{
    int i;

    for(i = 0; i < CAP_FOES; i++) g_foes[i].live = 0;
    for(i = 0; i < CAP_SHOTS; i++) g_shots[i].live = 0;
    for(i = 0; i < CAP_BALLS; i++) g_balls[i].live = 0;
    for(i = 0; i < CAP_DROPS; i++) g_drops[i].live = 0;
    for(i = 0; i < CAP_FX; i++) g_fx[i].live = 0;
    for(i = 0; i < CAP_POP; i++) g_pop[i].live = 0;
}

static void fresh(void)
{
    mode_set(g_mode_run);
    g_hp = 3.0f;
    g_max_hp = 3.0f;
    g_ink = 55.0f;
    g_angle = 0.32f;
    g_cool = 0.0f;
    g_inv = 0.0f;
    g_world = 0.0f;
    g_throttle = 1.0f;
    g_score = 0.0f;
    g_wave = 1;
    g_next_wave = 620.0f;
    g_strike = 0.0f;
    g_shake = 0.0f;
    g_hit = 0.0f;
    g_dip = 0.0f;
    g_shield = 0.0f;
    g_shield_hp = 0.0f;
    g_rail = 0.0f;
    g_rail_y = 0.0f;
    clear_all();
    g_spawn_x = 320.0f;
    g_banner = 1.6f;
    g_pause_sel = 1;
    g_new_record = 0;
    g_storm_flash = 0.0f;
    g_rng = 20240520u;
    g_time = 0.0f;
    g_amp = 7.5f;
    g_wind = 0.0f;
    jit_reset(2024);
}

static void add_fx(int kind, float x, float y, float r)
{
    int i;

    for(i = 0; i < CAP_FX; i++) {
        if(!g_fx[i].live) {
            g_fx[i].live = 1;
            g_fx[i].kind = kind;
            g_fx[i].x = x;
            g_fx[i].y = y;
            g_fx[i].life = 0.5f;
            g_fx[i].r = r;
            return;
        }
    }
}

static void pop_copy(pop_t *p, const char *t)
{
    int i = 0;

    while(t[i] && i < 11) { p->t[i] = t[i]; i++; }
    p->t[i] = '\0';
}

static void add_pop(float x, float y, const char *txt, uint16_t c)
{
    int i;

    for(i = 0; i < CAP_POP; i++) {
        if(!g_pop[i].live) {
            g_pop[i].live = 1;
            g_pop[i].x = x;
            g_pop[i].y = y;
            pop_copy(&g_pop[i], txt);
            g_pop[i].life = 0.8f;
            g_pop[i].c = c;
            return;
        }
    }
}

static void hurt(int n)
{
    if(g_inv > 0.0f) return;
    g_hp -= (float)n;
    g_inv = 1.6f;
    g_shake = 0.4f;
    g_hit = 0.4f;
    if(g_hp <= 0.0f) {
        g_hp = 0.0f;
        mode_set(g_mode_over);
        if(g_score > (float)g_cfg_hi) {
            g_cfg_hi = (int)g_score;
            g_new_record = 1;
        }
        g_grade = grade_for((int)g_score);
        /* Save.finish_battle: totals, best grade, unlocked pens. */
        {
            int before[4], i;

            for(i = 0; i < 4; i++) before[i] = pen_unlocked(i + 1);
            g_cfg_total += (int)g_score;
            if(g_grade > g_cfg_grade) g_cfg_grade = g_grade;
            for(i = 0; i < 4; i++)
                if(pen_unlocked(i + 1) && !before[i]) g_cfg_pen = i + 1;
        }
    }
}

static foe_t *spawn(int kind, float wx)
{
    int i;

    for(i = 0; i < CAP_FOES; i++) {
        if(!g_foes[i].live) {
            foe_t *e = &g_foes[i];

            e->live = 1;
            e->kind = kind;
            e->x = wx;
            e->hp = (float)kind_hp(kind);
            e->max_hp = e->hp;
            e->t = 0.0f;
            e->fire = 1.0f + (float)irnd(20) / 10.0f;
            e->dir = (kind == KIND_BLIMP) ? 1.0f : -1.0f;
            e->sy = 0.0f;
            if(kind == KIND_BOSS) e->fire = 2.0f;
            return e;
        }
    }
    return NULL;
}

static int pick_kind(int wave)
{
    int r = irnd(100);

    if(wave <= 1) {
        if(r <= 45) return KIND_BOAT;
        if(r <= 78) return KIND_FISH;
        return KIND_ROCK;
    }
    if(wave == 2) {
        if(r <= 34) return KIND_BOAT;
        if(r <= 58) return KIND_FISH;
        if(r <= 76) return KIND_ROCK;
        return KIND_ARM;
    }
    if(r <= 30) return KIND_BOAT;
    if(r <= 48) return KIND_FISH;
    if(r <= 66) return KIND_ROCK;
    if(r <= 84) return KIND_ARM;
    return KIND_BLIMP;
}

static void aim_arc(float dx, float dy, float sp, float *out)
{
    float d = qabsf(dx);
    float t, vy;

    if(d < 4.0f) d = 4.0f;
    t = d / sp;
    vy = (dy - 0.5f * GFORCE * t * t) / t;
    out[0] = (dx >= 0.0f) ? sp : -sp;
    out[1] = vy;
}

static void fire(void)
{
    float base, bx, by;
    int i;

    if(g_cool > 0.0f || g_ink < INK_COST) {
        if(g_ink < INK_COST) add_pop(SHIP_X + 14.0f, SEA_BASE - 60.0f, "HET MUC", COL_RED);
        return;
    }
    base = ocean_height(g_world, g_time, g_amp, g_wind, SEA_BASE);
    bx = g_world + 6.0f;
    by = base - 14.0f;
    for(i = 0; i < CAP_SHOTS; i++) {
        if(!g_shots[i].live) {
            shot_t *sh = &g_shots[i];

            sh->live = 1;
            if(g_rail > 0.0f) {
                sh->x = bx;
                sh->y = g_rail_y;
                sh->vx = SHOT_V * 1.15f;
                sh->vy = 0.0f;
                sh->life = 3.0f;
                sh->rail = g_rail_y;
            } else {
                sh->x = bx;
                sh->y = by;
                sh->vx = qcos(g_angle) * SHOT_V;
                sh->vy = -qsin(g_angle) * SHOT_V;
                sh->life = 2.6f;
                sh->rail = 0.0f;
            }
            break;
        }
    }
    g_ink -= INK_COST;
    g_cool = 0.22f;
    g_dip = 0.12f;
}

static void do_shield(void)
{
    if(g_ink < SHIELD_COST) {
        add_pop(SHIP_X + 10.0f, SEA_BASE - 60.0f, "HET MUC", COL_RED);
        return;
    }
    g_ink -= SHIELD_COST;
    g_shield = SHIELD_TIME;
    g_shield_hp = 3.0f;
    add_pop(SHIP_X, SEA_BASE - 46.0f, "LA CHAN", COL_PEN);
}

static void do_rail(void)
{
    float base;

    if(g_ink < RAIL_COST) {
        add_pop(SHIP_X + 10.0f, SEA_BASE - 60.0f, "HET MUC", COL_RED);
        return;
    }
    base = ocean_height(g_world, g_time, g_amp, g_wind, SEA_BASE);
    g_ink -= RAIL_COST;
    g_rail = RAIL_TIME;
    g_rail_y = clampf(base - 34.0f - g_angle * 95.0f, 46.0f, SEA_BASE - 16.0f);
    add_pop(SHIP_X, g_rail_y - 12.0f, "DUONG DAN", COL_PEN);
}

static void do_strike(void)
{
    int i;

    if(g_ink < INK_MAX || g_strike > 0.0f) return;
    g_ink = 0.0f;
    g_strike = 1.1f;
    g_shake = 0.6f;
    for(i = 0; i < CAP_FOES; i++) {
        if(g_foes[i].live && g_foes[i].kind != KIND_ROCK)
            g_foes[i].hp -= (g_foes[i].kind == KIND_BOSS) ? 6.0f : 3.0f;
    }
    add_pop((float)GAME_W / 2.0f - 40.0f, 150.0f, "GACH XOA!", COL_RED);
}

static void spawn_ball(float x, float y, float vx, float vy, float life,
                       float r, int bomb)
{
    int i;

    for(i = 0; i < CAP_BALLS; i++) {
        if(!g_balls[i].live) {
            ball_t *b = &g_balls[i];

            b->live = 1;
            b->x = x; b->y = y; b->vx = vx; b->vy = vy;
            b->life = life; b->r = r; b->bomb = bomb;
            return;
        }
    }
}

static void spawn_drop(float x, float bob, int kind)
{
    int i;

    for(i = 0; i < CAP_DROPS; i++) {
        if(!g_drops[i].live) {
            drop_t *d = &g_drops[i];

            d->live = 1;
            d->x = x;
            d->y = SEA_BASE;
            d->bob = bob;
            d->kind = kind;
            return;
        }
    }
}

/* ---- update -------------------------------------------------------------- */

static void update_player(float dt)
{
    g_cool -= dt;
    if(g_inv > 0.0f) g_inv -= dt;
    if(g_shake > 0.0f) g_shake -= dt;
    if(g_hit > 0.0f) g_hit -= dt;
    if(g_dip > 0.0f) g_dip -= dt;
    if(g_strike > 0.0f) g_strike -= dt;
    if(g_shield > 0.0f) {
        g_shield -= dt;
        if(g_shield <= 0.0f) g_shield_hp = 0.0f;
    }
    if(g_rail > 0.0f) g_rail -= dt;
    g_ink += INK_REGEN * dt;
    if(g_ink > INK_MAX) g_ink = INK_MAX;
    g_world += BASE_SPEED * g_throttle * dt;
}

static void update_foes(float dt, float cam)
{
    float pb = ocean_height(g_world, g_time, g_amp, g_wind, SEA_BASE);
    int i;

    for(i = 0; i < CAP_FOES; i++) {
        foe_t *e;
        int k, dead = 0;
        float ex, ey, w, h;
        int touching = 0;

        if(!g_foes[i].live) continue;
        e = &g_foes[i];
        k = e->kind;
        e->t += dt;

        if(k == KIND_BOAT || k == KIND_FISH || k == KIND_BLIMP) {
            float sp = (float)kind_spd(k) + (float)g_wave * 1.2f;
            e->x -= sp * dt;
        } else if(k == KIND_BOSS) {
            e->x = cam + 152.0f;
        }

        if(mode_is(g_mode_run)) {
            float sx;
            e->fire -= dt;
            sx = e->x - cam;
            if(e->fire <= 0.0f && sx > -30.0f && sx < (float)GAME_W + 30.0f) {
                if(k == KIND_BOAT) {
                    float eb, off;
                    float aim[2];

                    e->fire = 3.4f - (float)g_wave * 0.15f;
                    if(e->fire < 2.0f) e->fire = 2.0f;
                    eb = ocean_height(e->x, g_time, g_amp, g_wind, SEA_BASE);
                    off = (float)(irnd(70) - 35);
                    aim_arc(g_world - e->x + off, (pb - 12.0f) - (eb - 14.0f), 120.0f, aim);
                    spawn_ball(e->x - 8.0f, eb - 14.0f, aim[0], aim[1], 3.0f, 2.0f, 0);
                } else if(k == KIND_BLIMP) {
                    e->fire = 2.4f;
                    spawn_ball(e->x, 84.0f, -10.0f, 10.0f, 3.4f, 3.0f, 1);
                } else if(k == KIND_BOSS) {
                    float aim[2];
                    e->fire = 2.6f;
                    aim_arc(g_world - e->x, (pb - 10.0f) - 150.0f, 96.0f, aim);
                    spawn_ball(e->x, 150.0f, aim[0], aim[1], 3.2f, 4.0f, 0);
                }
            }
        }

        ex = e->x - cam;
        if(k == KIND_BOAT) {
            ey = ocean_height(e->x, g_time, g_amp, g_wind, SEA_BASE) - 14.0f;
        } else if(k == KIND_FISH) {
            float s = qsin(e->t * 1.5f + e->x * 0.02f);
            ey = SEA_BASE + 8.0f - (s > 0.0f ? s : 0.0f) * 30.0f;
        } else if(k == KIND_ROCK) {
            ey = ocean_height(e->x, g_time, g_amp, g_wind, SEA_BASE) - 16.0f;
        } else if(k == KIND_ARM) {
            ey = SEA_BASE - 10.0f - 20.0f * (0.5f + 0.5f * qsin(e->t * 1.2f + e->x * 0.01f));
        } else if(k == KIND_BLIMP) {
            ey = 70.0f;
        } else {
            ey = 132.0f;
        }
        e->sy = ey;
        w = kind_w(k);
        h = kind_h(k);

        if(k != KIND_BLIMP && k != KIND_BOSS && k != KIND_ARM) {
            touching = box(SHIP_X - 22.0f, pb - 12.0f, 46.0f, 18.0f,
                           ex - w / 2.0f, ey - h / 2.0f, w, h);
            if(touching && k == KIND_ROCK) g_world -= 6.0f;
        } else if(k == KIND_ARM || k == KIND_BOSS) {
            touching = box(SHIP_X - 20.0f, pb - 14.0f, 44.0f, 20.0f,
                           ex - w / 2.0f, ey - h / 2.0f, w, h);
        }
        if(touching) {
            if(g_shield > 0.0f && k != KIND_ROCK) {
                e->hp -= 1.0f;
                e->x += 26.0f;
                g_shield_hp -= 1.0f;
                add_fx(2, SHIP_X + 30.0f, pb - 12.0f, 10.0f);
                add_pop(SHIP_X + 34.0f, pb - 30.0f, "-1", COL_PEN);
                if(g_shield_hp <= 0.0f) g_shield = 0.0f;
            } else {
                hurt(1);
                if(k == KIND_ROCK) add_fx(0, ex, ey + 10.0f, 16.0f);
            }
        }

        if(e->hp <= 0.0f) dead = 1;
        if(e->x - cam < -70.0f) dead = 1;
        if(dead) {
            if(k != KIND_ROCK && e->hp <= 0.0f) {
                int sc = kind_sc(k);

                g_score += (float)sc;
                if(g_score > (float)g_cfg_hi) g_cfg_hi = (int)g_score;
                {
                    char buf[12];
                    char *p = buf;

                    *p++ = '+';
                    p = append_uint(p, sc);
                    *p = '\0';
                    add_pop(ex, ey - 20.0f, buf, COL_PEN);
                }
                add_fx(1, ex, ey, k == KIND_BOSS ? 34.0f : 16.0f);
                if(k == KIND_BOSS) {
                    g_wave++;
                    g_banner = 1.6f;
                    spawn_drop(e->x - 20.0f, 0.0f, 0);
                    spawn_drop(e->x + 6.0f, 1.4f, 0);
                    spawn_drop(e->x - 6.0f, 2.2f, 1);
                } else {
                    int r = irnd(100);
                    if(r <= 50) spawn_drop(e->x, (float)irnd(10) / 3.0f, 0);
                    else if(r <= 62) spawn_drop(e->x, (float)irnd(10) / 3.0f, 1);
                }
            }
            e->live = 0;
        }
    }
}

static void update_shots(float dt, float cam)
{
    int i;

    for(i = 0; i < CAP_SHOTS; i++) {
        shot_t *sh;
        float sx, sy, wy;
        int gone, j;

        if(!g_shots[i].live) continue;
        sh = &g_shots[i];
        if(sh->rail != 0.0f) sh->y = sh->rail;
        else {
            sh->vy += GFORCE * dt;
            sh->vx += g_wind * 16.0f * dt;
        }
        sh->x += sh->vx * dt;
        sh->y += sh->vy * dt;
        sh->life -= dt;
        sx = sh->x - cam;
        sy = sh->y;
        gone = sh->life <= 0.0f;

        if(!gone && sh->rail == 0.0f) {
            wy = ocean_height(sh->x, g_time, g_amp, g_wind, SEA_BASE);
            if(sy > wy) {
                add_fx(0, sx, wy, 9.0f);
                for(j = 0; j < CAP_FOES; j++) {
                    if(g_foes[j].live && g_foes[j].kind != KIND_ROCK &&
                       g_foes[j].kind != KIND_BLIMP) {
                        float ex = g_foes[j].x - cam;
                        if(qabsf(ex - sx) < 20.0f) {
                            g_foes[j].hp -= 1.0f;
                            add_fx(0, ex, wy, 12.0f);
                            add_pop(ex, g_foes[j].sy - 14.0f, "-1", COL_RED);
                        }
                    }
                }
                gone = 1;
            }
        }
        if(sx < -20.0f || sx > (float)GAME_W + 20.0f) gone = 1;

        if(!gone) {
            for(j = 0; j < CAP_FOES; j++) {
                foe_t *e;
                int k;
                float ex, w, h;

                if(!g_foes[j].live) continue;
                e = &g_foes[j];
                k = e->kind;
                ex = e->x - cam;
                w = kind_w(k);
                h = kind_h(k);
                if(k != KIND_ROCK && box(sx - 3.0f, sy - 3.0f, 6.0f, 6.0f,
                                         ex - w / 2.0f, e->sy - h * 0.8f, w, h * 1.6f)) {
                    e->hp -= 1.0f;
                    add_fx(2, sx, sy, 6.0f);
                    add_pop(ex, e->sy - 14.0f, "-1", COL_RED);
                    gone = 1;
                    break;
                }
            }
        }
        if(gone) sh->live = 0;
    }

    for(i = 0; i < CAP_BALLS; i++) {
        ball_t *b;
        float bx, by, wy;
        int gone;

        if(!g_balls[i].live) continue;
        b = &g_balls[i];
        b->vy += GFORCE * dt;
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        b->life -= dt;
        bx = b->x - cam;
        by = b->y;
        gone = b->life <= 0.0f;
        if(!gone) {
            wy = ocean_height(b->x, g_time, g_amp, g_wind, SEA_BASE);
            if(by > wy) {
                add_fx(0, bx, wy, 7.0f);
                gone = 1;
            }
        }
        if(!gone) {
            float pb = ocean_height(g_world, g_time, g_amp, g_wind, SEA_BASE);

            if(g_shield > 0.0f &&
               box(SHIP_X + 14.0f, pb - 34.0f, 34.0f, 42.0f,
                   bx - 2.0f, by - 2.0f, b->r + 3.0f, b->r + 3.0f)) {
                g_shield_hp -= 1.0f;
                if(g_shield_hp <= 0.0f) g_shield = 0.0f;
                add_fx(2, bx, by, 9.0f);
                gone = 1;
            } else if(box(SHIP_X - 20.0f, pb - 12.0f, 42.0f, 18.0f,
                          bx - 2.0f, by - 2.0f, b->r + 2.0f, b->r + 2.0f)) {
                hurt(1);
                add_fx(2, bx, by, 8.0f);
                gone = 1;
            }
        }
        if(bx < -24.0f || bx > (float)GAME_W + 24.0f) gone = 1;
        if(gone) b->live = 0;
    }

    for(i = 0; i < CAP_DROPS; i++) {
        drop_t *d;
        float dx, dy, pb;

        if(!g_drops[i].live) continue;
        d = &g_drops[i];
        d->bob += dt;
        dy = ocean_height(d->x, g_time, g_amp, g_wind, SEA_BASE) - 5.0f +
             qsin(d->bob * 3.0f) * 2.0f;
        dx = d->x - cam;
        pb = ocean_height(g_world, g_time, g_amp, g_wind, SEA_BASE);
        if(box(SHIP_X - 24.0f, pb - 16.0f, 50.0f, 24.0f,
               dx - 7.0f, dy - 7.0f, 14.0f, 14.0f)) {
            if(d->kind == 1) {
                g_hp += 1.0f;
                if(g_hp > g_max_hp) g_hp = g_max_hp;
                add_pop(dx, dy - 12.0f, "+TIM", COL_RED);
            } else {
                g_ink += 25.0f;
                if(g_ink > INK_MAX) g_ink = INK_MAX;
                add_pop(dx, dy - 12.0f, "+MUC", COL_PEN);
            }
            d->live = 0;
        } else if(dx < -30.0f) {
            d->live = 0;
        }
    }

    for(i = 0; i < CAP_FX; i++) {
        if(g_fx[i].live) {
            g_fx[i].life -= dt;
            if(g_fx[i].life <= 0.0f) g_fx[i].live = 0;
        }
    }
    for(i = 0; i < CAP_POP; i++) {
        if(g_pop[i].live) {
            g_pop[i].life -= dt;
            if(g_pop[i].life <= 0.0f) g_pop[i].live = 0;
        }
    }
}

static void spawn_wave(float cam)
{
    int live = 0, i;

    if(g_world > g_next_wave) {
        g_wave++;
        g_next_wave = g_world + 520.0f + (float)g_wave * 120.0f;
        g_banner = 1.6f;
        g_score += 40.0f * (float)g_wave;
        if(g_score > (float)g_cfg_hi) g_cfg_hi = (int)g_score;
        if(g_wave % 3 == 0) {
            spawn(KIND_BOSS, cam + 250.0f);
            g_banner = 2.0f;
        }
        return;
    }
    for(i = 0; i < CAP_FOES; i++) if(g_foes[i].live) live++;
    if(live < MAX_FOES && g_spawn_x < cam + (float)GAME_W + 80.0f) {
        int kind = pick_kind(g_wave);
        float gap;

        if(kind == KIND_BLIMP && live > 3) kind = KIND_FISH;
        spawn(kind, g_spawn_x);
        gap = 88.0f - (float)g_wave * 6.0f;
        if(gap < 42.0f) gap = 42.0f;
        g_spawn_x += gap + (float)irnd(24);
    }
}

static void update(float dt)
{
    float cam;

    if(mode_is(g_mode_pause) || mode_is(g_mode_over)) return;
    g_amp = g_cfg_sea == 1 ? 4.0f : (g_cfg_sea == 3 ? 13.0f : 7.5f);
    g_wind = qsin((g_time + 3.0f) * 0.13f) * 0.75f;
    cam = g_world - SHIP_X;

    if(g_banner > 0.0f) g_banner -= dt;
    if(g_storm_flash > 0.0f) g_storm_flash -= dt;

    update_player(dt);
    update_foes(dt, cam);
    update_shots(dt, cam);
    spawn_wave(cam);

    if(g_cfg_sea == 3 && irnd(1400) <= 5) g_storm_flash = 0.16f;
}

/* ---- drawing ------------------------------------------------------------- */

static void hud(void)
{
    int full = g_ink >= INK_MAX - 0.5f || g_strike > 0.0f;
    float ty = 258.0f;

    g_pen_c = active_pen_colour();
    afill(0.0f, 0.0f, (float)GAME_W, 27.0f, COL_PAPER);
    stroke(0.0f, 27.0f, (float)GAME_W - 1.0f, 27.0f, COL_RULE, 0.0f);
    set_font(8);
    hearts(10.0f, 11.0f, (int)g_hp, (int)g_max_hp, 0.75f);
    meter(62.0f, 5.0f, 84.0f, 12.0f, g_ink / INK_MAX, COL_PEN2, COL_INK, COL_PEN);
    /* Ui.arrow */
    {
        int dir = g_wind >= 0.0f ? 1 : -1;
        uint16_t c = g_wind >= 0.0f ? COL_PEN : COL_RED;
        stroke(150.0f, 16.0f, 150.0f + 5.0f * (float)dir, 20.0f, c, 0.4f);
        stroke(150.0f + 5.0f * (float)dir, 20.0f, 150.0f, 24.0f, c, 0.4f);
    }
    compass(216.0f, 13.0f, 10.0f, g_wind);
    pen_icon(190.0f, 34.0f, 1.0f, full ? COL_RED : COL_PEN);
    {
        char *p;
        int i = 0;

        p = g_line;
        {
            const char *label = "DIEM ";
            while(label[i]) { *p++ = label[i]; i++; }
        }
        p = append_uint(p, (int)g_score);
        *p = '\0';
        text_at(62, 18, g_line, COL_PEN);
    }
    if(full && mode_is(g_mode_run))
        ctext(GAME_W / 2, 244, "DAY MUC: BAM Q", COL_RED);
    set_font(8);
    {
        char *p = g_line;
        int i = 0;

        while("DOT "[i]) { *p++ = "DOT "[i]; i++; }
        p = append_uint(p, g_wave);
        *p = '\0';
        text_at(4, 300, g_line, COL_RED);
    }
    rtext(GAME_W - 6, 300, "3: KRAKEN", COL_LEAD2);

    if(g_shield > 0.0f) {
        char *p;
        int i = 0;

        p = g_line;
        while("CHAN CON "[i]) { *p++ = "CHAN CON "[i]; i++; }
        p = append_uint(p, (int)(g_shield + 0.9f));
        *p = '\0';
        text_at(6, (int)ty, g_line, COL_RED);
        ty += 11.0f;
    }
    if(g_rail > 0.0f) {
        char *p;
        int i = 0;

        p = g_line;
        while("DUONG DAN CON "[i]) { *p++ = "DUONG DAN CON "[i]; i++; }
        p = append_uint(p, (int)(g_rail + 0.9f));
        *p = '\0';
        text_at(6, (int)ty, g_line, COL_PEN);
    } else if(g_shield <= 0.0f && g_ink >= SHIELD_COST && mode_is(g_mode_run)) {
        ctext(GAME_W / 2, 262, "1: LA CHAN  3: DUONG DAN", COL_LEAD2);
    }
}

static void ui_panel(float x, float y, float w, float h, const char *title)
{
    afill(x, y, w, h, COL_PAPER);
    hatch(x, y, w, h, COL_SHADOW, 11, 1);
    outline(x, y, w, h, COL_INK, 1.0f);
    afill(x + 6.0f, y - 3.0f, 16.0f, 7.0f, COL_YELLOW);
    outline(x + 6.0f, y - 3.0f, 16.0f, 7.0f, COL_LEAD, 0.5f);
    if(title != NULL) {
        set_font(8);
        text_at((int)(x + 6.0f), (int)(y + 6.0f), title, COL_RED);
        stroke(x + 4.0f, y + 16.0f, x + w - 5.0f, y + 16.0f, COL_RULE, 0.0f);
    }
}

static void ui_item(float x, float y, float w, const char *label, int sel)
{
    uint16_t c = sel ? COL_PEN : COL_LEAD;

    if(sel) {
        afill(x, y - 3.0f, w, 15.0f, COL_SHADOW);
        {
            int i;
            for(i = 0; i < 6; i++) {
                float px = x + (float)i * (w / 6.0f) + 2.0f;
                stroke(px, y + 10.0f, px + 8.0f, y - 2.0f, COL_LEAD2, 0.0f);
            }
        }
        stroke(x, y + 12.0f, x + w, y + 13.0f, COL_RED, 0.9f);
        stroke(x + 3.0f, y, x + 8.0f, y + 4.0f, COL_RED, 0.4f);
        stroke(x + 8.0f, y + 4.0f, x + 3.0f, y + 8.0f, COL_RED, 0.4f);
    }
    text_at((int)(x + 14.0f), (int)y, label, c);
}

static void softkeys(const char *left, const char *right)
{
    int y0 = GAME_H - 22;

    afill(0.0f, (float)y0, (float)GAME_W, (float)(GAME_H - y0), COL_PAPER);
    stroke(0.0f, (float)y0, (float)GAME_W - 1.0f, (float)y0, COL_RULE, 0.0f);
    set_font(8);
    if(left != NULL) text_at(4, GAME_H - 14, left, COL_PEN);
    if(right != NULL) rtext(GAME_W - 6, GAME_H - 14, right, COL_PEN);
}

static void draw_fx(float sc)
{
    int i;

    (void)sc;
    for(i = 0; i < CAP_FX; i++) {
        fx_t *f;
        float x, y;

        if(!g_fx[i].live) continue;
        f = &g_fx[i];
        x = f->x;
        y = f->y;
        if(f->kind == 0) {
            ocean_splash(x, y, 10.0f, COL_WATER3);
            ocean_splash(x, y + 3.0f, 14.0f, COL_WATER2);
        } else if(f->kind == 1) {
            int a;
            gon(x, y, f->r, 8, COL_RED, 0.6f);
            gon(x, y, f->r * 0.6f, 7, COL_YELLOW, 0.5f);
            for(a = 0; a < 5; a++) {
                float ang = (float)a * 1.3f;
                stroke(x, y, x + qcos(ang) * f->r * 1.4f,
                       y + qsin(ang) * f->r * 1.4f, COL_INK, 0.6f);
            }
        } else {
            gon(x, y, f->life * 24.0f + 4.0f, 6, COL_LEAD, 0.4f);
        }
    }
}

static void draw_blimp(float sx, float sy)
{
    gon(sx, sy, 16.0f, 9, COL_LEAD, 0.3f);
    hatch(sx - 12.0f, sy - 10.0f, 24.0f, 20.0f, COL_LEAD2, 6, 1);
    outline(sx - 8.0f, sy + 10.0f, 16.0f, 8.0f, COL_INK, 0.5f);
    stroke(sx - 16.0f, sy - 4.0f, sx - 24.0f, sy - 6.0f, COL_LEAD, 0.4f);
    stroke(sx - 24.0f, sy - 6.0f, sx - 18.0f, sy + 4.0f, COL_LEAD, 0.4f);
    gon(sx - 4.0f, sy + 18.0f, 5.0f, 6, COL_INK, 0.0f);
    stroke(sx - 8.0f, sy + 22.0f, sx - 14.0f, sy + 28.0f, COL_RED, 0.5f);
}

static void draw_boss(float sx, float sy, float hp, float max_hp, float t)
{
    float r = 30.0f;

    draw_tentacle(sx - 22.0f, sy + 44.0f, 40.0f, t * 1.4f, 14.0f, COL_LEAD);
    draw_tentacle(sx + 24.0f, sy + 44.0f, 46.0f, t * 1.1f + 2.0f, 16.0f, COL_LEAD);
    draw_tentacle(sx + 2.0f, sy + 46.0f, 52.0f, t * 1.7f + 1.0f, 13.0f, COL_LEAD);
    gon(sx, sy, r, 9, COL_INK, 0.5f);
    hatch(sx - r + 4.0f, sy - r + 6.0f, 2.0f * r - 8.0f, 2.0f * r - 14.0f,
          COL_LEAD2, 7, 1);
    gon(sx, sy, r, 9, COL_INK, 0.5f);
    o_dot(sx - 12.0f, sy - 4.0f, 7.0f, COL_INK);
    o_dot(sx + 13.0f, sy - 2.0f, 5.0f, COL_INK);
    afill(sx - 14.0f, sy - 6.0f, 5.0f, 5.0f, COL_RED);
    afill(sx + 12.0f, sy - 4.0f, 3.0f, 3.0f, COL_RED);
    stroke(sx - 6.0f, sy + 14.0f, sx + 6.0f, sy + 20.0f, COL_INK, 0.6f);
    stroke(sx - 6.0f, sy + 22.0f, sx + 6.0f, sy + 14.0f, COL_INK, 0.6f);
    meter(24.0f, 30.0f, 192.0f, 10.0f, hp / max_hp, COL_RED2, COL_INK, COL_RED);
    set_font(8);
    text_at(28, 32, "KRAKEN", COL_INK);
}

static void draw_scene(void)
{
    float t = g_time;
    float cam = g_world - SHIP_X;
    float shake_x = 0.0f;
    float sc;
    float base, slope;
    int blink, i;

    g_pen_c = active_pen_colour();
    if(g_shake > 0.0f) shake_x = qsin(t * 60.0f) * 2.5f;
    sc = cam - shake_x;

    paper();

    {
        int dark = g_cfg_sea >= 3;
        cloud(48.0f, 52.0f, 22.0f, dark ? COL_LEAD : COL_PEN2, dark);
        cloud(150.0f, 40.0f, 18.0f, dark ? COL_LEAD : COL_PEN2, dark);
        cloud(210.0f, 62.0f, 14.0f, dark ? COL_LEAD : COL_PEN2, dark);
    }
    if(g_storm_flash > 0.0f) {
        afill(0.0f, 28.0f, (float)GAME_W, 150.0f, COL_SKY);
        bolt(120.0f, 40.0f, 90.0f, COL_YELLOW);
    } else if(g_cfg_sea >= 2) {
        rain(g_cfg_sea == 3 ? 22 : 8, t, g_wind);
    }

    /* Ocean.draw(cam, t, amp, wind, baseY, top, bottom): the deep lines run
     * from baseY+amp+16 down to 296 in the Java source. */
    ocean_draw(sc, t, g_amp, g_wind, SEA_BASE, 296.0f);

    for(i = 0; i < CAP_DROPS; i++) {
        drop_t *d;
        float dx, dy;

        if(!g_drops[i].live) continue;
        d = &g_drops[i];
        dx = d->x - sc;
        dy = ocean_height(d->x, t, g_amp, g_wind, SEA_BASE) - 5.0f +
             qsin(d->bob * 3.0f) * 2.0f;
        if(d->kind == 1) heart(dx, dy, 0.85f, COL_RED, 1, 0);
        else blob(dx, dy, 7.0f, COL_PEN);
    }

    for(i = 0; i < CAP_FOES; i++) {
        foe_t *e;
        float ex;

        if(!g_foes[i].live) continue;
        e = &g_foes[i];
        ex = e->x - sc;
        if(ex <= -42.0f || ex >= (float)GAME_W + 42.0f) continue;
        if(e->kind == KIND_BOAT) {
            float b = ocean_height(e->x, t, g_amp, g_wind, SEA_BASE);
            float barrel = clampf((g_world - e->x) / 60.0f, -0.5f, 0.5f);

            draw_ship(ex, b, -1.0f, ocean_slope(e->x, t, g_amp, g_wind) * 6.0f,
                      COL_INK, COL_RED, barrel, e->hp <= 1.0f, 0);
        } else if(e->kind == KIND_FISH) {
            draw_fish(ex, e->sy, -1.0f, COL_PEN);
        } else if(e->kind == KIND_ROCK) {
            draw_rock(ex, e->sy + 14.0f, 40.0f, 34.0f, COL_LEAD);
            set_font(8);
            text_at((int)(ex - 4.0f), (int)(e->sy - 16.0f), "!", COL_RED);
        } else if(e->kind == KIND_ARM) {
            float h = 22.0f + 30.0f * (0.5f + 0.5f * qsin(e->t * 1.2f + e->x * 0.01f));
            draw_tentacle(ex, SEA_BASE + 26.0f, h, e->t * 1.5f, 13.0f, COL_LEAD);
        } else if(e->kind == KIND_BLIMP) {
            draw_blimp(ex, 70.0f);
        } else {
            draw_boss(ex, 132.0f, e->hp, e->max_hp, t);
        }
    }

    for(i = 0; i < CAP_SHOTS; i++)
        if(g_shots[i].live) ball(g_shots[i].x - sc, g_shots[i].y, COL_INK, 0);
    for(i = 0; i < CAP_BALLS; i++) {
        ball_t *b;

        if(!g_balls[i].live) continue;
        b = &g_balls[i];
        if(b->bomb) {
            gon(b->x - sc, b->y, 3.0f, 6, COL_INK, 0.0f);
            stroke(b->x - sc, b->y - 3.0f, b->x - sc + 3.0f, b->y - 8.0f,
                   COL_RED, 0.0f);
        } else {
            ball(b->x - sc, b->y, COL_RED, 1);
        }
    }

    base = ocean_height(g_world, t, g_amp, g_wind, SEA_BASE);
    slope = ocean_slope(g_world, t, g_amp, g_wind) * 7.0f;
    blink = g_inv > 0.0f && (((int)(t * 12.0f)) % 2 == 0);
    if(!blink)
        draw_ship(SHIP_X, base, 1.0f, slope, COL_INK, COL_RED, g_angle,
                  g_hp <= 1.0f, 1);

    if(g_shield > 0.0f) {
        float cx = SHIP_X + 10.0f;
        float cy = base - 16.0f;
        int ph;

        for(ph = 1; ph <= 2; ph++) {
            float r = 24.0f + (float)ph * 7.0f;
            float prevx = 0.0f, prevy = 0.0f;
            int has = 0, k;

            for(k = 0; k <= 8; k++) {
                float a = -1.15f + (float)k * 0.2875f;
                float x = cx + qcos(a) * r;
                float y = cy + qsin(a) * r * 1.2f;

                if(has) stroke(prevx, prevy, x, y, g_pen_c, 0.6f);
                prevx = x;
                prevy = y;
                has = 1;
            }
        }
        for(i = 0; i < 4; i++) {
            float y = cy - 18.0f + (float)i * 12.0f;
            stroke(cx + 30.0f, y, cx + 38.0f, y + 6.0f, COL_LEAD2, 0.0f);
        }
        if(g_shield_hp <= 1.0f)
            stroke(cx + 18.0f, cy - 10.0f, cx + 30.0f, cy + 6.0f, COL_RED, 0.5f);
    }

    if(g_rail > 0.0f) {
        float ry = g_rail_y;
        float x;

        for(x = 0.0f; x < (float)GAME_W; x += 22.0f)
            stroke(x, ry, x + 12.0f, ry - 2.0f, g_pen_c, 0.5f);
        stroke(0.0f, ry + 3.0f, (float)GAME_W - 1.0f, ry + 3.0f, COL_LEAD2, 0.0f);
    }

    {
        float bx2 = SHIP_X + 22.0f;
        float by2 = base - 14.0f - g_angle * 16.0f;
        float ax = bx2, ay = by2;
        float d;

        for(d = 0.0f; d < 110.0f; d += 14.0f) {
            float nx = bx2 + qcos(g_angle) * (d + 6.0f);
            float ny = by2 - qsin(g_angle) * (d + 6.0f);

            stroke(ax, ay, nx, ny, COL_LEAD2, 0.0f);
            ax = nx + 5.0f;
            ay = ny + 1.0f;
        }
    }
    ocean_splash(SHIP_X + 26.0f, base + 2.0f, 9.0f, COL_WATER2);

    draw_fx(sc);

    if(g_strike > 0.0f) {
        float kk = 1.1f - g_strike;

        scribble(0.0f, 120.0f + qsin(kk * 6.0f) * 30.0f, (float)GAME_W, 60.0f,
                 COL_PEN, kk * 4.0f);
        pen_icon(20.0f + kk * 200.0f, 190.0f, 1.0f, COL_RED);
        set_font(8);
        ctext(GAME_W / 2, 96, "GACH XOA", COL_RED);
    }

    if(g_hit > 0.0f) gon(SHIP_X, base - 8.0f, 34.0f, 8, COL_RED, 0.9f);

    for(i = 0; i < CAP_POP; i++) {
        if(g_pop[i].live) {
            set_font(8);
            text_at((int)g_pop[i].x,
                    (int)(g_pop[i].y - (0.8f - g_pop[i].life) * 14.0f),
                    g_pop[i].t, g_pop[i].c);
        }
    }

    hud();

    if(g_banner > 0.0f) {
        char *p = g_line;
        int k = 0;

        while("DOT "[k]) { *p++ = "DOT "[k]; k++; }
        p = append_uint(p, g_wave);
        *p = '\0';
        banner(120.0f, g_line, COL_RED, COL_PAPER);
        set_font(8);
        ctext(GAME_W / 2, 140, "SONG CHIA HET CHO 3: KRAKEN", COL_LEAD);
    }

    if(mode_is(g_mode_run)) {
        softkeys("BAN", "DUNG");
    } else if(mode_is(g_mode_pause)) {
        afill(0.0f, 80.0f, (float)GAME_W, 180.0f, COL_PAPER);
        ui_panel(22.0f, 86.0f, (float)GAME_W - 44.0f, 106.0f, "TAM DUNG");
        ui_item(32.0f, 110.0f, (float)GAME_W - 64.0f, "Tiep tuc", g_pause_sel == 1);
        ui_item(32.0f, 132.0f, (float)GAME_W - 64.0f, "Choi lai", g_pause_sel == 2);
    } else if(mode_is(g_mode_over)) {
        int gr = g_grade;
        const char *letter = gr == 3 ? "A" : (gr == 2 ? "B" : "C");

        afill(0.0f, 40.0f, (float)GAME_W, 250.0f, COL_PAPER);
        ui_panel(16.0f, 46.0f, (float)GAME_W - 32.0f, 106.0f, "KET THUC");
        set_font(14);
        ctext(GAME_W / 2, 70, num((int)g_score), COL_INK);
        set_font(8);
        {
            char line[40];
            int k = 0, j;
            const char *pre = "KY LUC: ";

            for(j = 0; pre[j]; j++) line[k++] = pre[j];
            {
                char *p = append_uint(&line[k], g_cfg_hi);
                *p = '\0';
            }
            ctext(GAME_W / 2, 92, line, COL_LEAD);

            k = 0;
            pre = "TONG: ";
            for(j = 0; pre[j]; j++) line[k++] = pre[j];
            {
                char *p = append_uint(&line[k], g_cfg_total);
                *p = '\0';
            }
            ctext(GAME_W / 2, 104, line, COL_LEAD);
        }
        if(g_new_record) ctext(GAME_W / 2, 118, "KY LUC MOI!", COL_GREEN);
        ctext(GAME_W / 2, 130, "BAM 5 CHOI LAI, 0 DE DUNG", COL_LEAD2);

        ui_panel(16.0f, 158.0f, (float)GAME_W - 32.0f, 92.0f, "XEP LOAI");
        set_font(14);
        ctext(58, 178, letter, COL_RED);
        jit_reset(99);
        stroke(40.0f, 176.0f, 76.0f, 178.0f, COL_RED, 1.2f);
        stroke(76.0f, 178.0f, 74.0f, 196.0f, COL_RED, 1.2f);
        stroke(74.0f, 196.0f, 38.0f, 194.0f, COL_RED, 1.2f);
        stroke(38.0f, 194.0f, 40.0f, 176.0f, COL_RED, 1.2f);
        set_font(8);
        text_at(86, 172, gr == 3 ? "Xuat sac" : (gr == 2 ? "Kha" : "Trung binh"),
                COL_RED);
        {
            char line[40];
            int k = 0, j;
            const char *pre = "Hang: ";

            for(j = 0; pre[j]; j++) line[k++] = pre[j];
            line[k++] = letter[0];
            line[k] = '\0';
            text_at(86, 186, line, COL_INK);
        }
        text_at(86, 198, g_cfg_pen >= 2 ? "But: But chi" : "But: But bi", COL_PEN);

        draw_ship(56.0f, 272.0f, 1.0f, 0.4f, COL_INK, COL_RED, -0.35f, 1, 0);
        draw_fish(140.0f, 262.0f, -1.0f, COL_LEAD);
        set_font(8);
        softkeys("CHOI LAI", "DUNG");
    }
}

/* ---- present: 240x320 -> 720x960 at y=300 -------------------------------- */

static void present(void)
{
    int y;

    /* Letterbox: the notebook page is a 240x320 sheet on a ruled desk. */
    {
        volatile uint16_t *band = g_band;
        int i;

        for(i = 0; i < CANVAS_W * 3; i++) band[i] = COL_RULE;
        for(y = 0; y < OY; y += 3) {
            int h = OY - y;
            if(h > 3) h = 3;
            g_api->blit_pixels(0, y, CANVAS_W, h, CANVAS_W, g_band);
        }
        for(y = OY + GAME_H * SCALE; y < CANVAS_H; y += 3) {
            int h = CANVAS_H - y;
            if(h > 3) h = 3;
            g_api->blit_pixels(0, y, CANVAS_W, h, CANVAS_W, g_band);
        }
    }

    for(y = 0; y < GAME_H; y++) {
        volatile uint16_t *band = g_band;
        const uint16_t *src = &g_fb[y * GAME_W];
        int x, r;

        for(x = 0; x < GAME_W; x++) {
            uint16_t c = src[x];
            band[x * 3] = c;
            band[x * 3 + 1] = c;
            band[x * 3 + 2] = c;
        }
        for(r = 1; r < 3; r++) {
            int k;
            for(k = 0; k < CANVAS_W; k++) band[r * CANVAS_W + k] = band[k];
        }
        g_api->blit_pixels(0, OY + y * SCALE, CANVAS_W, 3, CANVAS_W, g_band);
    }
}

static void frame(void)
{
    g_time += DT;
    update(DT);
    draw_scene();
    present();
}

/* ---- lifecycle ----------------------------------------------------------- */

void *create(const qeaf_app_api_v5 *api)
{
    if(api == NULL) return NULL;
    if(api->abi_version < VQEAF_APP_API_V5_VERSION) return NULL;
    if(api->blit_pixels == NULL || api->draw_text == NULL) return NULL;
    g_api = api;
    g_ready = 0;
    return (void *)&g_ready;
}

int start(void *handle)
{
    (void)handle;
    if(g_api == NULL) return -1;
    fresh();
    g_ready = 1;
    g_time = 0.0f;
    draw_scene();
    present();
    if(g_api->log_utf8 != NULL) g_api->log_utf8("Bien Muc: san sang");
    return 0;
}

int pause(void *handle)
{
    (void)handle;
    if(g_api != NULL && g_api->log_utf8 != NULL) g_api->log_utf8("Bien Muc: tam dung");
    return 0;
}

int resume(void *handle)
{
    (void)handle;
    if(g_api != NULL && g_api->log_utf8 != NULL) g_api->log_utf8("Bien Muc: tiep tuc");
    return 0;
}

int stop(void *handle)
{
    (void)handle;
    g_ready = 0;
    if(g_api != NULL && g_api->log_utf8 != NULL) g_api->log_utf8("Bien Muc: dung");
    return 0;
}

void destroy(void *handle)
{
    (void)handle;
    g_api = NULL;
    g_ready = 0;
}

/* ---- input --------------------------------------------------------------- */

#define KEY_ESC   1
#define KEY_1     2
#define KEY_3     4
#define KEY_5     6
#define KEY_0     11
#define KEY_Q     16
#define KEY_ENTER 28
#define KEY_SPACE 57
#define KEY_AST   55
#define KEY_A     97
#define KEY_D     100
#define KEY_W     119
#define KEY_S     115
#define KEY_LEFT  105
#define KEY_RIGHT 106
#define KEY_UP    103
#define KEY_DOWN  108

static void handle_key(unsigned code)
{
    if(mode_is(g_mode_over)) {
        if(code == KEY_5 || code == KEY_ENTER || code == KEY_SPACE) {
            fresh();
            frame();
        } else if(code == KEY_0 || code == KEY_ESC) {
            mode_set(g_mode_run);
            fresh();
            frame();
        }
        return;
    }
    if(mode_is(g_mode_pause)) {
        if(code == KEY_UP || code == KEY_W) {
            g_pause_sel--;
            if(g_pause_sel < 1) g_pause_sel = 2;
            frame();
        } else if(code == KEY_DOWN || code == KEY_S) {
            g_pause_sel++;
            if(g_pause_sel > 2) g_pause_sel = 1;
            frame();
        } else if(code == KEY_5 || code == KEY_ENTER) {
            if(g_pause_sel == 1) mode_set(g_mode_run);
            else fresh();
            frame();
        } else if(code == KEY_0 || code == KEY_ESC) {
            mode_set(g_mode_run);
            frame();
        }
        return;
    }

    if(code == KEY_LEFT || code == KEY_A) {
        g_throttle = clampf(g_throttle - 0.18f, 0.45f, 1.85f);
    } else if(code == KEY_RIGHT || code == KEY_D) {
        g_throttle = clampf(g_throttle + 0.18f, 0.45f, 1.85f);
    } else if(code == KEY_UP || code == KEY_W) {
        g_angle = clampf(g_angle + 0.09f, -0.12f, 1.05f);
    } else if(code == KEY_DOWN || code == KEY_S) {
        g_angle = clampf(g_angle - 0.09f, -0.12f, 1.05f);
    } else if(code == KEY_1) {
        do_shield();
    } else if(code == KEY_3) {
        do_rail();
    } else if(code == KEY_5 || code == KEY_ENTER || code == KEY_SPACE) {
        fire();
    } else if(code == KEY_AST || code == KEY_Q) {
        do_strike();
    } else if(code == KEY_0 || code == KEY_ESC) {
        mode_set(g_mode_pause);
        g_pause_sel = 1;
    }
    frame();
}

void on_input(void *handle, const qeaf_input_event_t *event)
{
    (void)handle;
    if(event == NULL || !g_ready) return;
    if(!event->pressed) return;

    if(event->type == QEAF_INPUT_KEY) {
        handle_key(event->code);
    } else if(event->type == QEAF_INPUT_TOUCH) {
        int gx = ((int)event->x) / SCALE;
        int gy = (((int)event->y) - OY) / SCALE;

        if(gy < 27) mode_set(g_mode_pause);
        else if(mode_is(g_mode_over)) fresh();
        else if(gx > GAME_W / 2) fire();
        else if(gy > GAME_H - 60) do_strike();
        else do_shield();
        frame();
    }
}
