/* main.c - sample QEAPP: the four regions of a phone layout.
 *
 * The app canvas is 720x1560 (the shell composites it 1:1). This sample draws
 * the same structure a phone UI has, so the regions can be seen and measured:
 *
 *   status bar   y    0..55    solid, no content: the shell's clock/battery
 *                              overlay this band when it is uniform
 *   title bar    y   56..195   app name and subtitle
 *   main         y  196..1371  content
 *   nav bar      y 1372..1559  tab labels
 *
 * Freestanding PIC code, framework API v4 through the table only: `blit_pixels`
 * for the filled bars (RGB565) and `draw_text_weighted` for the labels. No
 * libc, no syscall, no hardware.
 *
 * Shape learned on hardware (examples/terminal-qeapp/README.md): all state is
 * file-static and no static const arrays, because qenlink resolves only
 * PC-relative references; buffers are `aligned(16)` and filled through a
 * volatile pointer, because the arm64 gate runs with the MMU off and an
 * unaligned 8-byte store faults.
 */
#include <stddef.h>
#include <stdint.h>

#include "qeaf_api.h"
#include "qeaf_native_abi.h"

#define CANVAS_W 720
#define CANVAS_H 1560

#define STATUS_H 56
#define TITLE_H  140
#define NAV_H    188

#define STATUS_Y 0
#define TITLE_Y  STATUS_H                /* 56   */
#define MAIN_Y   (TITLE_Y + TITLE_H)     /* 196  */
#define NAV_Y    (CANVAS_H - NAV_H)      /* 1372 */
#define MAIN_H   (NAV_Y - MAIN_Y)        /* 1176 */

/* RGB565 of the shell's dark palette (vqeaf_theme.c): bg 0x06100F,
 * surface 0x0C191B, surface_2 0x112629, line 0x1D3A3D, accent 0x1EE0C8. */
#define COL_STATUS 0x0081u
#define COL_TITLE  0x1125u
#define COL_MAIN   0x08C3u
#define COL_NAV    0x0081u
#define COL_LINE   0x19C7u
#define COL_PANEL  0x1125u
#define COL_ACCENT 0x1F19u

#define BAND_ROWS 8

static const qeaf_app_api_v4 *g_api;
static int g_v4;
static uint16_t g_band[CANVAS_W * BAND_ROWS] __attribute__((aligned(16)));
static char g_buf[96] __attribute__((aligned(16)));
static char g_digits[12] __attribute__((aligned(16)));
static int g_state;

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

/* ---- text --------------------------------------------------------------- */

static void text_at(int x, int y, const char *text, uint32_t weight)
{
    if(g_api == NULL || text == NULL) return;
    if(g_v4 && g_api->draw_text_weighted != NULL)
        g_api->draw_text_weighted(x, y, text, weight);
    else
        g_api->draw_text(x, y, text);
}

/* Decimal without '/' or '%': qenlink links no division helper. */
static char *append_dec(char *out, uint32_t value)
{
    int n = 0, i;
    uint32_t digit, rest;

    if(value == 0) {
        *out++ = '0';
        return out;
    }
    while(value > 0 && n < (int)sizeof(g_digits)) {
        digit = 0;
        rest = value;
        while(rest >= 10) {
            rest -= 10;
            digit++;
        }
        g_digits[n++] = (char)('0' + rest);
        value = digit;
    }
    for(i = n - 1; i >= 0; i--) *out++ = g_digits[i];
    return out;
}

static char *append_str(char *out, const char *text)
{
    if(text == NULL) return out;
    while(*text != '\0') *out++ = *text++;
    return out;
}

/* One "region" line in the main area: index, name, range, note. */
static void region_line(int y, const char *index, const char *name,
                        const char *from, const char *to, const char *note)
{
    char *out = g_buf;
    out = append_str(out, index);
    out = append_str(out, name);
    out = append_str(out, "y ");
    out = append_str(out, from);
    out = append_str(out, "..");
    out = append_str(out, to);
    out = append_str(out, "   ");
    out = append_str(out, note);
    *out = '\0';
    text_at(24, y, g_buf, QEAF_TEXT_WEIGHT_REGULAR);
}

/* ---- screen ------------------------------------------------------------- */

static void draw_screen(void)
{
    char *out;

    /* The four regions, top to bottom. */
    fill_rect(0, STATUS_Y, CANVAS_W, STATUS_H, COL_STATUS);
    fill_rect(0, TITLE_Y, CANVAS_W, TITLE_H, COL_TITLE);
    fill_rect(0, MAIN_Y, CANVAS_W, MAIN_H, COL_MAIN);
    fill_rect(0, NAV_Y, CANVAS_W, NAV_H, COL_NAV);

    /* Separators, so each bar has a visible edge on the panel. */
    fill_rect(0, TITLE_Y, CANVAS_W, 3, COL_ACCENT);
    fill_rect(0, MAIN_Y, CANVAS_W, 2, COL_LINE);
    fill_rect(0, NAV_Y, CANVAS_W, 2, COL_LINE);
    /* Accent underline under the title, like the shell's own headers. */
    fill_rect(24, TITLE_Y + 108, 156, 4, COL_ACCENT);

    /* Title bar. */
    text_at(24, TITLE_Y + 34, "SHELL UI SAMPLE", QEAF_TEXT_WEIGHT_BOLD);
    text_at(24, TITLE_Y + 78, "status / title / main / nav",
            QEAF_TEXT_WEIGHT_REGULAR);

    /* Main. */
    text_at(24, MAIN_Y + 34, "MAIN", QEAF_TEXT_WEIGHT_BOLD);
    fill_rect(24, MAIN_Y + 60, 160, 2, COL_LINE);

    region_line(MAIN_Y + 92, "1  ", "status bar   ", "0", "55",
                "solid band: the shell clock sits here");
    region_line(MAIN_Y + 126, "2  ", "title bar    ", "56", "195",
                "app name and subtitle");
    region_line(MAIN_Y + 160, "3  ", "main         ", "196", "1371",
                "content");
    region_line(MAIN_Y + 194, "4  ", "nav bar      ", "1372", "1559",
                "tab labels");

    /* A panel with values measured through the API table, not hardcoded. */
    fill_rect(24, MAIN_Y + 240, CANVAS_W - 48, 76, COL_PANEL);
    out = g_buf;
    out = append_str(out, "api table: size ");
    out = append_dec(out, g_api != NULL ? g_api->size : 0u);
    out = append_str(out, " bytes, abi ");
    out = append_dec(out, g_api != NULL ? g_api->abi_version : 0u);
    *out = '\0';
    text_at(40, MAIN_Y + 262, g_buf, QEAF_TEXT_WEIGHT_REGULAR);
    text_at(40, MAIN_Y + 292,
            g_v4 ? "blit_pixels + draw_text_weighted" :
                   "legacy table: draw_text only",
            QEAF_TEXT_WEIGHT_REGULAR);

    /* Nav bar: four labels with a marker over the active one. */
    fill_rect(40, NAV_Y + 40, 132, 4, COL_LINE);
    text_at(40, NAV_Y + 62, "Home", QEAF_TEXT_WEIGHT_BOLD);
    text_at(210, NAV_Y + 62, "Files", QEAF_TEXT_WEIGHT_REGULAR);
    text_at(380, NAV_Y + 62, "Settings", QEAF_TEXT_WEIGHT_REGULAR);
    text_at(560, NAV_Y + 62, "About", QEAF_TEXT_WEIGHT_REGULAR);
}

/* ---- lifecycle ---------------------------------------------------------- */

void *create(const qeaf_app_api_v2 *api)
{
    if(api == NULL || api->abi_version < 2) return NULL;
    g_api = (const qeaf_app_api_v4 *)(const void *)api;
    g_v4 = api->abi_version >= VQEAF_APP_API_V4_VERSION &&
           api->size >= sizeof(qeaf_app_api_v4);
    if(api->log_utf8 != NULL) api->log_utf8("shellui: create");
    return &g_state;
}

int start(void *handle)
{
    (void)handle;
    if(g_api == NULL) return -1;
    draw_screen();
    return 0;
}

int pause(void *handle)
{
    (void)handle;
    return 0;
}

int resume(void *handle)
{
    (void)handle;
    if(g_api == NULL) return -1;
    draw_screen();
    return 0;
}

int stop(void *handle)
{
    (void)handle;
    return 0;
}

void destroy(void *handle)
{
    (void)handle;
    g_api = NULL;
}
