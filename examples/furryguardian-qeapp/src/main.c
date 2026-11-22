/* main.c - "Furry Guardian: Siege Camp" native QEAPP port.
 *
 * Source game: D:\Program\android\gameapk\furry_guardian (Android WebView
 * wrapper around a Three.js game, v4.6.4, ~10k lines of JS + GLB models).
 * QEAF OS has no browser, no JavaScript engine and no GPU/WebGL, so this is a
 * direct port of the game's rules and systems to the QEAPP native ABI with a
 * software-rendered isometric view instead of the Three.js scene:
 *
 *   - wave schedule        : startWave(), count = 7 + wave*2, boss every 5th
 *   - enemy table          : goblin/brute/slime/flyingDino/falcon/boss with the
 *                            same hp, speed, damage, range and cooldown scaling
 *   - camp defence         : three gates (270 hp total = 3 x 90), core 240 hp,
 *                            camp integrity decides the wave star
 *   - economy              : gold/meat loot dropped by kills, auto-collected
 *                            near the guardian, depot stock 10 gold / 30 meat
 *   - build                : tower pads, 60 gold, 8 s construction, 24 dmg / 0.8 s
 *   - Heroes               : cost 38 + 8 per existing Hero, meat, six classes
 *   - skills               : Q ice nova (34 dmg, 3.1 s slow, 7.2 s cd),
 *                            E chain lightning (58 dmg, -12% per jump, 8.5 s),
 *                            Space whirlwind (42 dmg, 3.35 radius, 6.5 s),
 *                            R gas burst (10 dmg / 0.22 s, 3.2 radius, 5.5 s)
 *   - guardian             : 115 hp, 14 dmg / 0.28 s auto-attack, 15 s down
 *   - defeat               : core at 0 hp ends the run, Enter restarts
 *
 * Deliberate differences, stated instead of hidden:
 *   - The 3D scene (Three.js, the GLB guardian model, lights and shaders) is
 *     replaced by flat-shaded isometric prisms drawn into a 240x520 RGB565
 *     framebuffer, upscaled 3x with blit_pixels. Low-poly silhouettes, camp
 *     layout, gates, towers, core, units and FX keep the same read.
 *   - No procedural infinite map, fog of war, workers, assistant, save files,
 *     ads or audio: the ABI has no timer, storage-write or audio call, and the
 *     port keeps the siege loop that the whole game is built around.
 *   - Vietnamese labels are written without diacritics because the module
 *     embeds the OS 8x8 ASCII face.
 *   - The ABI has no frame callback, so one on_input event advances the
 *     simulation by one 1/30 s step and redraws (same rule as the other native
 *     QEAPP ports). Keyboard auto-repeat in the simulator window is the frame
 *     clock; a plain launch renders the starting camp only.
 */
#include <stddef.h>
#include <stdint.h>

#include "qeaf_api.h"
#include "qeaf_native_abi.h"
#include "font8x8.h"

/* ---- canvas -------------------------------------------------------------- */

#define LW 240
#define LH 520
#define SC 3
#define CW (LW * SC)
#define CH (LH * SC)
#define BAND_ROWS 3

#define SCENE_TOP 52
#define SCENE_BOT 392
#define SCENE_MID ((SCENE_TOP + SCENE_BOT) / 2)

#define PX 11
#define PY 6
#define PZ 9

#define DT_MS 33
#define U 256              /* fixed point: 256 units per world unit (Q8) */

#define MAX_ENEMIES 40
#define MAX_HEROES  6
#define MAX_TOWERS  3
#define MAX_LOOT    24
#define MAX_FX      20
#define MAX_ITEMS   110
#define MAX_PROPS   40

/* ---- palette (RGB565) ---------------------------------------------------- */

#define C(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define COL_SKY_TOP  C(18, 38, 48)
#define COL_SKY_MID  C(32, 66, 70)
#define COL_SKY_LOW  C(58, 96, 84)
#define COL_GRASS    C(44, 92, 62)
#define COL_GRASS2   C(38, 82, 56)
#define COL_DIRT     C(104, 84, 58)
#define COL_DIRT2    C(92, 74, 50)
#define COL_CAMP     C(118, 96, 66)
#define COL_WALL     C(122, 92, 56)
#define COL_WALL_X   C(96, 72, 44)
#define COL_WALL_Z   C(108, 82, 50)
#define COL_GATE     C(140, 104, 62)
#define COL_GATE_TOP C(96, 70, 42)
#define COL_CORE     C(168, 132, 92)
#define COL_CORE_TOP C(196, 160, 112)
#define COL_ROOF     C(146, 66, 50)
#define COL_DOOR     C(60, 40, 28)
#define COL_PLAYER   C(214, 148, 92)
#define COL_PLAYER_X C(170, 110, 66)
#define COL_PLAYER_Z C(188, 126, 78)
#define COL_HEAD     C(232, 176, 118)
#define COL_EAR      C(246, 206, 156)
#define COL_GOBLIN   C(108, 168, 96)
#define COL_GOBLIN_X C(78, 126, 70)
#define COL_GOBLIN_Z C(92, 146, 82)
#define COL_BRUTE    C(178, 92, 70)
#define COL_BRUTE_X  C(136, 66, 50)
#define COL_SLIME    C(110, 196, 206)
#define COL_SLIME_X  C(78, 148, 158)
#define COL_FLY      C(120, 142, 214)
#define COL_FLY_X    C(88, 104, 168)
#define COL_BOSS     C(158, 96, 200)
#define COL_BOSS_X   C(114, 66, 150)
#define COL_GOLD     C(242, 196, 62)
#define COL_MEAT     C(214, 96, 88)
#define COL_ICE      C(154, 232, 255)
#define COL_BOLT     C(190, 152, 255)
#define COL_FIRE     C(255, 152, 64)
#define COL_GAS      C(154, 198, 96)
#define COL_INK      C(236, 240, 234)
#define COL_DIM      C(150, 166, 158)
#define COL_BAR_BG   C(30, 36, 36)
#define COL_PANEL    C(18, 34, 32)
#define COL_PANEL2   C(28, 52, 46)
#define COL_ACCENT   C(240, 196, 74)
#define COL_HP       C(120, 214, 96)
#define COL_HP_LOW   C(214, 84, 64)
#define COL_GATEBAR  C(226, 172, 72)
#define COL_COREBAR  C(228, 118, 92)

/* ---- framework ----------------------------------------------------------- */

static const qeaf_app_api_v5 *g_api;
static uint16_t g_fb[LW * LH] __attribute__((aligned(16)));
static uint16_t g_band[CW * BAND_ROWS] __attribute__((aligned(16)));

/* ---- raster -------------------------------------------------------------- */

static void present(void);

static void fb_set(int x, int y, uint16_t c)
{
    if(x < 0 || y < 0 || x >= LW || y >= LH) return;
    g_fb[y * LW + x] = c;
}

static void fb_rect(int x, int y, int w, int h, uint16_t c)
{
    int i, j;

    if(x < 0) { w += x; x = 0; }
    if(y < 0) { h += y; y = 0; }
    if(x + w > LW) w = LW - x;
    if(y + h > LH) h = LH - y;
    if(w <= 0 || h <= 0) return;
    for(j = 0; j < h; j++) {
        uint16_t *row = g_fb + (y + j) * LW + x;
        for(i = 0; i < w; i++) row[i] = c;
    }
}

static void hspan(int y, int x0, int x1, uint16_t c)
{
    int i;

    if(y < 0 || y >= LH) return;
    if(x0 < 0) x0 = 0;
    if(x1 >= LW) x1 = LW - 1;
    for(i = x0; i <= x1; i++) g_fb[y * LW + i] = c;
}

typedef struct { int x, err, adx, dy, sx; } edge_t;

static edge_t edge_make(int x0, int y0, int x1, int y1)
{
    edge_t e;

    e.dy = y1 - y0;
    if(e.dy < 0) e.dy = -e.dy;
    e.adx = x1 > x0 ? x1 - x0 : x0 - x1;
    e.sx = x1 >= x0 ? 1 : -1;
    e.x = x0;
    e.err = e.dy >> 1;
    return e;
}

static void edge_step(edge_t *e)
{
    if(e->dy <= 0) return;
    e->err += e->adx;
    while(e->err >= e->dy) {
        e->x += e->sx;
        e->err -= e->dy;
    }
}

static void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c)
{
    int t, y;
    edge_t a, b, d;

    if(y0 > y1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if(y1 > y2) { t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    if(y0 > y1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if(y0 == y2) return;
    a = edge_make(x0, y0, x2, y2);
    b = edge_make(x0, y0, x1, y1);
    d = edge_make(x1, y1, x2, y2);
    for(y = y0; y <= y2; y++) {
        int xa = a.x, xb;
        if(y <= y1 && y1 > y0) xb = b.x;
        else xb = d.x;
        if(xa > xb) { t = xa; xa = xb; xb = t; }
        hspan(y, xa, xb, c);
        edge_step(&a);
        edge_step(&b);
        edge_step(&d);
    }
}

static void line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    int ax = dx > 0 ? dx : -dx, ay = dy > 0 ? dy : -dy;
    int err, i;

    if(ax >= ay) {
        err = ax >> 1;
        for(i = 0; i <= ax; i++) {
            fb_set(x0, y0, c);
            fb_set(x0, y0 + 1, c);
            err -= ay;
            if(err < 0) { y0 += sy; err += ax; }
            x0 += sx;
        }
    } else {
        err = ay >> 1;
        for(i = 0; i <= ay; i++) {
            fb_set(x0, y0, c);
            fb_set(x0 + 1, y0, c);
            err -= ax;
            if(err < 0) { x0 += sx; err += ay; }
            y0 += sy;
        }
    }
}

static void text(int x, int y, const char *s, int scale, uint16_t c)
{
    int cx = x;

    if(s == NULL) return;
    while(*s != '\0') {
        unsigned char ch = (unsigned char)*s++;
        const uint8_t *rows;
        int r, b;

        if(ch >= 128) ch = (unsigned char)'?';
        rows = vqeaf_font8x8[ch & 0x7F];
        for(r = 0; r < 8; r++) {
            uint8_t bits = rows[r];
            for(b = 0; b < 8; b++) {
                if(bits & (uint8_t)(1u << b))
                    fb_rect(cx + b * scale, y + r * scale, scale, scale, c);
            }
        }
        cx += 8 * scale;
    }
}

static int text_w(const char *s, int scale)
{
    int n = 0;

    while(s[n] != '\0') n++;
    return n * 8 * scale;
}

static char *append_uint(char *out, int32_t value)
{
    int pos, started = 0;

    if(value < 0) { *out++ = '-'; value = -value; }
    if(value == 0) { *out++ = '0'; return out; }
    if(value > 9999999) value = 9999999;
    for(pos = 6; pos >= 0; pos--) {
        int32_t scale = 1;
        int digit = 0, k;

        for(k = 0; k < pos; k++) scale *= 10;
        while(value >= scale && digit < 9) { value -= scale; digit++; }
        if(digit > 0 || started) {
            *out++ = (char)('0' + digit);
            started = 1;
        }
    }
    return out;
}

static char *append_str(char *out, const char *s)
{
    while(*s != '\0') *out++ = *s++;
    return out;
}

/* ---- isometric projection ------------------------------------------------ */

typedef struct { int x, y; } pt_t;
typedef struct { int32_t x, z; } wxz_t;

static int32_t g_camx, g_camz;

static pt_t proj(int32_t wx, int32_t wy, int32_t wz)
{
    pt_t p;
    int32_t dx = wx - g_camx;
    int32_t dz = wz - g_camz;

    p.x = (LW / 2) + (int)(((dx - dz) * PX) >> 8);
    p.y = SCENE_MID + (int)(((dx + dz) * PY) >> 8) - (int)((wy * PZ) >> 8);
    return p;
}

static void quad3(int32_t x0, int32_t z0, int32_t x1, int32_t z1,
                  int32_t x2, int32_t z2, int32_t x3, int32_t z3,
                  int32_t y, uint16_t c)
{
    pt_t a = proj(x0, y, z0);
    pt_t b = proj(x1, y, z1);
    pt_t d = proj(x2, y, z2);
    pt_t e = proj(x3, y, z3);

    tri(a.x, a.y, b.x, b.y, d.x, d.y, c);
    tri(a.x, a.y, d.x, d.y, e.x, e.y, c);
}

/* Flat-shaded box: top face plus the +x and +z side faces. */
static void box3(int32_t x, int32_t z, int32_t r, int32_t ybase, int32_t h,
                 uint16_t top, uint16_t fx, uint16_t fz)
{
    int32_t y1 = ybase + h;
    pt_t t0 = proj(x - r, y1, z - r), t1 = proj(x + r, y1, z - r);
    pt_t t2 = proj(x + r, y1, z + r), t3 = proj(x - r, y1, z + r);
    pt_t b0, b1, b2, b3;

    /* +x face */
    b0 = proj(x + r, ybase, z - r);
    b2 = proj(x + r, ybase, z + r);
    tri(b0.x, b0.y, b2.x, b2.y, t2.x, t2.y, fx);
    tri(b0.x, b0.y, t2.x, t2.y, t1.x, t1.y, fx);
    /* +z face */
    b1 = proj(x + r, ybase, z + r);
    b3 = proj(x - r, ybase, z + r);
    tri(b3.x, b3.y, b1.x, b1.y, t2.x, t2.y, fz);
    tri(b3.x, b3.y, t2.x, t2.y, t3.x, t3.y, fz);
    /* top */
    tri(t0.x, t0.y, t1.x, t1.y, t2.x, t2.y, top);
    tri(t0.x, t0.y, t2.x, t2.y, t3.x, t3.y, top);
}

static void ground_diamond(int32_t x, int32_t z, int32_t r, uint16_t c)
{
    quad3(x - r, z - r, x + r, z - r, x + r, z + r, x - r, z + r, 0, c);
}

static void ring(int32_t x, int32_t z, int32_t r, uint16_t c)
{
    pt_t a = proj(x - r, 6, z - r), b = proj(x + r, 6, z - r);
    pt_t d = proj(x + r, 6, z + r), e = proj(x - r, 6, z + r);

    line(a.x, a.y, b.x, b.y, c);
    line(b.x, b.y, d.x, d.y, c);
    line(d.x, d.y, e.x, e.y, c);
    line(e.x, e.y, a.x, a.y, c);
}

/* ---- small math ---------------------------------------------------------- */

static uint32_t g_seed = 0x2545F491u;

static uint32_t rnd(void)
{
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

static int32_t isqrt32(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;

    while(b > v) b >>= 2;
    while(b != 0) {
        if(v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return (int32_t)r;
}

static uint32_t udiv32(uint32_t n, uint32_t d)
{
    uint32_t q = 0, r = 0;
    int i;

    if(d == 0) return 0;
    for(i = 31; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if(r >= d) { r -= d; q |= 1u << i; }
    }
    return q;
}

static int32_t dist2(int32_t ax, int32_t az, int32_t bx, int32_t bz)
{
    int32_t dx = ax - bx, dz = az - bz;
    return dx * dx + dz * dz;
}

static void move_toward(int32_t *x, int32_t *z, int32_t tx, int32_t tz, int32_t spd)
{
    int32_t dx = tx - *x, dz = tz - *z;
    int32_t adx = dx < 0 ? -dx : dx;
    int32_t adz = dz < 0 ? -dz : dz;
    uint32_t len, fx, fz;
    int32_t sx, sz;

    if(adx == 0 && adz == 0) return;
    len = (uint32_t)isqrt32((uint32_t)(adx * adx + adz * adz));
    if(len == 0) return;
    if((int32_t)len <= spd) { *x = tx; *z = tz; return; }
    fx = udiv32((uint32_t)adx << 16, len);
    fz = udiv32((uint32_t)adz << 16, len);
    sx = (int32_t)((fx * (uint32_t)spd) >> 16);
    sz = (int32_t)((fz * (uint32_t)spd) >> 16);
    *x += dx < 0 ? -sx : sx;
    *z += dz < 0 ? -sz : sz;
}

/* ---- world layout -------------------------------------------------------- */

/* Gates sit on three sides of the camp; enemies walk in from further out. */
static const int32_t g_gate_x[3] = { -4 * U - 128, 4 * U + 128, 128 };
static const int32_t g_gate_z[3] = { 2 * U, -2 * U, 4 * U + 128 };
static const int32_t g_spawn_x[3] = { -9 * U, 9 * U, 2 * U };
static const int32_t g_spawn_z[3] = { 4 * U + 128, -4 * U - 128, 9 * U };

static const int32_t g_pad_x[MAX_TOWERS] = { -2 * U - 64, 2 * U + 64, 96 };
static const int32_t g_pad_z[MAX_TOWERS] = { 40, -80, 2 * U + 96 };

#define CORE_X 0
#define CORE_Z 0

#define GATE_HP_MAX 90

static void present(void)
{
    int y, x, r;

    if(g_api == NULL || g_api->blit_pixels == NULL) return;
    for(y = 0; y < LH; y++) {
        const uint16_t *src = g_fb + y * LW;
        for(x = 0; x < LW; x++) {
            uint16_t c = src[x];
            for(r = 0; r < BAND_ROWS; r++) {
                int base = r * CW + x * SC;
                g_band[base] = c;
                g_band[base + 1] = c;
                g_band[base + 2] = c;
            }
        }
        g_api->blit_pixels(0, y * SC, CW, BAND_ROWS, CW, g_band);
    }
}

/* ---- game state ---------------------------------------------------------- */

typedef struct {
    int32_t x, z;
    int16_t hp, maxhp;
    uint8_t kind;   /* 0 goblin, 1 brute, 2 slime, 3 flyingDino, 4 falcon, 5 boss */
    uint8_t lane;
    uint8_t state;
    uint8_t dead;
    uint16_t cd, slow, flash;
} enemy_t;

typedef struct {
    int32_t x, z;
    int16_t hp, maxhp;
    uint8_t kind, alive;
    uint16_t cd;
} hero_t;

typedef struct { uint8_t built, building; uint16_t build_ms, cd; } tower_t;

typedef struct { int32_t x, z; uint8_t kind, alive; uint16_t life; } loot_t;

typedef struct {
    int32_t x, z, x2, z2;
    uint8_t kind;      /* 0 ring, 1 bolt, 2 gas, 3 burst */
    uint16_t t, life;
    uint16_t col;
} fx_t;

typedef struct { int32_t x, z; uint8_t kind; } prop_t;

static struct {
    int32_t px, pz;
    int32_t tx, tz;
    int32_t mvx, mvz;
    int32_t php, pmaxhp;
    int32_t gold, meat;
    int32_t wave, kills, star;
    int32_t phase;          /* 0 build, 1 combat, 2 game over */
    int32_t build_ms, wave_ms;
    int32_t core_hp, core_max;
    int32_t gate_hp[3];
    int32_t down_ms;
    int32_t inner;
    int32_t cd_ice, cd_bolt, cd_whirl, cd_gas, pcd;
    int32_t gas_ms, gas_tick;
    int32_t spawn_left, spawn_gap, spawn_timer, spawn_i;
    int32_t msg_ms;
    int32_t paused;
    int32_t ready;
    int32_t ecount, hcount, lcount, fcount;
    enemy_t enemies[MAX_ENEMIES];
    hero_t heroes[MAX_HEROES];
    tower_t towers[MAX_TOWERS];
    loot_t loot[MAX_LOOT];
    fx_t fx[MAX_FX];
} g;

static prop_t g_props[MAX_PROPS];
static int32_t g_prop_count;
static char g_msg[52] __attribute__((aligned(16)));

/* cos/sin on a 16-point ring, Q8. Keeps prop placement free of libm. */
static const int32_t g_dir_x[16] = {
    256, 236, 181, 98, 0, -98, -181, -236,
    -256, -236, -181, -98, 0, 98, 181, 236
};
static const int32_t g_dir_z[16] = {
    0, 98, 181, 236, 256, 236, 181, 98,
    0, -98, -181, -236, -256, -236, -181, -98
};

static void draw_frame(void);

/* ---- message ------------------------------------------------------------- */

static void msg(const char *s)
{
    char *p = g_msg;
    int n = 0;

    while(s[n] != '\0' && n < 44) { *p++ = s[n]; n++; }
    *p = '\0';
    g.msg_ms = 2800;
}

static void msg_wave(void)
{
    char buf[52];
    char *p = buf;

    p = append_str(p, "DOT ");
    p = append_uint(p, g.wave);
    p = append_str(p, ": BA HUONG TAN CONG!");
    *p = '\0';
    msg(buf);
}

/* ---- enemy table (startWave / spawnEnemy) -------------------------------- */

static int enemy_hp(uint8_t kind)
{
    int w = (int)g.wave;

    switch(kind) {
    case 0: return 70 + w * 12;
    case 1: return 158 + w * 19;
    case 2: return 92 + w * 13;
    case 3: return 105 + w * 15;
    case 4: return 88 + w * 12;
    default: return 620 + w * 68;
    }
}

static int32_t enemy_speed(uint8_t kind)
{
    switch(kind) {
    case 0: return 8;
    case 1: return 7;
    case 2: return 9;
    case 3: return 11;
    case 4: return 15;
    default: return 6;
    }
}

static int32_t enemy_range(uint8_t kind)
{
    switch(kind) {
    case 0: return 200;
    case 1: return 294;
    case 2: return 230;
    case 3: return 666;
    case 4: return 768;
    default: return 422;
    }
}

static uint16_t enemy_cd(uint8_t kind)
{
    switch(kind) {
    case 0: return 1750;
    case 1: return 1550;
    case 2: return 1420;
    case 3: return 1700;
    case 4: return 1080;
    default: return 1300;
    }
}

static int enemy_dmg(uint8_t kind)
{
    int w = (int)g.wave;

    switch(kind) {
    case 0: return 6 + w * 36 / 100;
    case 1: return 11;
    case 2: return 7 + w * 32 / 100;
    case 3: return 8 + w * 38 / 100;
    case 4: return 6 + w * 34 / 100;
    default: return 22 + w * 50 / 100;
    }
}

/* ---- effects and loot ---------------------------------------------------- */

static void add_fx(uint8_t kind, int32_t x, int32_t z, int32_t x2, int32_t z2,
                   uint16_t col, uint16_t life)
{
    fx_t *f;

    if(g.fcount >= MAX_FX) {
        int i;
        for(i = 1; i < MAX_FX; i++) g.fx[i - 1] = g.fx[i];
        g.fcount = MAX_FX - 1;
    }
    f = &g.fx[g.fcount++];
    f->kind = kind;
    f->x = x; f->z = z;
    f->x2 = x2; f->z2 = z2;
    f->col = col;
    f->t = 0;
    f->life = life;
}

static void drop_loot(int32_t x, int32_t z)
{
    loot_t *l;

    if(g.lcount >= MAX_LOOT) {
        int i;
        for(i = 1; i < MAX_LOOT; i++) g.loot[i - 1] = g.loot[i];
        g.lcount = MAX_LOOT - 1;
    }
    l = &g.loot[g.lcount++];
    l->x = x + ((int32_t)(rnd() % 200u) - 100);
    l->z = z + ((int32_t)(rnd() % 200u) - 100);
    l->kind = (uint8_t)(rnd() & 1u);
    l->life = 60000 / DT_MS;
    l->alive = 1;
}

static void damage_player(int dmg)
{
    if(g.down_ms > 0) return;
    g.php -= dmg;
    if(g.php <= 0) {
        g.php = 0;
        g.down_ms = 15000;
        msg("NHAN VAT BI HA! HOI SINH SAU 15s");
    }
}

static void damage_enemy(enemy_t *e, int dmg)
{
    if(e->dead) return;
    e->hp = (int16_t)(e->hp - dmg);
    e->flash = 120;
    if(e->hp <= 0) {
        e->dead = 1;
        g.kills++;
        drop_loot(e->x, e->z);
        add_fx(3, e->x, e->z, 600, 0, COL_FIRE, 300);
    }
}

/* ---- waves --------------------------------------------------------------- */

static uint8_t pick_kind(void)
{
    uint32_t roll = rnd() % 100u;

    if(g.wave >= 4 && roll < 4u) return 4;
    if(g.wave >= 2 && roll < 14u) return 3;
    if(g.wave >= 2 && roll < 34u) return 2;
    if(g.wave >= 2 && (g.spawn_i % 6) == 0) return 1;
    return 0;
}

static void spawn_enemy(uint8_t kind, uint8_t lane)
{
    enemy_t *e;
    int32_t ox, oz;

    if(g.ecount >= MAX_ENEMIES) return;
    e = &g.enemies[g.ecount++];
    e->kind = kind;
    e->lane = lane;
    e->maxhp = (int16_t)enemy_hp(kind);
    e->hp = e->maxhp;
    e->cd = (uint16_t)(150 + (rnd() % 600u));
    e->slow = 0;
    e->flash = 0;
    e->state = 0;
    e->dead = 0;
    ox = ((int32_t)(rnd() % 1000u) - 500) * 3;
    oz = ((int32_t)(rnd() % 700u) - 350) * 3;
    e->x = g_spawn_x[lane] + ox;
    e->z = g_spawn_z[lane] + oz;
}

static void start_wave(void)
{
    g.wave++;
    g.phase = 1;
    g.wave_ms = 0;
    g.spawn_left = 7 + (int)g.wave * 2;
    g.spawn_i = 0;
    g.spawn_gap = 620 - (int)g.wave * 12;
    if(g.spawn_gap < 360) g.spawn_gap = 360;
    g.spawn_timer = 450;
    if((g.wave % 5) == 0) {
        g.spawn_left += 1;             /* boss joins the wave */
        spawn_enemy(5, (uint8_t)(g.wave % 3));
        msg("BOSS XUAT HIEN!");
    } else {
        msg_wave();
    }
    if(g_api != NULL && g_api->log_utf8 != NULL) {
        char buf[24];
        char *p = buf;
        p = append_str(p, "Furry Guardian: wave ");
        p = append_uint(p, g.wave);
        *p = '\0';
        g_api->log_utf8(buf);
    }
}

static void wave_complete(void)
{
    char buf[52];
    char *p = buf;

    g.phase = 0;
    g.build_ms = 24000;
    g.gold += 25 + g.wave * 4;
    g.meat += 12 + g.wave * 2;
    if(g.core_hp * 2 >= g.core_max) g.star++;
    p = append_str(p, "DOT ");
    p = append_uint(p, g.wave);
    p = append_str(p, " XONG! +VANG +THIT");
    *p = '\0';
    msg(buf);
    if(g_api != NULL && g_api->log_utf8 != NULL) g_api->log_utf8("Furry Guardian: wave cleared");
}

/* ---- skills (useIce / useLightning / useWhirlwind / useGasSpecial) ------- */

static void skill_ice(void)
{
    int i;

    if(g.cd_ice > 0 || g.down_ms > 0) return;
    g.cd_ice = 7200;
    for(i = 0; i < g.ecount; i++) {
        enemy_t *e = &g.enemies[i];
        if(e->dead) continue;
        if(dist2(e->x, e->z, g.px, g.pz) <= 1843 * 1843) {
            e->slow = 3100 / DT_MS;
            damage_enemy(e, 34);
        }
    }
    add_fx(0, g.px, g.pz, 5 * U, 0, COL_ICE, 700);
    msg("BANG NOVA!");
}

static void skill_bolt(void)
{
    int i, best = -1, jumps;
    int32_t bd = 0;
    int32_t ox = g.px, oz = g.pz;
    int dmg = 58;

    if(g.cd_bolt > 0 || g.down_ms > 0) return;
    for(i = 0; i < g.ecount; i++) {
        enemy_t *e = &g.enemies[i];
        int32_t d;
        if(e->dead) continue;
        d = dist2(e->x, e->z, g.px, g.pz);
        if(d > 2432 * 2432) continue;
        if(best < 0 || d < bd) { best = i; bd = d; }
    }
    if(best < 0) { msg("KHONG CO MUC TIEU TRONG TAM SET"); return; }
    g.cd_bolt = 8500;
    for(jumps = 0; jumps < 4 && best >= 0; jumps++) {
        enemy_t *e = &g.enemies[best];
        int j, next = -1;
        int32_t nd = 0;

        damage_enemy(e, dmg);
        add_fx(1, ox, oz, e->x, e->z, COL_BOLT, 260);
        ox = e->x; oz = e->z;
        dmg -= dmg * 12 / 100;
        for(j = 0; j < g.ecount; j++) {
            enemy_t *o = &g.enemies[j];
            int32_t d;
            if(o->dead || j == best) continue;
            d = dist2(o->x, o->z, ox, oz);
            if(d > 1126 * 1126) continue;
            if(next < 0 || d < nd) { next = j; nd = d; }
        }
        best = next;
    }
    msg("SET DAN CHUYEN!");
}

static void skill_whirl(void)
{
    int i;

    if(g.cd_whirl > 0 || g.down_ms > 0) return;
    g.cd_whirl = 6500;
    for(i = 0; i < g.ecount; i++) {
        enemy_t *e = &g.enemies[i];
        int32_t px, pz;
        if(e->dead) continue;
        if(dist2(e->x, e->z, g.px, g.pz) > 858 * 858) continue;
        damage_enemy(e, 42);
        if(e->kind == 5) continue;
        px = e->x + (e->x - g.px) / 2;
        pz = e->z + (e->z - g.pz) / 2;
        move_toward(&e->x, &e->z, px, pz, 24);
    }
    add_fx(0, g.px, g.pz, 3 * U, 0, COL_FIRE, 520);
    msg("LOC KIEM!");
}

static void skill_gas(void)
{
    if(g.gas_ms > 0 || g.down_ms > 0) return;
    if(g.inner < 100) { msg("NOI NANG CHUA DAY (100%)"); return; }
    g.inner = 0;
    g.gas_ms = 5500;
    g.gas_tick = 0;
    msg("SI HOI DIEN RONG!");
    add_fx(2, g.px, g.pz, 3 * U, 0, COL_GAS, 5500);
}

/* ---- economy actions ----------------------------------------------------- */

static void build_tower(void)
{
    int i, best = -1;
    int32_t bd = 0;

    if(g.gold < 60) { msg("CAN 60 VANG DE XAY TRU"); return; }
    for(i = 0; i < MAX_TOWERS; i++) {
        int32_t d;
        if(g.towers[i].built || g.towers[i].building) continue;
        d = dist2(g_pad_x[i], g_pad_z[i], g.px, g.pz);
        if(best < 0 || d < bd) { best = i; bd = d; }
    }
    if(best < 0) { msg("HET O DAT XAY TRU"); return; }
    if(bd > 6 * U * 6 * U) { msg("HAY DUNG GAN O DAT TRONG"); return; }
    g.gold -= 60;
    g.towers[best].building = 1;
    g.towers[best].build_ms = 8000;
    msg("DANG XAY TRU (8s)");
}

static int hero_cost(void)
{
    return 38 + g.hcount * 8;
}

static void summon_hero(void)
{
    hero_t *h;
    int cost = hero_cost();

    if(g.hcount >= MAX_HEROES) { msg("DA DU 6 HERO"); return; }
    if(g.meat < cost) { msg("CAN NHIEU THIT HON"); return; }
    g.meat -= cost;
    h = &g.heroes[g.hcount];
    h->kind = (uint8_t)(g.hcount % 6);
    h->maxhp = 220;
    h->hp = 220;
    h->alive = 1;
    h->cd = 300;
    h->x = CORE_X + (int32_t)(rnd() % 300u) - 150;
    h->z = CORE_Z + (int32_t)(rnd() % 300u) - 150;
    g.hcount++;
    msg("TRIEU HOI HERO!");
}

static uint16_t hero_color(uint8_t kind)
{
    switch(kind) {
    case 0: return COL_GOLD;
    case 1: return C(122, 216, 255);
    case 2: return COL_ICE;
    case 3: return COL_FIRE;
    case 4: return COL_BOLT;
    default: return C(140, 240, 120);
    }
}

static int hero_damage(uint8_t kind)
{
    switch(kind) {
    case 0: return 18;
    case 1: return 25;
    case 2: return 22;
    case 3: return 29;
    case 4: return 24;
    default: return 16;
    }
}

static uint16_t hero_rate(uint8_t kind)
{
    switch(kind) {
    case 0: return 720;
    case 1: return 1050;
    case 2: return 1080;
    case 3: return 1020;
    case 4: return 920;
    default: return 660;
    }
}

/* ---- per-frame simulation ------------------------------------------------ */

static void dec(int32_t *v, int32_t amount)
{
    *v -= amount;
    if(*v < 0) *v = 0;
}

static void dec16(uint16_t *v, uint16_t amount)
{
    if(*v > amount) *v = (uint16_t)(*v - amount);
    else *v = 0;
}

static void update_enemies(void)
{
    int i;

    for(i = 0; i < g.ecount; i++) {
        enemy_t *e = &g.enemies[i];
        int32_t spd, range, tx, tz;
        int flying, gate_alive;

        if(e->dead) continue;
        if(e->flash > 0) e->flash = (uint16_t)(e->flash > DT_MS ? e->flash - DT_MS : 0);
        if(e->slow > 0) e->slow--;
        if(e->cd > 0) e->cd = (uint16_t)(e->cd > DT_MS ? e->cd - DT_MS : 0);

        spd = enemy_speed(e->kind);
        if(e->slow > 0) spd = spd > 2 ? spd - 2 : 1;
        range = enemy_range(e->kind);
        flying = (e->kind == 3 || e->kind == 4) ? 1 : 0;
        gate_alive = g.gate_hp[e->lane] > 0;

        if(g.down_ms == 0 &&
           dist2(e->x, e->z, g.px, g.pz) <= (range + 120) * (range + 120)) {
            if(e->cd == 0) {
                damage_player(enemy_dmg(e->kind));
                e->cd = enemy_cd(e->kind);
            }
            continue;
        }

        if(!flying && gate_alive) { tx = g_gate_x[e->lane]; tz = g_gate_z[e->lane]; }
        else { tx = CORE_X; tz = CORE_Z; }

        if(dist2(e->x, e->z, tx, tz) <= (range + 70) * (range + 70)) {
            if(e->cd == 0) {
                int dmg = enemy_dmg(e->kind);
                if(!flying && gate_alive) {
                    g.gate_hp[e->lane] -= dmg;
                    if(g.gate_hp[e->lane] <= 0) {
                        g.gate_hp[e->lane] = 0;
                        msg("CONG BI PHA! QUAI TRAN VAO");
                        add_fx(0, tx, tz, 3 * U, 0, COL_FIRE, 600);
                    }
                } else {
                    g.core_hp -= dmg;
                    if(g.core_hp < 0) g.core_hp = 0;
                }
                e->cd = enemy_cd(e->kind);
            }
            continue;
        }
        move_toward(&e->x, &e->z, tx, tz, spd);
    }
}

static void update_player(void)
{
    int i, best = -1;
    int32_t bd = 0;

    if(g.down_ms > 0) {
        dec(&g.down_ms, DT_MS);
        if(g.down_ms == 0) {
            g.php = g.pmaxhp;
            g.px = CORE_X + 60;
            g.pz = CORE_Z + 60;
            msg("HOI SINH TAI NHA CHINH");
        }
        return;
    }

    if(g.mvx != 0 || g.mvz != 0) {
        g.px += (g.mvx * 36) >> 8;
        g.pz += (g.mvz * 36) >> 8;
    } else if(g.tx != 0 || g.tz != 0) {
        move_toward(&g.px, &g.pz, g.tx, g.tz, 36);
        if(dist2(g.px, g.pz, g.tx, g.tz) < 600) { g.tx = 0; g.tz = 0; }
    }
    if(g.px < -9 * U) g.px = -9 * U;
    if(g.px > 9 * U) g.px = 9 * U;
    if(g.pz < -9 * U) g.pz = -9 * U;
    if(g.pz > 9 * U) g.pz = 9 * U;

    if(g.pcd > 0) { dec(&g.pcd, DT_MS); return; }
    for(i = 0; i < g.ecount; i++) {
        enemy_t *e = &g.enemies[i];
        int32_t d;
        if(e->dead) continue;
        d = dist2(e->x, e->z, g.px, g.pz);
        if(d > 1894 * 1894) continue;
        if(best < 0 || d < bd) { best = i; bd = d; }
    }
    if(best < 0) return;
    damage_enemy(&g.enemies[best], 14);
    add_fx(1, g.px, g.pz, g.enemies[best].x, g.enemies[best].z, COL_ACCENT, 130);
    g.pcd = 280;
}

static void update_towers(void)
{
    int i, j;

    for(i = 0; i < MAX_TOWERS; i++) {
        tower_t *t = &g.towers[i];
        if(t->building) {
            dec16(&t->build_ms, DT_MS);
            if(t->build_ms == 0) { t->building = 0; t->built = 1; msg("TRU HOAN THANH"); }
            continue;
        }
        if(!t->built) continue;
        if(t->cd > 0) { dec16(&t->cd, DT_MS); continue; }
        for(j = 0; j < g.ecount; j++) {
            enemy_t *e = &g.enemies[j];
            if(e->dead) continue;
            if(dist2(e->x, e->z, g_pad_x[i], g_pad_z[i]) > 1638 * 1638) continue;
            damage_enemy(e, 24);
            add_fx(1, g_pad_x[i], g_pad_z[i], e->x, e->z, COL_ACCENT, 140);
            t->cd = 800;
            break;
        }
    }
}

static void update_heroes(void)
{
    int i, j;

    for(i = 0; i < g.hcount; i++) {
        hero_t *h = &g.heroes[i];
        int best = -1;
        int32_t bd = 0;

        if(!h->alive) continue;
        if(h->cd > 0) { dec16(&h->cd, DT_MS); continue; }
        for(j = 0; j < g.ecount; j++) {
            enemy_t *e = &g.enemies[j];
            int32_t d;
            if(e->dead) continue;
            d = dist2(e->x, e->z, h->x, h->z);
            if(d > 1792 * 1792) continue;
            if(best < 0 || d < bd) { best = j; bd = d; }
        }
        if(best < 0) continue;
        damage_enemy(&g.enemies[best], hero_damage(h->kind));
        add_fx(1, h->x, h->z, g.enemies[best].x, g.enemies[best].z,
               hero_color(h->kind), 150);
        h->cd = hero_rate(h->kind);
    }
}

static void update_loot(void)
{
    int i;

    for(i = 0; i < g.lcount; i++) {
        loot_t *l = &g.loot[i];
        if(!l->alive) continue;
        if(l->life > 0) l->life--;
        if(l->life == 0) { l->alive = 0; continue; }
        if(dist2(l->x, l->z, g.px, g.pz) <= 858 * 858) {
            move_toward(&l->x, &l->z, g.px, g.pz, 64);
            if(dist2(l->x, l->z, g.px, g.pz) <= 160 * 160) {
                l->alive = 0;
                if(l->kind == 0) g.gold += 6;
                else g.meat += 4;
            }
        }
    }
}

static void tick(void)
{
    int i, alive = 0;

    if(!g.ready || g.paused) return;
    if(g.phase == 2) return;

    dec(&g.msg_ms, DT_MS);
    dec(&g.cd_ice, DT_MS);
    dec(&g.cd_bolt, DT_MS);
    dec(&g.cd_whirl, DT_MS);
    dec(&g.cd_gas, DT_MS);

    g.inner += 14;
    if(g.inner > 100) g.inner = 100;

    if(g.gas_ms > 0) {
        dec(&g.gas_ms, DT_MS);
        dec(&g.gas_tick, DT_MS);
        if(g.gas_tick == 0) {
            g.gas_tick = 220;
            for(i = 0; i < g.ecount; i++) {
                enemy_t *e = &g.enemies[i];
                if(e->dead) continue;
                if(dist2(e->x, e->z, g.px, g.pz) <= 819 * 819) {
                    e->slow = 900 / DT_MS;
                    damage_enemy(e, 10);
                }
            }
        }
    }

    update_player();
    update_enemies();
    update_towers();
    update_heroes();
    update_loot();

    for(i = 0; i < g.fcount; i++) {
        g.fx[i].t = (uint16_t)(g.fx[i].t + DT_MS);
    }
    while(g.fcount > 0 && g.fx[0].t >= g.fx[0].life) {
        for(i = 1; i < g.fcount; i++) g.fx[i - 1] = g.fx[i];
        g.fcount--;
    }

    if(g.phase == 0) {
        dec(&g.build_ms, DT_MS);
        if(g.build_ms == 0) start_wave();
    } else {
        g.wave_ms += DT_MS;
        if(g.spawn_left > 0) {
            dec(&g.spawn_timer, DT_MS);
            if(g.spawn_timer == 0) {
                uint8_t kind = pick_kind();
                uint8_t lane = (uint8_t)((g.spawn_i + g.wave) % 3);
                spawn_enemy(kind, lane);
                g.spawn_i++;
                g.spawn_left--;
                g.spawn_timer = g.spawn_gap;
            }
        }
        for(i = 0; i < g.ecount; i++) if(!g.enemies[i].dead) alive++;
        if(g.spawn_left == 0 && alive == 0) wave_complete();
    }

    if(g.core_hp <= 0) {
        g.phase = 2;
        msg("DOANH TRAI THAT THU");
        if(g_api != NULL && g_api->log_utf8 != NULL) g_api->log_utf8("Furry Guardian: camp destroyed");
    }

    g_camx = g.px;
    g_camz = g.pz;
}

/* ---- reset --------------------------------------------------------------- */

static void reset_game(void)
{
    int i;

    g_seed = 0x2545F491u;
    g.px = 40;
    g.pz = 60;
    g.tx = 0;
    g.tz = 0;
    g.mvx = 0;
    g.mvz = 0;
    g.php = 115;
    g.pmaxhp = 115;
    g.gold = 10;
    g.meat = 30;
    g.wave = 0;
    g.kills = 0;
    g.star = 0;
    g.phase = 0;
    g.build_ms = 30000;
    g.wave_ms = 0;
    g.core_max = 240;
    g.core_hp = 240;
    for(i = 0; i < 3; i++) g.gate_hp[i] = GATE_HP_MAX;
    g.down_ms = 0;
    g.inner = 0;
    g.cd_ice = 0;
    g.cd_bolt = 0;
    g.cd_whirl = 0;
    g.cd_gas = 0;
    g.pcd = 0;
    g.gas_ms = 0;
    g.gas_tick = 0;
    g.spawn_left = 0;
    g.spawn_gap = 620;
    g.spawn_timer = 0;
    g.spawn_i = 0;
    g.msg_ms = 0;
    g.paused = 0;
    g.ecount = 0;
    g.hcount = 0;
    g.lcount = 0;
    g.fcount = 0;
    for(i = 0; i < MAX_ENEMIES; i++) g.enemies[i].dead = 1;
    for(i = 0; i < MAX_LOOT; i++) g.loot[i].alive = 0;
    for(i = 0; i < MAX_HEROES; i++) g.heroes[i].alive = 0;
    for(i = 0; i < MAX_TOWERS; i++) {
        g.towers[i].built = 0;
        g.towers[i].building = 0;
        g.towers[i].build_ms = 0;
        g.towers[i].cd = 0;
    }

    g_prop_count = 0;
    for(i = 0; i < MAX_PROPS; i++) {
        prop_t *p = &g_props[g_prop_count];
        int32_t k = (int32_t)(rnd() % 16u);
        int32_t rad = 8 * U + (int32_t)(rnd() % (6 * 256u));

        p->x = (g_dir_x[k] * rad) >> 8;
        p->z = (g_dir_z[k] * rad) >> 8;
        p->kind = (uint8_t)((rnd() % 3u) == 0 ? 1 : 0);
        g_prop_count++;
    }
    g_camx = g.px;
    g_camz = g.pz;
    if(g_api != NULL && g_api->log_utf8 != NULL)
        g_api->log_utf8("Furry Guardian: camp ready");
}
/* ---- drawing ------------------------------------------------------------- */

typedef struct { int32_t d; uint8_t kind; int16_t idx; } item_t;

static item_t g_items[MAX_ITEMS];
static int g_ni;

static void push_item(int32_t depth, uint8_t kind, int16_t idx)
{
    if(g_ni >= MAX_ITEMS) return;
    g_items[g_ni].d = depth;
    g_items[g_ni].kind = kind;
    g_items[g_ni].idx = idx;
    g_ni++;
}

static void sort_items(void)
{
    int i, j;

    for(i = 1; i < g_ni; i++) {
        item_t key = g_items[i];
        j = i - 1;
        while(j >= 0 && g_items[j].d > key.d) {
            g_items[j + 1] = g_items[j];
            j--;
        }
        g_items[j + 1] = key;
    }
}

static void strip(int32_t ax, int32_t az, int32_t bx, int32_t bz,
                  int32_t halfw, uint16_t c)
{
    int32_t dx = bx - ax, dz = bz - az;
    int32_t adx = dx < 0 ? -dx : dx;
    int32_t adz = dz < 0 ? -dz : dz;
    uint32_t len = (uint32_t)isqrt32((uint32_t)(adx * adx + adz * adz));
    uint32_t fx, fz;
    int32_t px, pz;

    if(len == 0) return;
    fx = udiv32((uint32_t)adx << 16, len);
    fz = udiv32((uint32_t)adz << 16, len);
    px = (int32_t)((fz * (uint32_t)halfw) >> 16);
    pz = (int32_t)((fx * (uint32_t)halfw) >> 16);
    quad3(ax - px, az - pz, ax + px, az + pz,
          bx + px, bz + pz, bx - px, bz - pz, 2, c);
}

static void tri_pt(pt_t a, pt_t b, pt_t c, uint16_t col)
{
    tri(a.x, a.y, b.x, b.y, c.x, c.y, col);
}

static void quad3_pt(pt_t a, pt_t b, pt_t c, pt_t d, uint16_t col)
{
    tri_pt(a, b, c, col);
    tri_pt(a, c, d, col);
}

static void draw_ground(void)
{
    int i, j;

    fb_rect(0, SCENE_TOP, LW, SCENE_BOT - SCENE_TOP, COL_SKY_MID);
    fb_rect(0, SCENE_TOP, LW, 70, COL_SKY_TOP);
    fb_rect(0, SCENE_TOP + 70, LW, 60, COL_SKY_MID);
    fb_rect(0, SCENE_TOP + 130, LW, 60, COL_SKY_LOW);
    fb_rect(0, SCENE_TOP + 190, LW, SCENE_BOT - SCENE_TOP - 190, COL_SKY_LOW);

    quad3(-22 * U, -22 * U, 22 * U, -22 * U,
          22 * U, 22 * U, -22 * U, 22 * U, 0, COL_GRASS);
    for(i = -4; i < 4; i++) {
        for(j = -4; j < 4; j++) {
            int32_t x0, z0, x1, z1;
            if(((i + j) & 1) == 0) continue;
            x0 = i * 5 * U;
            z0 = j * 5 * U;
            x1 = x0 + 5 * U;
            z1 = z0 + 5 * U;
            quad3(x0, z0, x1, z0, x1, z1, x0, z1, 0, COL_GRASS2);
        }
    }
}

static void draw_paths(void)
{
    int i;

    for(i = 0; i < 3; i++)
        strip(g_gate_x[i], g_gate_z[i], g_spawn_x[i], g_spawn_z[i], 78, COL_DIRT);
    for(i = 0; i < 3; i++)
        strip(g_gate_x[i], g_gate_z[i], CORE_X, CORE_Z, 54, COL_DIRT2);
}

static void draw_camp(void)
{
    pt_t a = proj(g_gate_x[0], 1, g_gate_z[0]);
    pt_t b = proj(g_gate_x[1], 1, g_gate_z[1]);
    pt_t c = proj(g_gate_x[2], 1, g_gate_z[2]);

    tri_pt(a, b, c, COL_CAMP);
}

static void wall_seg(int32_t ax, int32_t az, int32_t bx, int32_t bz)
{
    int32_t dx = bx - ax, dz = bz - az;
    int32_t adx = dx < 0 ? -dx : dx;
    int32_t adz = dz < 0 ? -dz : dz;
    uint32_t len = (uint32_t)isqrt32((uint32_t)(adx * adx + adz * adz));
    uint32_t fx, fz;
    int32_t px, pz;
    pt_t ao0, bo0, ai1, bi1, ao1, bo1;

    if(len == 0) return;
    fx = udiv32((uint32_t)adx << 16, len);
    fz = udiv32((uint32_t)adz << 16, len);
    px = (int32_t)((fz * 26u) >> 16);
    pz = (int32_t)((fx * 26u) >> 16);

    ao0 = proj(ax + px, 0, az + pz);
    bo0 = proj(bx + px, 0, bz + pz);
    ai1 = proj(ax - px, 250, az - pz);
    bi1 = proj(bx - px, 250, bz - pz);
    ao1 = proj(ax + px, 250, az + pz);
    bo1 = proj(bx + px, 250, bz + pz);

    quad3_pt(ao0, bo0, bo1, ao1, COL_WALL_X);
    quad3_pt(ai1, bi1, bo1, ao1, COL_WALL);
}

static void draw_wall(int seg)
{
    int a = seg;
    int b = seg == 2 ? 0 : seg + 1;

    wall_seg(g_gate_x[a], g_gate_z[a], g_gate_x[b], g_gate_z[b]);
}

static void draw_gate(int lane)
{
    int32_t x = g_gate_x[lane], z = g_gate_z[lane];

    if(g.gate_hp[lane] > 0) {
        box3(x, z, 70, 0, 150, COL_GATE_TOP, COL_WALL_X, COL_GATE);
        box3(x, z, 40, 150, 60, COL_GATE_TOP, COL_GATE_TOP, COL_GATE_TOP);
    } else {
        box3(x - 40, z, 30, 0, 30, COL_WALL_X, COL_WALL_X, COL_GATE);
        box3(x + 40, z, 30, 0, 22, COL_WALL_X, COL_WALL_X, COL_GATE);
    }
}

static void draw_core(void)
{
    int32_t h = 380;
    pt_t a, b, c, d, apex;

    box3(CORE_X, CORE_Z, 300, 0, h, COL_CORE_TOP, COL_CORE, COL_CORE);
    box3(CORE_X + 230, CORE_Z + 210, 70, 0, 100, COL_ROOF, COL_ROOF, COL_ROOF);
    box3(CORE_X - 170, CORE_Z + 170, 46, 0, 70, COL_DOOR, COL_DOOR, COL_DOOR);

    a = proj(CORE_X - 320, h, CORE_Z - 320);
    b = proj(CORE_X + 320, h, CORE_Z - 320);
    c = proj(CORE_X + 320, h, CORE_Z + 320);
    d = proj(CORE_X - 320, h, CORE_Z + 320);
    apex = proj(CORE_X, h + 260, CORE_Z);
    tri_pt(a, b, apex, COL_ROOF);
    tri_pt(b, c, apex, COL_ROOF);
    tri_pt(c, d, apex, C(120, 54, 42));
    tri_pt(d, a, apex, COL_ROOF);
}

static void draw_tower(int i)
{
    int32_t x = g_pad_x[i], z = g_pad_z[i];

    ground_diamond(x, z, 90, COL_DIRT2);
    if(g.towers[i].building) {
        box3(x, z, 44, 0, 120, COL_WALL, COL_WALL_X, COL_WALL_Z);
        box3(x, z, 60, 120, 16, COL_DIM, COL_DIM, COL_DIM);
        return;
    }
    if(!g.towers[i].built) {
        box3(x, z, 30, 0, 20, COL_WALL_X, COL_WALL_X, COL_WALL_X);
        return;
    }
    box3(x, z, 78, 0, 250, COL_WALL_Z, COL_WALL_X, COL_WALL);
    box3(x, z, 96, 250, 40, COL_CORE_TOP, COL_WALL, COL_WALL_Z);
}

static void draw_prop(int i)
{
    prop_t *p = &g_props[i];

    if(p->kind == 0) {
        box3(p->x, p->z, 18, 0, 110, C(96, 68, 40), C(78, 54, 32), C(88, 62, 36));
        box3(p->x, p->z, 76, 90, 150, C(62, 122, 72), C(46, 96, 58), C(54, 110, 66));
        box3(p->x, p->z, 52, 200, 90, C(82, 150, 86), C(58, 118, 70), C(68, 132, 80));
    } else {
        box3(p->x, p->z, 50, 0, 60, C(128, 132, 128), C(96, 100, 98), C(112, 116, 110));
    }
}

static void hp_bar_world(int32_t x, int32_t z, int32_t y, int32_t hp, int32_t maxhp,
                         int width)
{
    pt_t p = proj(x, y, z);
    int fill;
    int left = p.x - width / 2;

    if(maxhp <= 0) return;
    if(hp < 0) hp = 0;
    fill = (int)udiv32((uint32_t)(width - 2) * (uint32_t)hp, (uint32_t)maxhp);
    fb_rect(left, p.y, width, 3, COL_BAR_BG);
    fb_rect(left + 1, p.y + 1, fill, 1, COL_HP);
}

static void draw_player(void)
{
    int32_t bob = (g.down_ms > 0) ? 0 : 0;

    if(g.down_ms > 0) {
        box3(g.px, g.pz, 70, 0, 40, COL_PLAYER_X, COL_PLAYER_X, COL_PLAYER_X);
        return;
    }
    box3(g.px, g.pz, 70, bob, 150, COL_PLAYER, COL_PLAYER_X, COL_PLAYER_Z);
    box3(g.px, g.pz, 44, 150, 76, COL_HEAD, COL_HEAD, C(206, 148, 96));
    box3(g.px - 42, g.pz - 16, 16, 220, 44, COL_EAR, COL_EAR, COL_EAR);
    box3(g.px + 42, g.pz - 16, 16, 220, 44, COL_EAR, COL_EAR, COL_EAR);
    /* tail, opposite the facing direction */
    box3(g.px - 70 - (g.mvx >> 2), g.pz - (g.mvz >> 2), 26, 60, 30,
         COL_PLAYER_Z, COL_PLAYER_X, COL_PLAYER_Z);

}

static void draw_enemy(int i)
{
    enemy_t *e = &g.enemies[i];
    int32_t r, h, base = 0;
    uint16_t top = e->flash > 0 ? COL_INK : COL_GOBLIN;
    uint16_t fx = COL_GOBLIN_X, fz = COL_GOBLIN_Z;

    switch(e->kind) {
    case 1: r = 86; h = 200; top = COL_BRUTE; fx = COL_BRUTE_X; fz = COL_BRUTE_X; break;
    case 2: r = 78; h = 110; top = COL_SLIME; fx = COL_SLIME_X; fz = COL_SLIME_X; break;
    case 3: r = 70; h = 130; base = 560; top = COL_FLY; fx = COL_FLY_X; fz = COL_FLY_X; break;
    case 4: r = 56; h = 100; base = 760; top = COL_FLY; fx = COL_FLY_X; fz = COL_FLY_X; break;
    case 5: r = 130; h = 260; top = COL_BOSS; fx = COL_BOSS_X; fz = COL_BOSS_X; break;
    default: r = 62; h = 120; break;
    }
    if(e->flash > 0) { fx = COL_INK; fz = COL_INK; }
    box3(e->x, e->z, r, base, h, top, fx, fz);
    if(e->kind == 5) {
        box3(e->x - 40, e->z, 18, base + h, 60, COL_ACCENT, COL_ACCENT, COL_ACCENT);
        box3(e->x + 40, e->z, 18, base + h, 60, COL_ACCENT, COL_ACCENT, COL_ACCENT);
    }
    if(e->slow > 0) ground_diamond(e->x, e->z, r + 24, COL_ICE);
    hp_bar_world(e->x, e->z, base + h + 90, e->hp, e->maxhp, 16);
}

static void draw_hero(int i)
{
    hero_t *h = &g.heroes[i];
    uint16_t c = hero_color(h->kind);

    box3(h->x, h->z, 60, 0, 130, c, c, c);
    box3(h->x, h->z, 36, 130, 50, COL_HEAD, COL_HEAD, COL_HEAD);
    hp_bar_world(h->x, h->z, 220, h->hp, h->maxhp, 12);
}

static void draw_loot(int i)
{
    loot_t *l = &g.loot[i];

    box3(l->x, l->z, 20, 20, 34, l->kind ? COL_MEAT : COL_GOLD,
         l->kind ? COL_MEAT : COL_GOLD, l->kind ? COL_MEAT : COL_GOLD);
}

static void draw_fx(void)
{
    int i, k;

    for(i = 0; i < g.fcount; i++) {
        fx_t *f = &g.fx[i];
        uint32_t life = f->life ? (uint32_t)f->life : 1u;

        if(f->kind == 0) {
            int32_t r = (int32_t)udiv32((uint32_t)f->x2 * (uint32_t)f->t, life);
            ring(f->x, f->z, r, f->col);
        } else if(f->kind == 1) {
            pt_t a = proj(f->x, 320, f->z);
            pt_t b = proj(f->x2, 40, f->z2);
            line(a.x, a.y, b.x, b.y, f->col);
        } else if(f->kind == 2) {
            for(k = 0; k < 5; k++) {
                int32_t ox = (g_dir_x[(k * 3) & 15] * (f->x2 >> 1)) >> 8;
                int32_t oz = (g_dir_z[(k * 3) & 15] * (f->x2 >> 1)) >> 8;
                quad3(f->x + ox - 70, f->z + oz - 70, f->x + ox + 70, f->z + oz - 70,
                      f->x + ox + 70, f->z + oz + 70, f->x + ox - 70, f->z + oz + 70,
                      90, COL_GAS);
            }
        } else {
            int32_t r = (int32_t)udiv32((uint32_t)f->x2 * (uint32_t)f->t, life);
            ring(f->x, f->z, r, f->col);
        }
    }
}

static void draw_item(const item_t *it)
{
    switch(it->kind) {
    case 1: draw_prop(it->idx); break;
    case 2: draw_wall(it->idx); break;
    case 3: draw_gate(it->idx); break;
    case 4: draw_tower(it->idx); break;
    case 5: draw_core(); break;
    case 6: draw_player(); break;
    case 7: draw_enemy(it->idx); break;
    case 8: draw_hero(it->idx); break;
    default: draw_loot(it->idx); break;
    }
}

/* ---- HUD ----------------------------------------------------------------- */

#define BTN_Y 402
#define BTN_W 35
#define BTN_H 42
#define BTN_X(i) (4 + (i) * 39)

static void bar(int x, int y, int w, int h, int32_t v, int32_t maxv, uint16_t c)
{
    int fill;

    fb_rect(x, y, w, h, COL_BAR_BG);
    if(maxv <= 0 || v <= 0) return;
    if(v > maxv) v = maxv;
    fill = (int)udiv32((uint32_t)w * (uint32_t)v, (uint32_t)maxv);
    fb_rect(x, y, fill, h, c);
}

static const char *btn_key(int i)
{
    switch(i) {
    case 0: return "Q";
    case 1: return "E";
    case 2: return "SP";
    case 3: return "R";
    case 4: return "F";
    default: return "G";
    }
}

static const char *btn_name(int i)
{
    switch(i) {
    case 0: return "BANG";
    case 1: return "SET";
    case 2: return "LOC";
    case 3: return "HOI";
    case 4: return "TRU";
    default: return "HERO";
    }
}

static void draw_buttons(void)
{
    int i;

    for(i = 0; i < 6; i++) {
        int x = BTN_X(i), y = BTN_Y, w = BTN_W, h = BTN_H;
        int32_t cd = 0, cdmax = 1;
        int ready = 1;
        char sub[16];
        char *p = sub;
        const char *key = btn_key(i);

        switch(i) {
        case 0: cd = g.cd_ice; cdmax = 7200; break;
        case 1: cd = g.cd_bolt; cdmax = 8500; break;
        case 2: cd = g.cd_whirl; cdmax = 6500; break;
        case 3: cd = g.cd_gas; cdmax = 9500; break;
        case 4:
            if(g.gold < 60) ready = 0;
            p = append_uint(p, 60);
            *p = '\0';
            break;
        default:
            if(g.meat < hero_cost()) ready = 0;
            p = append_uint(p, hero_cost());
            *p = '\0';
            break;
        }
        if(cd > 0) ready = 0;
        fb_rect(x, y, w, h, ready ? COL_PANEL2 : COL_PANEL);
        fb_rect(x, y, w, 1, ready ? COL_ACCENT : COL_DIM);
        fb_rect(x, y + h - 1, w, 1, COL_PANEL);
        fb_rect(x, y, 1, h, COL_PANEL);
        fb_rect(x + w - 1, y, 1, h, COL_PANEL);

        if(cd > 0) {
            int fill = (int)udiv32((uint32_t)(h - 2) * (uint32_t)cd, (uint32_t)cdmax);
            fb_rect(x + 1, y + h - 1 - fill, w - 2, fill, COL_PANEL);
        }
        text(x + (w - text_w(key, 1)) / 2, y + 3, key, 1,
             ready ? COL_ACCENT : COL_DIM);
        text(x + (w - text_w(btn_name(i), 1)) / 2, y + 14, btn_name(i), 1, COL_INK);
        if(i < 4) {
            if(cd > 0) {
                p = append_uint(p, cd / 1000);
                p = append_str(p, "s");
                *p = '\0';
            } else {
                p = append_str(p, "OK");
                *p = '\0';
            }
        }
        text(x + (w - text_w(sub, 1)) / 2, y + 26, sub, 1,
             ready ? COL_GOLD : COL_DIM);
    }
}

static void center_text(int y, const char *s, int scale, uint16_t c)
{
    text((LW - text_w(s, scale)) / 2, y, s, scale, c);
}

static void draw_hud(void)
{
    char buf[64];
    char *p;
    int gates;

    fb_rect(0, 0, LW, SCENE_TOP, COL_PANEL);
    fb_rect(0, SCENE_TOP - 2, LW, 2, COL_ACCENT);

    text(5, 4, "FURRY GUARDIAN", 1, COL_ACCENT);

    text(5, 20, "DOT", 1, COL_DIM);
    p = buf;
    p = append_uint(p, g.wave);
    *p = '\0';
    text(30, 16, buf, 2, COL_INK);

    if(g.phase == 0) {
        p = buf;
        p = append_str(p, "XAY DUNG ");
        p = append_uint(p, g.build_ms / 1000);
        p = append_str(p, "s");
        *p = '\0';
    } else if(g.phase == 1) {
        p = buf;
        p = append_str(p, "CHIEN DAU");
        *p = '\0';
    } else {
        p = buf;
        p = append_str(p, "THAT THU");
        *p = '\0';
    }
    text(150, 4, buf, 1, g.phase == 1 ? COL_FIRE : COL_INK);

    p = buf;
    p = append_str(p, "VANG ");
    p = append_uint(p, g.gold);
    *p = '\0';
    text(62, 18, buf, 1, COL_GOLD);

    p = buf;
    p = append_str(p, "THIT ");
    p = append_uint(p, g.meat);
    *p = '\0';
    text(128, 18, buf, 1, COL_MEAT);

    p = buf;
    p = append_str(p, "DIET ");
    p = append_uint(p, g.kills);
    *p = '\0';
    text(188, 18, buf, 1, COL_DIM);

    text(4, 33, "MAU", 1, COL_DIM);
    bar(28, 33, 60, 6, g.php, g.pmaxhp, g.php * 3 < g.pmaxhp ? COL_HP_LOW : COL_HP);

    text(94, 33, "CONG", 1, COL_DIM);
    gates = g.gate_hp[0] + g.gate_hp[1] + g.gate_hp[2];
    bar(124, 33, 50, 6, gates, GATE_HP_MAX * 3, COL_GATEBAR);

    text(180, 33, "NHA", 1, COL_DIM);
    bar(206, 33, 30, 6, g.core_hp, g.core_max, COL_COREBAR);

    text(4, 42, "SAO", 1, COL_ACCENT);
    p = buf;
    p = append_uint(p, g.star);
    *p = '\0';
    text(32, 42, buf, 1, COL_INK);

    p = buf;
    p = append_str(p, "HERO ");
    p = append_uint(p, g.hcount);
    p = append_str(p, "/6  NOI ");
    p = append_uint(p, g.inner);
    p = append_str(p, "%");
    *p = '\0';
    text(56, 42, buf, 1, COL_DIM);

    fb_rect(0, SCENE_BOT, LW, LH - SCENE_BOT, COL_PANEL);
    fb_rect(0, SCENE_BOT, LW, 2, COL_ACCENT);
    draw_buttons();

    text(5, 450, "WASD/MUI TEN: DI CHUYEN", 1, COL_DIM);
    text(5, 460, "Q BANG  E SET  SPACE LOC  R HOI", 1, COL_DIM);
    text(5, 470, "ENTER: BAT DAU DOT  F TRU  G HERO", 1, COL_DIM);

    if(g.msg_ms > 0) text(5, 484, g_msg, 1, COL_ACCENT);

    if(g.phase == 2) {
        fb_rect(24, 150, 192, 110, COL_PANEL);
        fb_rect(24, 150, 192, 2, COL_ACCENT);
        fb_rect(24, 258, 192, 2, COL_ACCENT);
        center_text(168, "DOANH TRAI THAT THU", 2, COL_COREBAR);
        p = buf;
        p = append_str(p, "DOT ");
        p = append_uint(p, g.wave);
        p = append_str(p, "  DIET ");
        p = append_uint(p, g.kills);
        *p = '\0';
        center_text(200, buf, 1, COL_INK);
        p = buf;
        p = append_str(p, "SAO ");
        p = append_uint(p, g.star);
        *p = '\0';
        center_text(214, buf, 1, COL_INK);
        center_text(236, "ENTER DE CHOI LAI", 1, COL_ACCENT);
    } else if(g.paused) {
        fb_rect(60, 190, 120, 40, COL_PANEL);
        center_text(202, "TAM DUNG", 2, COL_ACCENT);
    }
}

static void draw_frame(void)
{
    int i;

    g_ni = 0;
    draw_ground();
    draw_paths();
    draw_camp();

    for(i = 0; i < g_prop_count; i++)
        push_item(g_props[i].x + g_props[i].z, 1, (int16_t)i);
    for(i = 0; i < 3; i++)
        push_item((g_gate_x[i] + g_gate_z[i]) / 2 - 40, 2, (int16_t)i);
    for(i = 0; i < 3; i++)
        push_item(g_gate_x[i] + g_gate_z[i], 3, (int16_t)i);
    push_item(CORE_X + CORE_Z, 5, 0);
    for(i = 0; i < MAX_TOWERS; i++)
        push_item(g_pad_x[i] + g_pad_z[i], 4, (int16_t)i);
    push_item(g.px + g.pz, 6, 0);
    for(i = 0; i < g.ecount; i++) {
        if(g.enemies[i].dead) continue;
        push_item(g.enemies[i].x + g.enemies[i].z, 7, (int16_t)i);
    }
    for(i = 0; i < g.hcount; i++) {
        if(!g.heroes[i].alive) continue;
        push_item(g.heroes[i].x + g.heroes[i].z, 8, (int16_t)i);
    }
    for(i = 0; i < g.lcount; i++) {
        if(!g.loot[i].alive) continue;
        push_item(g.loot[i].x + g.loot[i].z, 9, (int16_t)i);
    }
    sort_items();
    for(i = 0; i < g_ni; i++) draw_item(&g_items[i]);
    draw_fx();
    draw_hud();
    present();
}

/* ---- lifecycle ----------------------------------------------------------- */

void *create(const qeaf_app_api_v5 *api)
{
    if(api == NULL) return NULL;
    if(api->abi_version < VQEAF_APP_API_V5_VERSION) return NULL;
    if(api->blit_pixels == NULL || api->draw_text == NULL) return NULL;
    g_api = api;
    g.ready = 0;
    return &g;
}

int start(void *handle)
{
    (void)handle;
    if(g_api == NULL) return -1;
    reset_game();
    g.ready = 1;
    draw_frame();
    return 0;
}

int pause(void *handle)
{
    (void)handle;
    g.paused = 1;
    return 0;
}

int resume(void *handle)
{
    (void)handle;
    g.paused = 0;
    if(g.ready) draw_frame();
    return 0;
}

int stop(void *handle)
{
    (void)handle;
    g.ready = 0;
    return 0;
}

void destroy(void *handle)
{
    (void)handle;
    g_api = NULL;
    g.ready = 0;
}

/* ---- input --------------------------------------------------------------- */

static void step(int32_t dx, int32_t dz)
{
    g.mvx = dx;
    g.mvz = dz;
    tick();
    g.mvx = 0;
    g.mvz = 0;
    draw_frame();
}

static void do_action(int which)
{
    switch(which) {
    case 0: skill_ice(); break;
    case 1: skill_bolt(); break;
    case 2: skill_whirl(); break;
    case 3: skill_gas(); break;
    case 4: build_tower(); break;
    case 5: summon_hero(); break;
    default: break;
    }
    tick();
    draw_frame();
}

static void touch_at(int cx, int cy)
{
    int x = cx / SC, y = cy / SC;

    if(y >= BTN_Y && y < BTN_Y + BTN_H) {
        int idx = (x - 4) / 39;
        if(idx >= 0 && idx < 6 && x >= BTN_X(idx) && x < BTN_X(idx) + BTN_W) {
            if(g.phase == 2) return;
            do_action(idx);
            return;
        }
    }
    if(y >= SCENE_TOP && y < SCENE_BOT) {
        int32_t a, b;
        int i;

        if(g.phase == 2) return;
        a = (int32_t)((x - LW / 2) * 256) / PX;
        b = (int32_t)((y - SCENE_MID) * 256) / PY;
        g.tx = g_camx + (a + b) / 2;
        g.tz = g_camz + (b - a) / 2;
        if(g.tx < -9 * U) g.tx = -9 * U;
        if(g.tx > 9 * U) g.tx = 9 * U;
        if(g.tz < -9 * U) g.tz = -9 * U;
        if(g.tz > 9 * U) g.tz = 9 * U;
        for(i = 0; i < 8; i++) tick();
        draw_frame();
    }
}

void on_input(void *handle, const qeaf_input_event_t *event)
{
    int code;

    (void)handle;
    if(event == NULL || !g.ready) return;
    if(!event->pressed) return;

    if(g.phase == 2) {
        if(event->type == QEAF_INPUT_KEY &&
           (event->code == 28 || event->code == 57)) {
            reset_game();
            draw_frame();
        }
        return;
    }

    if(event->type == QEAF_INPUT_TOUCH) {
        touch_at((int)event->x, (int)event->y);
        return;
    }
    if(event->type != QEAF_INPUT_KEY) return;

    code = (int)event->code;
    switch(code) {
    case 103: case 119: step(-181, -181); return;   /* up   */
    case 108: case 115: step(181, 181); return;     /* down */
    case 105: case 97:  step(-181, 181); return;    /* left */
    case 106: case 100: step(181, -181); return;    /* right */
    case 16: do_action(0); return;                  /* Q */
    case 18: do_action(1); return;                  /* E */
    case 57: do_action(2); return;                  /* Space */
    case 19: do_action(3); return;                  /* R */
    case 33: do_action(4); return;                  /* F */
    case 34: do_action(5); return;                  /* G */
    case 28:                                        /* Enter */
        if(g.phase == 0) start_wave();
        tick();
        draw_frame();
        return;
    case 1:                                         /* Esc */
        g.paused = !g.paused;
        draw_frame();
        return;
    default:
        tick();
        draw_frame();
        return;
    }
}
