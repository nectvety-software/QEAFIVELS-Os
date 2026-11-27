#include <stddef.h>
#include <stdint.h>

#include "catalog.h"
#include "font_data/font_data.h"
#include "qeaf_api.h"
#include "qeaf_native_abi.h"

#define LW 360
#define LH 780
#define SC 2
#define CW (LW * SC)
#define CH (LH * SC)
#define BAND_ROWS 16
#define CONTENT_TOP 142
#define CONTENT_BOTTOM 712
#define MAX_HITS 64
#define CONSOLE_LINES 12
#define CONSOLE_WIDTH 128

#define C(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define COL_BG C(7, 11, 32)
#define COL_PANEL C(16, 26, 58)
#define COL_PANEL_2 C(23, 38, 79)
#define COL_VIOLET C(129, 87, 255)
#define COL_BLUE C(40, 123, 255)
#define COL_CYAN C(37, 217, 242)
#define COL_TEXT C(244, 245, 255)
#define COL_MUTED C(170, 178, 208)
#define COL_GREEN C(69, 224, 160)
#define COL_AMBER C(255, 200, 87)
#define COL_RED C(255, 107, 129)
#define COL_DISABLED C(62, 72, 110)

enum {
    SCREEN_MENU = 0,
    SCREEN_LIST,
    SCREEN_DETAIL,
    SCREEN_ABOUT,
    SCREEN_CONSOLE,
    SCREEN_EXIT
};

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    int16_t id;
} hit_t;

typedef struct {
    const qeaf_app_api_v5 *api;
    uint8_t screen;
    uint8_t category;
    uint8_t tool;
    int scroll;
    int scroll_max;
    hit_t hits[MAX_HITS];
    uint8_t hit_count;
    int touch_down;
    int touch_drag;
    int touch_start_x;
    int touch_start_y;
    int touch_start_scroll;
    char console[CONSOLE_LINES][CONSOLE_WIDTH];
    uint8_t console_count;
} app_t;

static app_t g_app;
static int g_clip_top = 0;
static int g_clip_bottom = LH;
static uint16_t g_fb[LW * LH] __attribute__((aligned(16)));
static uint16_t g_band[CW * BAND_ROWS * SC] __attribute__((aligned(16)));
static const uint16_t accents[] = {
    COL_VIOLET, COL_BLUE, COL_CYAN, COL_GREEN, COL_AMBER,
    COL_RED, COL_VIOLET, COL_BLUE, COL_CYAN
};

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while(n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int value, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while(n--) *d++ = (unsigned char)value;
    return dst;
}

static int str_equal(const char *a, const char *b)
{
    while(*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void copy_text(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    if(cap == 0) return;
    while(src[n] && n + 1 < cap) { dst[n] = src[n]; n++; }
    dst[n] = 0;
}

static int parse_number(const char *text)
{
    int value = 0;
    if(*text < '0' || *text > '9') return -1;
    while(*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        text++;
    }
    return *text == 0 ? value : -1;
}

static void number_text(char *out, uint32_t value)
{
    char reversed[12];
    size_t count = 0;
    size_t pos = 0;
    do { reversed[count++] = (char)('0' + value % 10u); value /= 10u; } while(value);
    while(count) out[pos++] = reversed[--count];
    out[pos] = 0;
}

static int clamp_scroll(int value)
{
    if(value < 0) return 0;
    if(value > g_app.scroll_max) return g_app.scroll_max;
    return value;
}

static void fb_pixel(int x, int y, uint16_t color)
{
    if(x >= 0 && y >= g_clip_top && y < g_clip_bottom && x < LW && y < LH)
        g_fb[y * LW + x] = color;
}

static void fb_rect(int x, int y, int w, int h, uint16_t color)
{
    int end_x = x + w;
    int end_y = y + h;
    if(x < 0) x = 0;
    if(y < g_clip_top) y = g_clip_top;
    if(end_x > LW) end_x = LW;
    if(end_y > g_clip_bottom) end_y = g_clip_bottom;
    if(end_y > LH) end_y = LH;
    for(int row = y; row < end_y; row++) {
        for(int col = x; col < end_x; col++) g_fb[row * LW + col] = color;
    }
}

static void fb_disc(int cx, int cy, int radius, uint16_t color)
{
    int r2 = radius * radius;
    for(int y = cy - radius; y <= cy + radius; y++) {
        for(int x = cx - radius; x <= cx + radius; x++) {
            int dx = x - cx;
            int dy = y - cy;
            if(dx * dx + dy * dy <= r2) fb_pixel(x, y, color);
        }
    }
}

static void fb_round(int x, int y, int w, int h, int radius, uint16_t color)
{
    if(w <= 0 || h <= 0) return;
    if(radius > w / 2) radius = w / 2;
    if(radius > h / 2) radius = h / 2;
    fb_rect(x + radius, y, w - radius * 2, h, color);
    fb_rect(x, y + radius, w, h - radius * 2, color);
    fb_disc(x + radius, y + radius, radius, color);
    fb_disc(x + w - radius - 1, y + radius, radius, color);
    fb_disc(x + radius, y + h - radius - 1, radius, color);
    fb_disc(x + w - radius - 1, y + h - radius - 1, radius, color);
}

static void panel(int x, int y, int w, int h, uint16_t fill)
{
    fb_round(x, y, w, h, 12, COL_PANEL_2);
    fb_round(x + 1, y + 1, w - 2, h - 2, 11, fill);
}

static uint16_t blend_channel(uint16_t background, uint16_t foreground, int shift,
                              unsigned coverage)
{
    int bg = (background >> shift) & (shift == 8 ? 0x3Fu : 0x1Fu);
    int fg = (foreground >> shift) & (shift == 8 ? 0x3Fu : 0x1Fu);
    return (uint16_t)((bg * (255u - coverage) + fg * coverage + 127u) / 255u);
}

static void blend_pixel(int x, int y, uint16_t color, unsigned coverage)
{
    if(coverage == 0 || x < 0 || y < g_clip_top || y >= g_clip_bottom ||
       x >= LW || y >= LH) return;
    if(coverage >= 255) { g_fb[y * LW + x] = color; return; }
    uint16_t bg = g_fb[y * LW + x];
    uint16_t r = blend_channel(bg, color, 11, coverage);
    uint16_t g = blend_channel(bg, color, 5, coverage);
    uint16_t b = blend_channel(bg, color, 0, coverage);
    g_fb[y * LW + x] = (uint16_t)((r << 11) | (g << 5) | b);
}

static int glyph_index(uint32_t codepoint)
{
    int low = 0;
    int high = TOOLHUB_FONT_COUNT - 1;
    while(low <= high) {
        int middle = low + (high - low) / 2;
        uint16_t value = toolhub_font_codepoints[middle];
        if(value == codepoint) return middle;
        if(value < codepoint) low = middle + 1;
        else high = middle - 1;
    }
    return -1;
}

static int utf8_next(const char **cursor, uint32_t *codepoint)
{
    const unsigned char *p = (const unsigned char *)*cursor;
    uint32_t cp;
    int length;
    if(p[0] < 0x80) { cp = p[0]; length = 1; }
    else if(p[0] >= 0xC2 && p[0] <= 0xDF && (p[1] & 0xC0) == 0x80) {
        cp = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F); length = 2;
    } else if(p[0] >= 0xE0 && p[0] <= 0xEF && (p[1] & 0xC0) == 0x80 &&
              (p[2] & 0xC0) == 0x80 &&
              !(p[0] == 0xE0 && p[1] < 0xA0) && !(p[0] == 0xED && p[1] >= 0xA0)) {
        cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) |
             (p[2] & 0x3F); length = 3;
    } else if(p[0] >= 0xF0 && p[0] <= 0xF4 && (p[1] & 0xC0) == 0x80 &&
              (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80 &&
              !(p[0] == 0xF0 && p[1] < 0x90) && !(p[0] == 0xF4 && p[1] >= 0x90)) {
        cp = ((uint32_t)(p[0] & 7) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
             ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F); length = 4;
    } else { cp = '?'; length = 1; }
    *cursor += length;
    *codepoint = cp;
    return length;
}

static int metric_half(int value)
{
    return value >= 0 ? (value + 1) / 2 : -((-value + 1) / 2);
}

static int glyph_advance(const toolhub_font_glyph_t *glyph, int scale)
{
    return (glyph->advance + 8 * scale) / (16 * scale);
}

static void draw_glyph(int x, int baseline, const toolhub_font_glyph_t *glyph,
                       int scale, uint16_t color)
{
    int width = (glyph->width + scale - 1) / scale;
    int height = (glyph->height + scale - 1) / scale;
    int left = x + metric_half(glyph->ofs_x);
    int top = baseline - height - metric_half(glyph->ofs_y);
    for(int ty = 0; ty < height; ty++) {
        for(int tx = 0; tx < width; tx++) {
            unsigned sum = 0;
            unsigned samples = 0;
            for(int sy = 0; sy < scale; sy++) {
                int source_y = ty * scale + sy;
                if(source_y >= glyph->height) continue;
                for(int sx = 0; sx < scale; sx++) {
                    int source_x = tx * scale + sx;
                    if(source_x >= glyph->width) continue;
                    sum += toolhub_font_bitmap[glyph->bitmap_offset +
                                               source_y * glyph->width + source_x];
                    samples++;
                }
            }
            if(samples) blend_pixel(left + tx, top + ty, color, sum / samples);
        }
    }
}

static int text_width_n(const char *text, size_t bytes, int scale)
{
    int width = 0;
    size_t used = 0;
    const char *cursor = text;
    while(used < bytes && *cursor) {
        uint32_t cp;
        int length = utf8_next(&cursor, &cp);
        if((size_t)length > bytes - used) break;
        used += (size_t)length;
        int index = glyph_index(cp);
        if(index < 0) index = glyph_index('?');
        if(index >= 0) width += glyph_advance(&toolhub_font_glyphs[index], scale);
    }
    return width;
}

static int text_width(const char *text, int scale)
{
    return text_width_n(text, (size_t)-1, scale);
}

static void draw_text_n(int x, int y, const char *text, size_t bytes,
                        int scale, uint16_t color)
{
    int baseline = y + (scale == 1 ? 19 : 9);
    size_t used = 0;
    const char *cursor = text;
    while(used < bytes && *cursor) {
        uint32_t cp;
        int length = utf8_next(&cursor, &cp);
        if((size_t)length > bytes - used) break;
        used += (size_t)length;
        int index = glyph_index(cp);
        if(index < 0) index = glyph_index('?');
        if(index < 0) continue;
        const toolhub_font_glyph_t *glyph = &toolhub_font_glyphs[index];
        draw_glyph(x, baseline, glyph, scale, color);
        x += glyph_advance(glyph, scale);
    }
}

static void draw_text(int x, int y, const char *text, int scale, uint16_t color)
{
    draw_text_n(x, y, text, (size_t)-1, scale, color);
}

static int draw_wrapped(int x, int y, int max_width, const char *text,
                        int scale, uint16_t color, int max_lines)
{
    const char *cursor = text;
    int line_height = scale == 1 ? 25 : 13;
    int lines = 0;
    while(*cursor && lines < max_lines) {
        const char *end = cursor;
        const char *scan = cursor;
        const char *last_space = NULL;
        int width = 0;
        while(*scan) {
            const char *next = scan;
            uint32_t cp;
            (void)utf8_next(&next, &cp);
            int index = glyph_index(cp);
            if(index < 0) index = glyph_index('?');
            int advance = index >= 0 ? glyph_advance(&toolhub_font_glyphs[index], scale) : 0;
            if(scan != cursor && width + advance > max_width) break;
            width += advance;
            end = next;
            if(*scan == ' ') last_space = scan;
            scan = next;
        }
        if(end == cursor) {
            uint32_t cp;
            (void)utf8_next(&end, &cp);
        } else if(last_space && last_space > cursor) {
            end = last_space;
        }
        draw_text_n(x, y, cursor, (size_t)(end - cursor), scale, color);
        cursor = end;
        while(*cursor == ' ') cursor++;
        y += line_height;
        lines++;
    }
    return y;
}

static void add_hit(int x, int y, int w, int h, int id)
{
    int end_y = y + h;
    if(g_app.hit_count >= MAX_HITS) return;
    if(y < g_clip_top) y = g_clip_top;
    if(end_y > g_clip_bottom) end_y = g_clip_bottom;
    h = end_y - y;
    if(w <= 0 || h <= 0) return;
    hit_t *hit = &g_app.hits[g_app.hit_count++];
    hit->x = (int16_t)x;
    hit->y = (int16_t)y;
    hit->w = (int16_t)w;
    hit->h = (int16_t)h;
    hit->id = (int16_t)id;
}

static int hit_at(int x, int y)
{
    for(int i = (int)g_app.hit_count - 1; i >= 0; i--) {
        const hit_t *hit = &g_app.hits[i];
        if(x >= hit->x && y >= hit->y && x < hit->x + hit->w && y < hit->y + hit->h)
            return hit->id;
    }
    return 0;
}

static void draw_button(int x, int y, int w, int h, const char *label,
                        int id, int enabled, uint16_t accent)
{
    panel(x, y, w, h, enabled ? COL_PANEL : COL_BG);
    if(enabled) fb_round(x, y, 4, h, 2, accent);
    int width = text_width(label, 2);
    draw_text(x + (w - width) / 2, y + (h - 13) / 2, label, 2,
              enabled ? COL_TEXT : COL_MUTED);
    if(enabled) add_hit(x, y, w, h, id);
}

static const char *screen_name(void)
{
    switch(g_app.screen) {
    case SCREEN_MENU: return "DANH MỤC CÔNG CỤ";
    case SCREEN_LIST: return "CÔNG CỤ THEO NHÓM";
    case SCREEN_DETAIL: return "CHI TIẾT CÔNG CỤ";
    case SCREEN_ABOUT: return "GIỚI THIỆU & NĂNG LỰC";
    case SCREEN_CONSOLE: return "CONSOLE CỤC BỘ";
    case SCREEN_EXIT: return "KẾT THÚC PHIÊN";
    default: return "QEAF TOOL HUB";
    }
}

static const char *navigation_label(uint8_t index)
{
    switch(index) {
    case 0: return "Menu";
    case 1: return "Console";
    case 2: return "Thông tin";
    default: return "Thoát";
    }
}

static void draw_header(void)
{
    fb_round(16, 16, 48, 48, 14, COL_VIOLET);
    int q_width = text_width("Q", 1);
    draw_text(40 - q_width / 2, 26, "Q", 1, COL_TEXT);
    draw_text(76, 15, "QEAF Tool Hub", 1, COL_TEXT);
    draw_text(76, 48, "CATALOG QEAF · ABI v5", 2, COL_MUTED);
    panel(296, 16, 44, 44, COL_PANEL);
    int i_width = text_width("i", 1);
    draw_text(318 - i_width / 2, 27, "i", 1, COL_CYAN);
    add_hit(296, 16, 44, 44, 4);
    fb_rect(16, 86, 328, 1, COL_PANEL_2);
    draw_text(18, 100, screen_name(), 2, COL_CYAN);
    draw_text(18, 118, "Chạm để chọn · vuốt để cuộn", 2, COL_MUTED);
}

static void draw_navigation(void)
{
    const int ids[] = { 2, 3, 4, 5 };
    for(int i = 0; i < 4; i++) {
        int x = 16 + i * 85;
        int selected = (i == 0 && g_app.screen == SCREEN_MENU) ||
                       (i == 1 && g_app.screen == SCREEN_CONSOLE) ||
                       (i == 2 && g_app.screen == SCREEN_ABOUT);
        panel(x, 724, 77, 44, selected ? COL_PANEL_2 : COL_PANEL);
        if(selected) fb_round(x, 724, 77, 3, 1, COL_VIOLET);
        draw_text(x + 10, 739, navigation_label((uint8_t)i), 2,
                  selected ? COL_TEXT : COL_MUTED);
        add_hit(x, 724, 77, 44, ids[i]);
    }
}

static void draw_scrollbar(void)
{
    if(g_app.scroll_max <= 0) return;
    int track_h = CONTENT_BOTTOM - CONTENT_TOP - 8;
    int thumb_h = track_h * track_h / (track_h + g_app.scroll_max);
    if(thumb_h < 24) thumb_h = 24;
    int travel = track_h - thumb_h;
    int y = CONTENT_TOP + 4 + travel * g_app.scroll / g_app.scroll_max;
    fb_round(350, CONTENT_TOP + 4, 3, track_h, 1, COL_PANEL_2);
    fb_round(350, y, 3, thumb_h, 1, COL_VIOLET);
}

static void draw_menu(void)
{
    int rows = ((int)TOOLHUB_CATEGORY_COUNT + 1) / 2;
    int total = rows * 78 + (rows - 1) * 10;
    g_app.scroll_max = total > CONTENT_BOTTOM - CONTENT_TOP ? total - (CONTENT_BOTTOM - CONTENT_TOP) : 0;
    g_app.scroll = clamp_scroll(g_app.scroll);
    for(uint8_t i = 0; i < TOOLHUB_CATEGORY_COUNT; i++) {
        int col = i & 1;
        int row = i / 2;
        int x = 16 + col * 168;
        int y = CONTENT_TOP + row * 88 - g_app.scroll;
        if(y + 78 < CONTENT_TOP || y > CONTENT_BOTTOM) continue;
        panel(x, y, 156, 78, COL_PANEL);
        fb_round(x + 12, y + 12, 30, 22, 7, accents[i % 9]);
        char number[4];
        number_text(number, i + 1u);
        int number_width = text_width(number, 2);
        draw_text(x + 27 - number_width / 2, y + 17, number, 2, COL_BG);
        draw_wrapped(x + 12, y + 42, 132, toolhub_category_name(i), 2, COL_TEXT, 2);
        add_hit(x, y, 156, 78, 100 + i);
    }
}

static const char *status_label(uint8_t status)
{
    switch(status) {
    case TOOLHUB_LOCAL: return "CỤC BỘ";
    case TOOLHUB_INFORMATION: return "THÔNG TIN";
    default: return "CHƯA HỖ TRỢ";
    }
}

static uint16_t status_color(uint8_t status)
{
    switch(status) {
    case TOOLHUB_LOCAL: return COL_GREEN;
    case TOOLHUB_INFORMATION: return COL_CYAN;
    default: return COL_AMBER;
    }
}

static void draw_status(int x, int y, uint8_t status)
{
    const char *label = status_label(status);
    int width = text_width(label, 2) + 16;
    fb_round(x, y, width, 20, 7, status_color(status));
    draw_text(x + 8, y + 4, label, 2, COL_BG);
}

static void draw_list(void)
{
    uint8_t first = toolhub_category_first_tool(g_app.category);
    uint8_t count = toolhub_category_tool_count(g_app.category);
    int total = count * 104;
    g_app.scroll_max = total > CONTENT_BOTTOM - 190 ? total - (CONTENT_BOTTOM - 190) : 0;
    g_app.scroll = clamp_scroll(g_app.scroll);
    draw_text(18, 145, toolhub_category_name(g_app.category), 1, COL_TEXT);
    draw_text(18, 174, toolhub_category_description(g_app.category), 2, COL_MUTED);
    for(uint8_t n = 0; n < count; n++) {
        uint8_t tool = first + n;
        int y = 202 + n * 104 - g_app.scroll;
        if(y + 96 < 198 || y > CONTENT_BOTTOM) continue;
        panel(16, y, 328, 96, COL_PANEL);
        draw_wrapped(30, y + 12, 214, toolhub_tool_name(tool), 2, COL_TEXT, 1);
        draw_wrapped(30, y + 33, 300, toolhub_tool_description(tool), 2, COL_MUTED, 2);
        draw_status(30, y + 69, toolhub_tool_status(tool));
        int arrow_width = text_width("→", 1);
        draw_text(326 - arrow_width, y + 68, "→", 1, COL_CYAN);
        add_hit(16, y, 328, 96, 1000 + tool);
    }
}

static void draw_detail(void)
{
    uint8_t tool = g_app.tool;
    uint8_t status = toolhub_tool_status(tool);
    int y = 146 - g_app.scroll;
    y = draw_wrapped(18, y, 324, toolhub_tool_name(tool), 1, COL_TEXT, 2);
    y += 8;
    draw_status(18, y, status);
    y += 32;
    y = draw_wrapped(18, y, 324, toolhub_tool_description(tool), 2, COL_MUTED, 2);
    y += 12;
    y = draw_wrapped(18, y, 324, toolhub_tool_detail(tool), 2, COL_TEXT, 8);
    y += 18;
    draw_text(18, y, "VÍ DỤ MINH HỌA · KHÔNG CHẠY", 2, COL_CYAN);
    y += 20;
    int example_height = 64;
    panel(18, y, 324, example_height, COL_BG);
    draw_wrapped(32, y + 14, 296, toolhub_tool_example(tool), 2, COL_TEXT, 3);
    y += example_height + 18;
    if(status == TOOLHUB_LOCAL) {
        draw_button(18, y, 324, 48, "MỞ HÀNH ĐỘNG CỤC BỘ", 3000 + tool, 1, COL_GREEN);
        draw_text(18, y + 58, "Hành động gọi API hoặc mở màn hình trong app.", 2, COL_MUTED);
        y += 82;
    } else if(status == TOOLHUB_UNAVAILABLE) {
        draw_button(18, y, 324, 48, "CÀI ĐẶT / CHẠY", 0, 0, COL_AMBER);
        draw_wrapped(18, y + 58, 324,
                     "Không có dịch vụ thật. Không tải gói, không mở tiến trình và không hiển thị tiến độ giả.",
                     2, COL_MUTED, 3);
        y += 102;
    } else {
        draw_button(18, y, 324, 48, "THÔNG TIN · KHÔNG HÀNH ĐỘNG", 0, 0, COL_CYAN);
        draw_text(18, y + 58, "Nội dung này là thông tin cục bộ của ứng dụng.", 2, COL_MUTED);
        y += 82;
    }
    g_app.scroll_max = y > CONTENT_BOTTOM ? y - CONTENT_BOTTOM : 0;
    g_app.scroll = clamp_scroll(g_app.scroll);
}

static void draw_about(void)
{
    panel(18, 146, 324, 100, COL_PANEL);
    draw_text(34, 160, "QEAF Tool Hub 1.0.0", 1, COL_TEXT);
    draw_wrapped(34, 193, 292,
                 "Catalog công cụ bằng tiếng Việt, render pixel riêng và console lệnh cục bộ.",
                 2, COL_MUTED, 3);

    panel(18, 258, 324, 112, COL_PANEL);
    draw_text(34, 272, "NGHĨA VỤ & QUYỀN", 2, COL_CYAN);
    draw_wrapped(34, 296, 292,
                 "Chỉ yêu cầu ui.display và input.receive. Quyền được broker quyết định; app không tự cấp thêm.",
                 2, COL_TEXT, 4);

    panel(18, 382, 324, 112, COL_PANEL);
    draw_text(34, 396, "RANH GIỚI THẬT", 2, COL_CYAN);
    draw_wrapped(34, 420, 292,
                 "Không shell. Không mạng. Không root. Không ghi tệp. Không tải hoặc thực thi mã bên thứ ba.",
                 2, COL_TEXT, 4);

    panel(18, 506, 324, 112, COL_PANEL);
    draw_text(34, 520, "CÔNG NGHỆ", 2, COL_CYAN);
    draw_wrapped(34, 544, 292,
                 "Native ABI v5 · canvas 360×780 · blit 2× · font Ubuntu 20 px nhúng dưới dạng dữ liệu vô hướng.",
                 2, COL_TEXT, 4);

    panel(18, 630, 324, 70, COL_PANEL);
    draw_wrapped(34, 642, 292,
                 "Đóng phiên bằng nút Quay lại của hệ thống. Không có nút thoát giả.",
                 2, COL_MUTED, 3);
    g_app.scroll_max = 0;
    g_app.scroll = 0;
}

static const char *console_command_label(uint8_t index)
{
    switch(index) {
    case 0: return "help";
    case 1: return "list";
    case 2: return "perms";
    case 3: return "about";
    case 4: return "clear";
    case 5: return "tools 1";
    case 6: return "tools 6";
    case 7: return "open 1";
    case 8: return "back";
    default: return "exit";
    }
}

static const char *console_command_value(uint8_t index)
{
    switch(index) {
    case 0: return "help";
    case 1: return "list";
    case 2: return "perms";
    case 3: return "about";
    case 4: return "clear";
    case 5: return "tools 1";
    case 6: return "tools 6";
    case 7: return "open 1";
    case 8: return "back";
    default: return "exit";
    }
}

static void draw_console(void)
{
    panel(16, 144, 328, 358, COL_BG);
    draw_text(30, 156, "qeaf@toolhub:~$", 2, COL_GREEN);
    draw_text(30, 176, "Lệnh cục bộ · không phải shell", 2, COL_MUTED);
    int y = 204;
    int start = g_app.console_count > 8 ? g_app.console_count - 8 : 0;
    for(int i = start; i < g_app.console_count; i++) {
        y = draw_wrapped(30, y, 300, g_app.console[i], 2,
                         g_app.console[i][0] == '>' ? COL_CYAN : COL_TEXT, 2);
        y += 4;
    }
    draw_text(18, 514, "CHẠY LỆNH CỤC BỘ", 2, COL_CYAN);
    for(uint8_t i = 0; i < 10; i++) {
        int col = i & 1;
        int row = i / 2;
        draw_button(16 + col * 168, 538 + row * 34, 156, 28,
                    console_command_label(i), 4000 + i, 1,
                    i == 9 ? COL_RED : COL_VIOLET);
    }
    g_app.scroll_max = 0;
    g_app.scroll = 0;
}

static void draw_exit(void)
{
    panel(28, 182, 304, 282, COL_PANEL);
    draw_text(48, 204, "Kết thúc phiên?", 1, COL_TEXT);
    draw_wrapped(48, 242, 264,
                 "Tool Hub không có API đóng chính tiến trình của mình. Dùng nút Quay lại trên thanh hệ thống để đóng phiên an toàn.",
                 2, COL_MUTED, 6);
    draw_wrapped(48, 342, 264,
                 "Lệnh exit cũng không thực thi shell; nó chỉ giải thích cách đóng phiên.",
                 2, COL_MUTED, 4);
    draw_button(48, 414, 264, 36, "VỀ MENU", 5000, 1, COL_VIOLET);
    g_app.scroll_max = 0;
    g_app.scroll = 0;
}

static void present(void)
{
    for(int first = 0; first < LH; first += BAND_ROWS) {
        int rows = LH - first;
        if(rows > BAND_ROWS) rows = BAND_ROWS;
        for(int row = 0; row < rows; row++) {
            const uint16_t *source = &g_fb[(first + row) * LW];
            for(int x = 0; x < LW; x++) {
                uint16_t pixel = source[x];
                g_band[(row * 2) * CW + x * 2] = pixel;
                g_band[(row * 2) * CW + x * 2 + 1] = pixel;
                g_band[(row * 2 + 1) * CW + x * 2] = pixel;
                g_band[(row * 2 + 1) * CW + x * 2 + 1] = pixel;
            }
        }
        (void)g_app.api->blit_pixels(0, first * SC, CW, rows * SC, CW, g_band);
    }
}

static void redraw(void)
{
    g_app.hit_count = 0;
    fb_rect(0, 0, LW, LH, COL_BG);
    draw_header();
    g_clip_top = CONTENT_TOP;
    g_clip_bottom = CONTENT_BOTTOM;
    switch(g_app.screen) {
    case SCREEN_MENU: draw_menu(); break;
    case SCREEN_LIST: draw_list(); break;
    case SCREEN_DETAIL: draw_detail(); break;
    case SCREEN_ABOUT: draw_about(); break;
    case SCREEN_CONSOLE: draw_console(); break;
    case SCREEN_EXIT: draw_exit(); break;
    default: break;
    }
    g_clip_top = 0;
    g_clip_bottom = LH;
    draw_scrollbar();
    draw_navigation();
    present();
}

static void console_append(const char *text)
{
    if(g_app.console_count >= CONSOLE_LINES) {
        for(int i = 1; i < CONSOLE_LINES; i++)
            copy_text(g_app.console[i - 1], CONSOLE_WIDTH, g_app.console[i]);
        g_app.console_count = CONSOLE_LINES - 1;
    }
    copy_text(g_app.console[g_app.console_count++], CONSOLE_WIDTH, text);
}

static void console_echo(const char *command)
{
    char line[CONSOLE_WIDTH];
    size_t pos = 0;
    line[pos++] = '>';
    line[pos++] = ' ';
    while(*command && pos + 1 < sizeof(line)) line[pos++] = *command++;
    line[pos] = 0;
    console_append(line);
}

static void show_about(void)
{
    g_app.screen = SCREEN_ABOUT;
    g_app.scroll = 0;
    redraw();
}

static void console_run(const char *command)
{
    console_echo(command);
    g_app.screen = SCREEN_CONSOLE;
    g_app.scroll = 0;
    if(str_equal(command, "help")) {
        console_append("help list perms about clear");
        console_append("tools 1..17 open 1..34 back exit");
        console_append("Không có shell hoặc tham số tự do.");
    } else if(str_equal(command, "list")) {
        console_append("17 nhóm · 34 công cụ · offline");
        console_append("Dùng tools 1..17 để mở nhóm.");
    } else if(str_equal(command, "perms")) {
        int display = g_app.api->request_permission("ui.display");
        int input = g_app.api->request_permission("input.receive");
        console_append(display ? "ui.display: ĐÃ CẤP" : "ui.display: KHÔNG CẤP");
        console_append(input ? "input.receive: ĐÃ CẤP" : "input.receive: KHÔNG CẤP");
    } else if(str_equal(command, "about")) {
        show_about();
        return;
    } else if(str_equal(command, "clear")) {
        g_app.console_count = 0;
    } else if(command[0] == 't' && command[1] == 'o' && command[2] == 'o' &&
              command[3] == 'l' && command[4] == 's' && command[5] == ' ') {
        int category = parse_number(command + 6) - 1;
        if(category >= 0 && category < (int)TOOLHUB_CATEGORY_COUNT) {
            g_app.category = (uint8_t)category;
            g_app.screen = SCREEN_LIST;
            g_app.scroll = 0;
            redraw();
            return;
        }
        console_append("Nhóm phải nằm trong khoảng 1..17.");
    } else if(command[0] == 'o' && command[1] == 'p' && command[2] == 'e' &&
              command[3] == 'n' && command[4] == ' ') {
        int tool = parse_number(command + 5) - 1;
        if(tool >= 0 && tool < (int)TOOLHUB_TOOL_COUNT) {
            g_app.tool = (uint8_t)tool;
            g_app.screen = SCREEN_DETAIL;
            g_app.scroll = 0;
            redraw();
            return;
        }
        console_append("Chỉ số công cụ phải nằm trong 1..34.");
    } else if(str_equal(command, "back")) {
        g_app.screen = SCREEN_MENU;
        g_app.scroll = 0;
    } else if(str_equal(command, "exit")) {
        console_append("Dùng nút Quay lại hệ thống để đóng phiên.");
    } else {
        console_append("Lệnh không được hỗ trợ.");
    }
    redraw();
}

static void activate(int id)
{
    if(id >= 100 && id < 100 + (int)TOOLHUB_CATEGORY_COUNT) {
        g_app.category = (uint8_t)(id - 100);
        g_app.screen = SCREEN_LIST;
        g_app.scroll = 0;
        redraw();
        return;
    }
    if(id >= 1000 && id < 1000 + (int)TOOLHUB_TOOL_COUNT) {
        g_app.tool = (uint8_t)(id - 1000);
        g_app.screen = SCREEN_DETAIL;
        g_app.scroll = 0;
        redraw();
        return;
    }
    if(id >= 3000 && id < 3000 + (int)TOOLHUB_TOOL_COUNT) {
        uint8_t tool = (uint8_t)(id - 3000);
        if(tool == 16 || tool == 25) console_run("perms");
        else if(tool == 17 || tool == 22 || tool == 32 || tool == 33) console_run("about");
        else if(tool == 24) console_run("help");
        return;
    }
    if(id >= 4000 && id < 4010) {
        console_run(console_command_value((uint8_t)(id - 4000)));
        return;
    }
    switch(id) {
    case 1:
        g_app.screen = g_app.screen == SCREEN_DETAIL ? SCREEN_LIST : SCREEN_MENU;
        g_app.scroll = 0;
        redraw();
        break;
    case 2:
        g_app.screen = SCREEN_MENU;
        g_app.scroll = 0;
        redraw();
        break;
    case 3:
        g_app.screen = SCREEN_CONSOLE;
        g_app.scroll = 0;
        redraw();
        break;
    case 4:
        show_about();
        break;
    case 5:
        g_app.screen = SCREEN_EXIT;
        g_app.scroll = 0;
        redraw();
        break;
    case 5000:
        g_app.screen = SCREEN_MENU;
        g_app.scroll = 0;
        redraw();
        break;
    default:
        break;
    }
}

static void handle_key(const qeaf_input_event_t *event)
{
    uint32_t code = event->code;
    if(code == 103 || code == 108) {
        g_app.scroll = clamp_scroll(g_app.scroll - 72);
        redraw();
    } else if(code == 106 || code == 105) {
        g_app.scroll = clamp_scroll(g_app.scroll + 72);
        redraw();
    } else if(code == 14 || code == 1) {
        activate(1);
    } else if(code == 28) {
        if(g_app.screen == SCREEN_MENU) activate(100 + g_app.category);
        else if(g_app.screen == SCREEN_LIST)
            activate(1000 + toolhub_category_first_tool(g_app.category));
        else activate(1);
    } else if(code >= 2 && code <= 10) {
        int index = (int)code - 2;
        if(g_app.screen == SCREEN_MENU && index < (int)TOOLHUB_CATEGORY_COUNT)
            activate(100 + index);
        else if(g_app.screen == SCREEN_LIST)
            activate(1000 + toolhub_category_first_tool(g_app.category) + index);
    } else if(code == 46) {
        activate(3);
    } else if(code == 23) {
        activate(4);
    } else if(code == 35) {
        console_run("help");
    } else if(code == 38) {
        console_run("list");
    } else if(code == 25) {
        console_run("perms");
    }
}

void *QEAF_APP_CALL create(const qeaf_app_api_v2 *api)
{
    memset(&g_app, 0, sizeof(g_app));
    g_app.api = (const qeaf_app_api_v5 *)api;
    g_app.screen = SCREEN_MENU;
    g_app.console_count = 0;
    console_append("QEAF Tool Hub · phiên cục bộ");
    console_append("Gõ help để xem lệnh hợp lệ.");
    redraw();
    return &g_app;
}

int QEAF_APP_CALL start(void *handle)
{
    (void)handle;
    g_app.api->log_utf8("toolhub: start ABI v5");
    redraw();
    return 0;
}

int QEAF_APP_CALL pause(void *handle)
{
    (void)handle;
    return 0;
}

int QEAF_APP_CALL resume(void *handle)
{
    (void)handle;
    redraw();
    return 0;
}

int QEAF_APP_CALL stop(void *handle)
{
    (void)handle;
    g_app.api->log_utf8("toolhub: stop");
    return 0;
}

void QEAF_APP_CALL destroy(void *handle)
{
    (void)handle;
}

void QEAF_APP_CALL on_input(void *handle, const qeaf_input_event_t *event)
{
    (void)handle;
    if(event == NULL || g_app.api == NULL) return;
    if(event->type == QEAF_INPUT_KEY) {
        if(event->pressed) handle_key(event);
        return;
    }
    if(event->type != QEAF_INPUT_TOUCH) return;
    int x = event->x / SC;
    int y = event->y / SC;
    if(event->pressed) {
        if(!g_app.touch_down) {
            g_app.touch_down = 1;
            g_app.touch_drag = 0;
            g_app.touch_start_x = x;
            g_app.touch_start_y = y;
            g_app.touch_start_scroll = g_app.scroll;
        } else if(g_app.scroll_max > 0) {
            int dy = g_app.touch_start_y - y;
            int dx = g_app.touch_start_x - x;
            if(dy > 8 || dy < -8 || dx > 8 || dx < -8) g_app.touch_drag = 1;
            if(g_app.touch_drag) {
                g_app.scroll = clamp_scroll(g_app.touch_start_scroll + dy);
                redraw();
            }
        }
    } else {
        if(g_app.touch_down && !g_app.touch_drag) activate(hit_at(x, y));
        g_app.touch_down = 0;
        g_app.touch_drag = 0;
    }
}
