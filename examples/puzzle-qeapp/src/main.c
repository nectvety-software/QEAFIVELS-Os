/* main.c - sample QEAPP: "Xếp Hình 4x4" (15-puzzle).
 *
 * A deliberately small, complete game that uses only the framework table:
 *   - blit_pixels    : fills the background and the board tiles (RGB565),
 *   - draw_text_weighted : title, move counter and hints (ABI v4 weight),
 *   - log_utf8       : create/start/win markers the simulator prints.
 *
 * Game rules: the 4x4 board has 15 numbered tiles and one empty cell. A move
 * slides the tile next to the empty cell into it. Solve 1..15 row by row.
 *
 * Input: on_input() receives the host/adapter event. Arrow keys (Linux key
 * codes 103/108/105/106), W/A/S/D (119/115/97/100) and a touch on a tile next
 * to the empty cell are all accepted. The move direction is the direction the
 * tile travels: "left" slides the tile that sits to the right of the empty
 * cell one step left.
 *
 * Shape that the arm64/x86_64 QEN loader requires (learned in
 * examples/terminal-qeapp): file-static state only, no static const arrays,
 * aligned buffers filled through a volatile pointer, no libc and no '/'
 * or '%' on runtime values (qenlink links no division helper).
 *
 * Honest scope: this is a UI/state sample. It scores nothing, keeps no
 * records and has no persistence - a QEAPP cannot write storage today.
 */
#include <stddef.h>
#include <stdint.h>

#include "qeaf_api.h"
#include "qeaf_native_abi.h"

#define CANVAS_W 720
#define CANVAS_H 1560
#define BAND_ROWS 8

#define BOARD_N  4
#define CELL     150
#define GAP      8
#define BOARD_W  (BOARD_N * CELL + (BOARD_N - 1) * GAP)   /* 624 */
#define BOARD_X  ((CANVAS_W - BOARD_W) / 2)               /*   48 */
#define BOARD_Y  420
#define BOARD_H  BOARD_W

/* Palette in RGB565, same family as the shell theme. */
#define COL_BG      0x08C3u   /* 0x0C191B surface */
#define COL_BG_DEEP 0x0081u   /* 0x06100F */
#define COL_TILE_LO 0x1A8Fu   /* teal    */
#define COL_TILE_HI 0x2D7Fu   /* lighter teal */
#define COL_EMPTY   0x1125u   /* surface_2 */
#define COL_ACCENT  0x1F19u   /* 0x1EE0C8 */
#define COL_INK     0x0861u   /* dark ink on tiles */
#define COL_TEXT    0xEF7Du   /* near white */

#define KEY_UP    103
#define KEY_DOWN  108
#define KEY_LEFT  105
#define KEY_RIGHT 106
#define KEY_W     119
#define KEY_S     115
#define KEY_A     97
#define KEY_D     100

static const qeaf_app_api_v4 *g_api;
static uint16_t g_band[CANVAS_W * BAND_ROWS] __attribute__((aligned(16)));
static char g_buf[80] __attribute__((aligned(16)));

static struct {
    uint8_t tiles[BOARD_N * BOARD_N];   /* 1..15, 0 = empty cell */
    uint8_t empty;                      /* index of the empty cell */
    uint32_t moves;
    uint32_t rng;
    int solved;
    int ready;
} g;

/* ---- pixels ------------------------------------------------------------- */

static void fill_rect(int x, int y, int w, int h, uint16_t colour)
{
    volatile uint16_t *band = g_band;
    int i, row, rows;

    if(g_api == NULL || g_api->blit_pixels == NULL) return;
    if(x < 0) { w += x; x = 0; }
    if(y < 0) { h += y; y = 0; }
    if(x + w > CANVAS_W) w = CANVAS_W - x;
    if(y + h > CANVAS_H) h = CANVAS_H - y;
    if(w <= 0 || h <= 0) return;

    for(i = 0; i < CANVAS_W * BAND_ROWS; i++) band[i] = colour;
    for(row = 0; row < h; row += BAND_ROWS) {
        rows = h - row;
        if(rows > BAND_ROWS) rows = BAND_ROWS;
        g_api->blit_pixels(x, y + row, w, rows, CANVAS_W, g_band);
    }
}

static void text_at(int x, int y, const char *text, uint32_t weight)
{
    if(g_api == NULL || text == NULL) return;
    if(g_api->draw_text_weighted != NULL)
        g_api->draw_text_weighted(x, y, text, weight);
    else
        g_api->draw_text(x, y, text);
}

/* Unsigned decimal without '/' or '%': a power-of-ten scale is built by
 * repeated multiplication and the digit is counted by repeated subtraction.
 * The module links no division helper, so this cannot use value / 10. */
static char *append_uint(char *out, uint32_t value)
{
    int pos, started = 0;

    if(value == 0) {
        *out++ = '0';
        return out;
    }
    if(value > 999999u) value = 999999u;
    for(pos = 6; pos >= 0; pos--) {
        uint32_t scale = 1u;
        int digit = 0, k;

        for(k = 0; k < pos; k++) scale *= 10u;
        while(value >= scale && digit < 9) { value -= scale; digit++; }
        if(digit > 0 || started) {
            *out++ = (char)('0' + digit);
            started = 1;
        }
    }
    return out;
}

/* ---- seven-segment digits ---------------------------------------------- */

/* Segment mask bits: a=1 b=2 c=4 d=8 e=16 f=32 g=64. */
static uint8_t digit_mask(int digit)
{
    switch(digit) {
    case 0: return 1 | 2 | 4 | 8 | 16 | 32;
    case 1: return 2 | 4;
    case 2: return 1 | 2 | 64 | 8 | 16;
    case 3: return 1 | 2 | 64 | 4 | 8;
    case 4: return 32 | 64 | 2 | 4;
    case 5: return 1 | 32 | 64 | 4 | 8;
    case 6: return 1 | 32 | 64 | 16 | 4 | 8;
    case 7: return 1 | 2 | 4;
    case 8: return 1 | 2 | 4 | 8 | 16 | 32 | 64;
    case 9: return 1 | 2 | 4 | 8 | 32 | 64;
    default: return 0;
    }
}

static void seg_rect(int x, int y, int w, int h, int on, uint16_t colour)
{
    if(on) fill_rect(x, y, w, h, colour);
}

static void draw_digit(int x, int y, int w, int h, int digit, uint16_t colour)
{
    int t = w / 5;
    int half = h / 2;
    uint8_t m;

    if(t < 3) t = 3;
    m = digit_mask(digit);
    seg_rect(x + t, y, w - 2 * t, t, m & 1, colour);                    /* a */
    seg_rect(x + w - t, y + t, t, half - t, (m & 2) != 0, colour);      /* b */
    seg_rect(x + w - t, y + half, t, half - t, (m & 4) != 0, colour);   /* c */
    seg_rect(x + t, y + h - t, w - 2 * t, t, (m & 8) != 0, colour);     /* d */
    seg_rect(x, y + half, t, half - t, (m & 16) != 0, colour);          /* e */
    seg_rect(x, y + t, t, half - t, (m & 32) != 0, colour);             /* f */
    seg_rect(x + t, y + half - t / 2, w - 2 * t, t, (m & 64) != 0, colour); /* g */
}

static void draw_number(int x, int y, int w, int h, int value, uint16_t colour)
{
    int tens = (value >= 10) ? 1 : 0;
    int units = value - tens * 10;

    if(value >= 20 || value < 0) return;
    if(tens > 0) {
        draw_digit(x, y, w * 2 / 5, h, tens, colour);
        draw_digit(x + w * 3 / 5, y, w * 2 / 5, h, units, colour);
    }
    else {
        draw_digit(x + w / 5, y, w * 3 / 5, h, units, colour);
    }
}

/* ---- board -------------------------------------------------------------- */

static int is_solved(void)
{
    int i;

    for(i = 0; i < BOARD_N * BOARD_N - 1; i++)
        if(g.tiles[i] != (uint8_t)(i + 1)) return 0;
    return g.tiles[BOARD_N * BOARD_N - 1] == 0;
}

static uint8_t rng_next(void)
{
    g.rng = g.rng * 1664525u + 1013904223u;
    return (uint8_t)((g.rng >> 16) & 3u);
}

/* dir: 0 up (tile moves up), 1 down, 2 left, 3 right. Returns 1 if a tile
 * actually moved. The tile must be the neighbour the empty cell points at. */
static int move_dir(int dir)
{
    int dr = 0, dc = 0, row, col, src, tmp;

    if(dir == 0) dr = -1;
    else if(dir == 1) dr = 1;
    else if(dir == 2) dc = -1;
    else if(dir == 3) dc = 1;
    else return 0;

    /* `dir` is the direction the tile travels, so the moving tile sits on the
     * opposite side of the empty cell: a tile moving right comes from empty-1. */
    row = (int)(g.empty / BOARD_N) - dr;
    col = (int)(g.empty % BOARD_N) - dc;
    if(row < 0 || row >= BOARD_N || col < 0 || col >= BOARD_N) return 0;

    src = row * BOARD_N + col;
    tmp = g.tiles[g.empty];
    g.tiles[g.empty] = g.tiles[src];
    g.tiles[src] = (uint8_t)tmp;
    g.empty = (uint8_t)src;
    g.moves++;
    g.solved = is_solved();
    return 1;
}

static void scramble(void)
{
    int i;
    int last = -1;

    g.rng = 0x2545F491u;
    for(i = 0; i < 80; i++) {
        int dir = (int)rng_next();
        int opposite = dir ^ 1;   /* 0<->1, 2<->3 */

        if(dir == last) continue;
        if(!move_dir(dir)) continue;
        last = opposite;
    }
    if(is_solved()) (void)move_dir(0);
    g.moves = 0;
    g.solved = is_solved();
}

static void draw_screen(void)
{
    int row, col;
    unsigned i;
    char *p;

    /* Background: a deep band at the top, then the surface colour. */
    fill_rect(0, 0, CANVAS_W, 200, COL_BG_DEEP);
    fill_rect(0, 200, CANVAS_W, CANVAS_H - 200, COL_BG);
    fill_rect(0, 196, CANVAS_W, 4, COL_ACCENT);

    text_at(BOARD_X, 70, "XEP HINH 4x4", QEAF_TEXT_WEIGHT_BOLD);
    text_at(BOARD_X, 120, "Truot o canh o trong. Mui ten hoac WASD.",
            QEAF_TEXT_WEIGHT_REGULAR);

    /* Board frame. */
    fill_rect(BOARD_X - 12, BOARD_Y - 12, BOARD_W + 24, BOARD_H + 24, COL_EMPTY);

    for(i = 0; i < (unsigned)(BOARD_N * BOARD_N); i++) {
        int value = (int)g.tiles[i];
        int x, y;

        row = (int)(i / (unsigned)BOARD_N);
        col = (int)(i % (unsigned)BOARD_N);
        x = BOARD_X + col * (CELL + GAP);
        y = BOARD_Y + row * (CELL + GAP);

        if(value == 0) {
            fill_rect(x, y, CELL, CELL, COL_BG_DEEP);
            continue;
        }
        fill_rect(x, y, CELL, CELL,
                  ((value & 1) != 0) ? COL_TILE_LO : COL_TILE_HI);
        draw_number(x + 28, y + 42, CELL - 56, CELL - 84, value, COL_INK);
    }

    /* Footer: move counter from the real state, never a hardcoded number. */
    fill_rect(BOARD_X, BOARD_Y + BOARD_H + 48, BOARD_W, 2, COL_ACCENT);
    p = append_uint(g_buf, g.moves);
    *p = '\0';
    text_at(BOARD_X, BOARD_Y + BOARD_H + 70, "So buoc:", QEAF_TEXT_WEIGHT_BOLD);
    text_at(BOARD_X + 120, BOARD_Y + BOARD_H + 70, g_buf, QEAF_TEXT_WEIGHT_BOLD);

    if(g.solved)
        text_at(BOARD_X, BOARD_Y + BOARD_H + 130, "DA XONG! Bam phim bat ky de xao lai.",
                QEAF_TEXT_WEIGHT_BOLD);
    else
        text_at(BOARD_X, BOARD_Y + BOARD_H + 130, "Sap xep 1..15 tu trai sang phai.",
                QEAF_TEXT_WEIGHT_REGULAR);

    if(g_api != NULL && g_api->log_utf8 != NULL && g.solved)
        g_api->log_utf8("Xep Hinh: solved");
}

/* ---- lifecycle ---------------------------------------------------------- */

void *create(const qeaf_app_api_v4 *api)
{
    if(api == NULL) return NULL;
    if(api->abi_version < VQEAF_APP_API_V4_VERSION) return NULL;
    if(api->blit_pixels == NULL || api->draw_text == NULL) return NULL;
    g_api = api;
    g.ready = 0;
    return &g;
}

int start(void *handle)
{
    int i;

    (void)handle;
    if(g_api == NULL) return -1;
    for(i = 0; i < BOARD_N * BOARD_N; i++) g.tiles[i] = (uint8_t)(i + 1);
    g.tiles[BOARD_N * BOARD_N - 1] = 0;
    g.empty = (uint8_t)(BOARD_N * BOARD_N - 1);
    g.moves = 0;
    g.solved = 1;
    scramble();
    g.ready = 1;
    draw_screen();
    if(g_api->log_utf8 != NULL) g_api->log_utf8("Xep Hinh: board ready");
    return 0;
}

int pause(void *handle)
{
    (void)handle;
    if(g_api != NULL && g_api->log_utf8 != NULL) g_api->log_utf8("Xep Hinh: pause");
    return 0;
}

int resume(void *handle)
{
    (void)handle;
    if(g_api != NULL && g_api->log_utf8 != NULL) g_api->log_utf8("Xep Hinh: resume");
    return 0;
}

int stop(void *handle)
{
    (void)handle;
    g.ready = 0;
    if(g_api != NULL && g_api->log_utf8 != NULL) g_api->log_utf8("Xep Hinh: stop");
    return 0;
}

void destroy(void *handle)
{
    (void)handle;
    g_api = NULL;
    g.ready = 0;
}

/* ---- input -------------------------------------------------------------- */

static void tile_tap(int x, int y)
{
    int row, col, index;

    if(x < BOARD_X || y < BOARD_Y) return;
    if(x >= BOARD_X + BOARD_W || y >= BOARD_Y + BOARD_H) return;
    col = (x - BOARD_X) / (CELL + GAP);
    row = (y - BOARD_Y) / (CELL + GAP);
    if(row < 0 || row >= BOARD_N || col < 0 || col >= BOARD_N) return;
    index = row * BOARD_N + col;

    /* A tap slides that tile when it is a neighbour of the empty cell. */
    if(index == (int)g.empty - 1 && (int)g.empty % BOARD_N != 0) (void)move_dir(3);
    else if(index == (int)g.empty + 1 && (int)g.empty % BOARD_N != BOARD_N - 1) (void)move_dir(2);
    else if(index == (int)g.empty - BOARD_N) (void)move_dir(1);
    else if(index == (int)g.empty + BOARD_N) (void)move_dir(0);
}

void on_input(void *handle, const qeaf_input_event_t *event)
{
    int dir = -1;

    (void)handle;
    if(event == NULL || !g.ready) return;
    if(!event->pressed) return;

    if(g.solved) {
        /* Any accepted input starts a new board after a win. */
        g.solved = 0;
        scramble();
        draw_screen();
        return;
    }

    if(event->type == QEAF_INPUT_KEY) {
        switch((int)event->code) {
        case KEY_UP: case KEY_W: dir = 0; break;
        case KEY_DOWN: case KEY_S: dir = 1; break;
        case KEY_LEFT: case KEY_A: dir = 2; break;
        case KEY_RIGHT: case KEY_D: dir = 3; break;
        default: return;
        }
        if(move_dir(dir)) draw_screen();
    }
    else if(event->type == QEAF_INPUT_TOUCH) {
        tile_tap(event->x, event->y);
        draw_screen();
    }
}
