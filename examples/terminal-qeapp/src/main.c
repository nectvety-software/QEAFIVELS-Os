/* main.c - compact, native QEAF Terminal dashboard (QEAPP ABI v5).
 *
 * The app still reports only values read from the live framework API. Its
 * own chrome is drawn with the ABI pixel blitter and white text raster; no
 * LVGL, image library, shell access or synthetic device data is involved.
 * The device runtime does not forward keys to QEAPPs yet, so the prompt is
 * clearly marked as view-only instead of pretending to be interactive.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "qeaf_api.h"
#include "qeaf_native_abi.h"
#include "../../../system/ui/include/vqeaf/font8x8.h"

#define TERM_W 720
#define TERM_H 1560
#define TERM_STRIP_H 24
#define TERM_STRIP_PIXELS (TERM_W * TERM_STRIP_H)

static const qeaf_app_api_v5 *g_api;
static uint16_t g_strip[TERM_STRIP_PIXELS] __attribute__((aligned(16)));
static char g_number[24] __attribute__((aligned(16)));

/* RGB565 colors: deep ink, raised graphite-green, and a mint terminal accent. */
#define C_BG       0x08A2
#define C_TOP      0x1103
#define C_PANEL    0x1124
#define C_PANEL_2  0x1986
#define C_EDGE     0x3289
#define C_MINT     0x7758
#define C_GREEN    0x8ED3
#define C_AMBER    0xFE4B
#define C_RED      0xF430
#define C_WHITE    0xFFFF

static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    int offset = 0;
    if(g_api == NULL || g_api->blit_pixels == NULL || w <= 0 || h <= 0)
        return;
    if(x < 0) { w += x; x = 0; }
    if(y < 0) { h += y; y = 0; }
    if(x + w > TERM_W) w = TERM_W - x;
    if(y + h > TERM_H) h = TERM_H - y;
    if(w <= 0 || h <= 0 || w > TERM_W) return;

    while(offset < h) {
        int rows = TERM_STRIP_H;
        if(rows > h - offset) rows = h - offset;
        for(int i = 0; i < rows * w; i++) g_strip[i] = color;
        if(g_api->blit_pixels(x, y + offset, w, rows, w, g_strip) != 0)
            return;
        offset += rows;
    }
}

static void frame_rect(int x, int y, int w, int h, uint16_t edge,
                       uint16_t inside)
{
    fill_rect(x, y, w, h, edge);
    fill_rect(x + 2, y + 2, w - 4, h - 4, inside);
}

static void text_at(int x, int y, const char *text, int bold,
                    uint16_t background)
{
    int chars = 0;
    int width;
    if(g_api == NULL || g_api->blit_pixels == NULL || text == NULL ||
       x < 0 || y < 0 || y + 16 > TERM_H || x >= TERM_W) return;
    while(text[chars] != '\0' && x + (chars + 1) * 16 <= TERM_W) chars++;
    width = chars * 16;
    if(width <= 0) return;
    for(int i = 0; i < width * 16; i++) g_strip[i] = background;
    for(int i = 0; i < chars; i++) {
        unsigned char ch = (unsigned char)text[i];
        if(ch >= 128) ch = (unsigned char)'?';
        const uint8_t *glyph = vqeaf_font8x8[ch];
        for(int row = 0; row < 8; row++) {
            for(int col = 0; col < 8; col++) {
                if(!(glyph[row] & (1u << col))) continue;
                for(int sy = 0; sy < 2; sy++) {
                    for(int sx = 0; sx < 2; sx++)
                        g_strip[(row * 2 + sy) * width + i * 16 + col * 2 + sx] = C_WHITE;
                    if(bold && col < 7)
                        g_strip[(row * 2 + sy) * width + i * 16 + col * 2 + 2] = C_WHITE;
                }
            }
        }
    }
    (void)g_api->blit_pixels(x, y, width, 16, width, g_strip);
}

static void number_at(int x, int y, unsigned int value, uint16_t background)
{
    int n = 0;
    int started = 0;
    for(int place = 9; place >= 0; place--) {
        unsigned int divisor = 1;
        char digit = '0';
        for(int k = 0; k < place; k++) divisor *= 10u;
        while(value >= divisor) { value -= divisor; digit++; }
        if(digit == '0' && !started && place != 0) continue;
        started = 1;
        if(n < (int)sizeof(g_number) - 1) g_number[n++] = digit;
    }
    g_number[n] = '\0';
    text_at(x, y, g_number, 1, background);
}

static void metric_row(int y, const char *label, unsigned int value)
{
    text_at(48, y, label, 0, C_PANEL);
    number_at(540, y, value, C_PANEL);
    fill_rect(42, y + 18, 636, 1, C_EDGE);
}

static void permission_row(int y, const char *name, int granted)
{
    fill_rect(46, y + 3, 8, 8, granted ? C_GREEN : C_RED);
    text_at(68, y, name, 0, C_PANEL);
    text_at(514, y, granted ? "GRANTED" : "DENIED", 1, C_PANEL);
}

static int permission(const char *name)
{
    return g_api->request_permission != NULL ?
           g_api->request_permission(name) : 0;
}

static void draw_storage_probe(void)
{
    void *buffer = NULL;
    size_t size = 0;
    int rc = -1;
    if(g_api->read_file != NULL)
        rc = g_api->read_file("apps/org.vqeaf.terminal/assets/icon.rgba",
                              &buffer, &size);
    if(rc == 0) {
        text_at(48, 904, "READ_FILE", 1, C_PANEL);
        text_at(270, 904, "OK", 1, C_PANEL);
        number_at(324, 904, (unsigned int)size, C_PANEL);
        text_at(390, 904, "BYTES", 0, C_PANEL);
        if(buffer != NULL && g_api->free != NULL) g_api->free(buffer);
    } else {
        text_at(48, 904, "READ_FILE", 1, C_PANEL);
        text_at(270, 904, "REFUSED", 1, C_PANEL);
        text_at(48, 938, "Storage is not granted to this app.", 0, C_PANEL);
    }
}

void *create(const qeaf_app_api_v2 *api)
{
    if(api == NULL || api->abi_version != VQEAF_APP_API_V5_VERSION)
        return NULL;
    g_api = (const qeaf_app_api_v5 *)api;
    api->log_utf8("Terminal: create v5");
    return (void *)api;
}

int start(void *handle)
{
    int display_granted;
    int input_granted;
    if(g_api == NULL) return -1;

    display_granted = permission("ui.display");
    input_granted = permission("input.receive");

    fill_rect(0, 0, TERM_W, TERM_H, C_BG);
    fill_rect(0, 0, TERM_W, 100, C_TOP);
    fill_rect(0, 96, TERM_W, 4, C_MINT);
    text_at(28, 24, "QEAF / TERMINAL", 1, C_TOP);
    text_at(28, 54, "VIOLET SYSTEM CONSOLE", 0, C_TOP);
    fill_rect(602, 28, 10, 10, C_GREEN);
    text_at(622, 28, "READY", 1, C_TOP);

    frame_rect(24, 122, 672, 146, C_EDGE, C_PANEL);
    fill_rect(26, 124, 5, 142, C_MINT);
    text_at(50, 146, "SYSTEM SESSION", 1, C_PANEL);
    text_at(50, 178, "QEAFIVELS OS  /  VIOLET", 1, C_PANEL);
    text_at(50, 210, "NATIVE APPLICATION  -  ARM64", 0, C_PANEL);
    text_at(500, 146, display_granted ? "DISPLAY ON" : "DISPLAY OFF", 1, C_PANEL);

    frame_rect(24, 288, 672, 144, C_EDGE, C_PANEL);
    text_at(48, 310, "FRAMEWORK API", 1, C_PANEL);
    metric_row(350, "TABLE SIZE", g_api->size);
    metric_row(386, "ABI VERSION", g_api->abi_version);

    frame_rect(24, 452, 672, 326, C_EDGE, C_PANEL);
    text_at(48, 476, "PERMISSIONS", 1, C_PANEL);
    text_at(522, 476, "LIVE CHECK", 0, C_PANEL);
    permission_row(520, "ui.display", display_granted);
    permission_row(568, "input.receive", input_granted);
    permission_row(616, "storage.read", permission("storage.read"));
    permission_row(664, "storage.write.app", permission("storage.write.app"));
    permission_row(712, "media.read", permission("media.read"));

    frame_rect(24, 800, 672, 176, C_EDGE, C_PANEL);
    text_at(48, 824, "STORAGE PROBE", 1, C_PANEL);
    text_at(48, 860, "Read one app-local asset through API", 0, C_PANEL);
    draw_storage_probe();

    frame_rect(24, 998, 672, 172, C_EDGE, C_PANEL_2);
    fill_rect(26, 1000, 5, 168, C_MINT);
    text_at(50, 1024, "SESSION PROMPT", 1, C_PANEL_2);
    text_at(50, 1064, "vqeaf@violet:~$ _", 1, C_PANEL_2);
    text_at(50, 1112, "VIEW ONLY / key events are not forwarded", 0, C_PANEL_2);

    frame_rect(24, 1192, 672, 112, C_EDGE, C_PANEL);
    text_at(48, 1216, "DEVICE ACCESS", 1, C_PANEL);
    text_at(48, 1256, "QEAPP has no shell or kernel access.", 0, C_PANEL);

    fill_rect(24, 1408, 672, 2, C_EDGE);
    text_at(28, 1430, "QEAF / LOCAL CONSOLE", 0, C_BG);
    text_at(520, 1430, "READ ONLY", 1, C_BG);

    (void)handle;
    g_api->log_utf8("Terminal: dashboard rendered");
    return 0;
}

int pause(void *handle) { (void)handle; return g_api != NULL ? 0 : -1; }
int resume(void *handle) { (void)handle; return g_api != NULL ? 0 : -1; }
int stop(void *handle) { (void)handle; return 0; }

void destroy(void *handle)
{
    (void)handle;
    g_api = NULL;
}
