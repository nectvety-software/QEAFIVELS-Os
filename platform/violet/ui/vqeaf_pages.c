/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_pages.c - lock, home, apps, control centre and
 * settings pages for the VQEAF phone shell.
 *
 * Every value that comes from the device (clock, battery, Wi-Fi, storage,
 * backlight) is read through vqeaf_state; the rest is chrome. Quick-setting
 * switches without a hardware backend keep a shell-local state and say so in
 * the row subtitle instead of pretending to change the radio.
 */
#include "vqeaf_pages.h"

#include <stdint.h>
#include <stdarg.h>
#include <strings.h>
#if defined(__linux__)
#include <sys/utsname.h>
#endif
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vqeaf_state.h"
#include "vqeaf_theme.h"
#include "vqeaf_ime.h"
#include "violet.h"
#include "vqeaf_wifi.h"
#include "vqeaf_files.h"
#include "vqeaf_sys_tools.h"
#include "vqeaf_tools_ui.h"
#include "vqeaf_media.h"
#include "vqeaf_video.h"
#include "vqeaf_qeapp.h"
#include "vqeaf_qeapp_device.h"
#include <vqeaf/settings.h>
#include "qeaf_native_abi.h"
#include "icons/vqeaf_app_icons.h"
#include "qeaf_version.h"
#include "libs/lodepng/lodepng.h"
#include "libs/tjpgd/tjpgd.h"

/* ---- shared bits --------------------------------------------------------- */

static lv_obj_t *page_clock;
static lv_obj_t *page_date;

/* Labels on the detail pages that follow live providers. They are rebound on
 * every rebuild, so at most one page's widgets are attached at a time. */
static lv_obj_t *live_uptime;
static lv_obj_t *live_mem_pair;
static lv_obj_t *live_storage_pct;
static lv_obj_t *live_storage_pair;
static lv_obj_t *live_storage_bar;
static lv_obj_t *live_battery;
static lv_obj_t *live_battery_pct;
static lv_obj_t *live_usb;
static const char *usb_mode_text(void);
static lv_obj_t *live_wifi;
static lv_obj_t *live_brightness;

static void format_bytes(uint64_t bytes, char *out, size_t cap)
{
    static const char *const units[] = { "B", "KB", "MB", "GB", "TB" };
    uint64_t whole = bytes;
    unsigned unit = 0;
    unsigned frac = 0;

    while(whole >= 1024u && unit + 1u < sizeof(units) / sizeof(units[0])) {
        frac = (unsigned)(((whole % 1024u) * 10u + 512u) / 1024u);
        whole /= 1024u;
        unit++;
    }
    if(frac > 9u) {
        whole++;
        frac = 0;
    }
    if(frac != 0u)
        snprintf(out, cap, "%llu.%u %s", (unsigned long long)whole, frac,
                 units[unit]);
    else
        snprintf(out, cap, "%llu %s", (unsigned long long)whole, units[unit]);
}

static void format_uptime(uint64_t seconds, char *out, size_t cap)
{
    unsigned days = (unsigned)(seconds / 86400u);
    unsigned hours = (unsigned)((seconds / 3600u) % 24u);
    unsigned minutes = (unsigned)((seconds / 60u) % 60u);

    if(days > 0)
        snprintf(out, cap, "%u ngày %02u:%02u", days, hours, minutes);
    else if(hours > 0)
        snprintf(out, cap, "%u giờ %02u phút", hours, minutes);
    else
        snprintf(out, cap, "%u phút", minutes);
}

static const char *wifi_text(void)
{
    return vqeaf_state.wifi_up > 0 ? "Đang bật" :
           vqeaf_state.wifi_up == 0 ? "Đang tắt" : "Không có thiết bị";
}

static void battery_text(char *out, size_t cap)
{
    if(!vqeaf_state.battery_valid) {
        snprintf(out, cap, "Không có pin");
        return;
    }
    snprintf(out, cap, vqeaf_state.charging ? "%d%% · Đang sạc" : "%d%%",
             vqeaf_state.battery_pct);
}

static void storage_pair_text(char *out, size_t cap)
{
    char used[24];
    char total[24];

    if(!vqeaf_state.storage_valid) {
        snprintf(out, cap, "Không đọc được");
        return;
    }
    format_bytes(vqeaf_state.storage_total - vqeaf_state.storage_free,
                 used, sizeof(used));
    format_bytes(vqeaf_state.storage_total, total, sizeof(total));
    snprintf(out, cap, "%s / %s", used, total);
}

static void memory_pair_text(char *out, size_t cap)
{
    char used[24];
    char total[24];

    if(!vqeaf_state.mem_valid || vqeaf_state.mem_total_kb == 0) {
        snprintf(out, cap, "Không đọc được");
        return;
    }
    format_bytes((uint64_t)(vqeaf_state.mem_total_kb -
                            vqeaf_state.mem_available_kb) * 1024u,
                 used, sizeof(used));
    format_bytes((uint64_t)vqeaf_state.mem_total_kb * 1024u,
                 total, sizeof(total));
    snprintf(out, cap, "%s / %s", used, total);
}

static void refresh_live_fields(void)
{
    char text[64];

    if(live_uptime) {
        format_uptime(vqeaf_state.uptime_s, text, sizeof(text));
        lv_label_set_text(live_uptime, text);
    }
    if(live_mem_pair) {
        memory_pair_text(text, sizeof(text));
        lv_label_set_text(live_mem_pair, text);
    }
    if(live_storage_pct) {
        if(vqeaf_state.storage_valid)
            snprintf(text, sizeof(text), "%d%%", vqeaf_state.storage_pct);
        else
            snprintf(text, sizeof(text), "--");
        lv_label_set_text(live_storage_pct, text);
        lv_obj_set_style_text_color(live_storage_pct,
            vqeaf_state.storage_valid && vqeaf_state.storage_pct >= 90 ?
            VQEAF_C_WARN : VQEAF_C_ACCENT, 0);
    }
    if(live_storage_pair) {
        storage_pair_text(text, sizeof(text));
        lv_label_set_text(live_storage_pair, text);
    }
    if(live_storage_bar) {
        lv_bar_set_value(live_storage_bar,
                         vqeaf_state.storage_valid ?
                         vqeaf_state.storage_pct : 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(live_storage_bar,
            vqeaf_state.storage_valid && vqeaf_state.storage_pct >= 90 ?
            VQEAF_C_WARN : VQEAF_C_ACCENT, LV_PART_INDICATOR);
    }
    if(live_battery) {
        battery_text(text, sizeof(text));
        lv_label_set_text(live_battery, text);
    }
    if(live_battery_pct) {
        if(vqeaf_state.battery_valid)
            snprintf(text, sizeof(text), "%d%%", vqeaf_state.battery_pct);
        else
            snprintf(text, sizeof(text), "--");
        lv_label_set_text(live_battery_pct, text);
        lv_obj_set_style_text_color(live_battery_pct,
            vqeaf_state.charging ? VQEAF_C_ACCENT : VQEAF_C_TEXT, 0);
    }
    if(live_usb) lv_label_set_text(live_usb, usb_mode_text());
    if(live_wifi) lv_label_set_text(live_wifi, wifi_text());
    if(live_brightness) {
        snprintf(text, sizeof(text), "%d%%", vqeaf_state.brightness);
        lv_label_set_text(live_brightness, text);
    }
}

void vqeaf_pages_refresh(void)
{
    if(page_clock) lv_label_set_text(page_clock, vqeaf_state.clock_text);
    if(page_date) lv_label_set_text(page_date, vqeaf_state.date_text);
    refresh_live_fields();
}

static void nav_cb(lv_event_t *event)
{
    vqeaf_page_t page = (vqeaf_page_t)(intptr_t)lv_event_get_user_data(event);
    vqeaf_ui_show(page);
}

static lv_obj_t *page_root(lv_obj_t *content)
{
    lv_obj_t *root = vqeaf_obj(content);
    lv_obj_set_size(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, VQEAF_PAD, 0);
    lv_obj_set_style_pad_row(root, 22, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    return root;
}

static lv_obj_t *page_title(lv_obj_t *parent, const char *title)
{
    lv_obj_t *row = vqeaf_row(parent);
    lv_obj_set_style_pad_top(row, 4, 0);
    lv_obj_t *label = vqeaf_label(row, title, VQEAF_FONT_HEADLINE_BOLD,
                                  VQEAF_C_TEXT);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    return row;
}

/* Detail pages carry their own back affordance; the nav bar keeps the four
 * primary destinations and never highlights a detail page. */
static lv_obj_t *detail_header(lv_obj_t *root, const char *title)
{
    lv_obj_t *row = vqeaf_row(root);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 18, 0);
    lv_obj_set_style_pad_top(row, 4, 0);

    lv_obj_t *back = vqeaf_tile(row, VQEAF_SYM_ARROW_BACK, VQEAF_C_SURFACE,
                                VQEAF_C_TEXT, 72, 24);
    vqeaf_on_click(back, nav_cb, (void *)(intptr_t)VQEAF_PAGE_SETTINGS);
    vqeaf_label(row, title, VQEAF_FONT_HEADLINE, VQEAF_C_TEXT);
    return row;
}

static void style_progress(lv_obj_t *bar)
{
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, 20);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, VQEAF_C_SURFACE_3, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, VQEAF_C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
}

/* One key/value line inside a card. Returns the value label so live providers
 * can rewrite it without rebuilding the page. */
static lv_obj_t *info_row(lv_obj_t *parent, const char *key,
                          const char *value)
{
    lv_obj_t *row = vqeaf_obj(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    vqeaf_label(row, key, VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
    lv_obj_t *val = vqeaf_label(row, value, VQEAF_FONT_BODY, VQEAF_C_TEXT);
    lv_obj_set_width(val, lv_pct(56));
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
    return val;
}

static void search_modal_close(void);

static lv_obj_t *g_search_modal;
static lv_obj_t *g_search_results;

/* QEAF-style input method: focusing a field asks the independent IME
 * window to show; the app content (if given) is resized by H (adjustResize).
 * Dialogs pass NULL and overlay instead (adjustNothing). */
static void ime_focus_cb(lv_event_t *event)
{
    lv_obj_t *ta = lv_event_get_target(event);
    lv_obj_t *content = (lv_obj_t *)lv_event_get_user_data(event);
    vqeaf_ime_show(ta, content);
}

static void ime_defocus_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_ime_hide();
}

static lv_obj_t *g_search_textarea;

static void search_modal_overlay_cb(lv_event_t *event)
{
    (void)event;
    search_modal_close();
}

static void search_modal_close(void)
{
    vqeaf_ime_hide();
    if(g_search_modal != NULL) {
        lv_obj_delete(g_search_modal);
        g_search_modal = NULL;
        g_search_textarea = NULL;
        g_search_results = NULL;
    }
}

static uint32_t search_utf8_next(const char **text)
{
    const unsigned char *p = (const unsigned char *)*text;
    uint32_t cp;
    if(*p < 0x80) {
        *text += 1;
        return *p;
    }
    if((*p & 0xE0) == 0xC0 && p[1] != 0) {
        cp = (uint32_t)(p[0] & 0x1F) << 6;
        cp |= (uint32_t)(p[1] & 0x3F);
        *text += 2;
        return cp;
    }
    if((*p & 0xF0) == 0xE0 && p[1] != 0 && p[2] != 0) {
        cp = (uint32_t)(p[0] & 0x0F) << 12;
        cp |= (uint32_t)(p[1] & 0x3F) << 6;
        cp |= (uint32_t)(p[2] & 0x3F);
        *text += 3;
        return cp;
    }
    if((*p & 0xF8) == 0xF0 && p[1] != 0 && p[2] != 0 && p[3] != 0) {
        cp = (uint32_t)(p[0] & 0x07) << 18;
        cp |= (uint32_t)(p[1] & 0x3F) << 12;
        cp |= (uint32_t)(p[2] & 0x3F) << 6;
        cp |= (uint32_t)(p[3] & 0x3F);
        *text += 4;
        return cp;
    }
    *text += 1;
    return 0xFFFD;
}

static char search_fold_codepoint(uint32_t cp)
{
    if((cp >= 0x00C0 && cp <= 0x00C5) ||
       (cp >= 0x00E0 && cp <= 0x00E5) || cp == 0x0102 || cp == 0x0103 ||
       (cp >= 0x1EA0 && cp <= 0x1EB7)) return 'a';
    if((cp >= 0x00C8 && cp <= 0x00CB) ||
       (cp >= 0x00E8 && cp <= 0x00EB) ||
       (cp >= 0x1EB8 && cp <= 0x1EC7)) return 'e';
    if((cp >= 0x00CC && cp <= 0x00CF) ||
       (cp >= 0x00EC && cp <= 0x00EF) ||
       (cp >= 0x0128 && cp <= 0x0131) ||
       (cp >= 0x1EC8 && cp <= 0x1ECB)) return 'i';
    if((cp >= 0x00D2 && cp <= 0x00D6) ||
       (cp >= 0x00F2 && cp <= 0x00F6) || cp == 0x01A0 || cp == 0x01A1 ||
       (cp >= 0x1ECC && cp <= 0x1EE3)) return 'o';
    if((cp >= 0x00D9 && cp <= 0x00DC) ||
       (cp >= 0x00F9 && cp <= 0x00FC) ||
       (cp >= 0x0168 && cp <= 0x0173) || cp == 0x01AF || cp == 0x01B0 ||
       (cp >= 0x1EE4 && cp <= 0x1EF1)) return 'u';
    if(cp == 0x00DD || cp == 0x00FD || cp == 0x00FF || cp == 0x0178 ||
       (cp >= 0x1EF2 && cp <= 0x1EF9)) return 'y';
    if(cp == 0x0110 || cp == 0x0111) return 'd';
    if(cp >= 'A' && cp <= 'Z') return (char)(cp + ('a' - 'A'));
    if(cp < 0x80 && ((cp >= 'a' && cp <= 'z') ||
                     (cp >= '0' && cp <= '9'))) return (char)cp;
    return 0;
}

static void search_normalize(const char *source, char *out, size_t capacity)
{
    size_t used = 0;
    int pending_space = 0;
    if(capacity == 0) return;
    while(source != NULL && *source != '\0') {
        uint32_t cp = search_utf8_next(&source);
        char folded = search_fold_codepoint(cp);
        if(folded != 0) {
            if(pending_space && used > 0 && used + 1 < capacity)
                out[used++] = ' ';
            pending_space = 0;
            if(used + 1 < capacity) out[used++] = folded;
        }
        else if(cp == ' ' || cp == '\t' || cp == '\n' || cp == '-' ||
                cp == '_' || cp == '.') {
            pending_space = 1;
        }
    }
    out[used] = '\0';
}

static const char *search_app_aliases(int app_id)
{
    static const char *const aliases[] = {
        "home launcher", "apps application", "control quick settings",
        "settings setup preferences", "phone call dialer", "messages sms",
        "contacts people", "browser web internet", "camera photo",
        "gallery photos", "music audio", "video movies", "files file",
        "installer package", "store qeaf catalog", "games game",
        "calculator math", "recorder voice", "calendar date",
        "notes note", "weather forecast", "security protection",
        "settings preferences", "clock alarm", "update upgrade",
        "storage disk", "about system info", "share", "tools utility"
    };
    if(app_id < 0 || app_id >= (int)(sizeof(aliases) / sizeof(aliases[0])))
        return "";
    return aliases[app_id];
}

static int search_text_matches(const char *name, const char *aliases,
                               const char *normalized_query)
{
    char normalized_name[128];
    char normalized_aliases[192];
    search_normalize(name, normalized_name, sizeof(normalized_name));
    search_normalize(aliases, normalized_aliases, sizeof(normalized_aliases));
    return strstr(normalized_name, normalized_query) != NULL ||
           strstr(normalized_aliases, normalized_query) != NULL;
}

static int search_is_android_store(const char *name, const char *package_id)
{
    char normalized[192];
    search_normalize(name, normalized, sizeof(normalized));
    if(strstr(normalized, "google") != NULL ||
       strstr(normalized, "play store") != NULL ||
       strstr(normalized, "playstore") != NULL) return 1;
    search_normalize(package_id, normalized, sizeof(normalized));
    return strstr(normalized, "google") != NULL ||
           strstr(normalized, "play store") != NULL ||
           strstr(normalized, "playstore") != NULL;
}

#define SEARCH_QEAPP_RESULT 0x10000

static void search_result_cb(lv_event_t *event)
{
    int result = (int)(intptr_t)lv_event_get_user_data(event);
    search_modal_close();
    if(result >= SEARCH_QEAPP_RESULT) {
        int index = result - SEARCH_QEAPP_RESULT;
        const vqeaf_qeapp_t *app = vqeaf_qeapp_at(index);
        if(app == NULL) return;
        vqeaf_qeapp_select(index);
        if(app->native) vqeaf_ui_run_app(index);
        else vqeaf_ui_show(VQEAF_PAGE_QEAPP);
        return;
    }
    const vqeaf_app_t *app = vqeaf_app_by_id(result);
    if(app != NULL && app->page != VQEAF_PAGE_COUNT)
        vqeaf_ui_show(app->page);
}

static void search_result_add(lv_obj_t *parent, const char *symbol,
                              const char *name, const char *detail,
                              uint32_t bg, uint32_t fg, int result,
                              int launchable)
{
    lv_obj_t *row = vqeaf_obj(parent);
    lv_obj_set_size(row, lv_pct(100), 72);
    lv_obj_set_style_bg_color(row, VQEAF_C_SURFACE_2, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 18, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, VQEAF_C_LINE, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, 12, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_t *tile = vqeaf_tile(row, symbol, lv_color_hex(fg),
                                lv_color_white(), 48, 14);
    vqeaf_app_icon_style(tile, lv_color_hex(fg), lv_color_hex(bg));
    lv_obj_t *text = vqeaf_column(row);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_style_pad_row(text, 1, 0);
    vqeaf_label(text, name, VQEAF_FONT_BODY, VQEAF_C_TEXT);
    vqeaf_label(text, detail, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    if(launchable) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, search_result_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)result);
        vqeaf_m3_ripple(row);
    }
}

static void search_results_refresh(void)
{
    const char *query = g_search_textarea != NULL ?
                        lv_textarea_get_text(g_search_textarea) : "";
    char normalized_query[128];
    int shown = 0;
    if(g_search_results == NULL) return;
    search_normalize(query, normalized_query, sizeof(normalized_query));
    lv_obj_clean(g_search_results);
    vqeaf_label(g_search_results, normalized_query[0] == '\0' ?
                "Gợi ý trên thiết bị" : "Kết quả trên thiết bị",
                VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);

    /* Search only the built-in registry and installed QEAF packages. No
     * network provider, Google service or Android package catalogue is used. */
    for(int i = 0; i < vqeaf_app_count(); i++) {
        const vqeaf_app_t *app = vqeaf_app_by_id(i);
        if(app == NULL || app->name == NULL) continue;
        if(normalized_query[0] != '\0' &&
           !search_text_matches(app->name, search_app_aliases(i),
                                normalized_query)) continue;
        if(normalized_query[0] == '\0' && shown >= 4) break;
        search_result_add(g_search_results, app->symbol, app->name,
            app->page == VQEAF_PAGE_COUNT ? "Chưa có trang hệ thống" :
                                           "Ứng dụng tích hợp",
            app->bg, app->fg, i, app->page != VQEAF_PAGE_COUNT);
        shown++;
    }
    for(int i = 0; i < vqeaf_qeapp_count(); i++) {
        const vqeaf_qeapp_t *app = vqeaf_qeapp_at(i);
        if(app == NULL || app->name[0] == '\0') continue;
        if(search_is_android_store(app->name, app->id)) continue;
        if(normalized_query[0] != '\0' &&
           !search_text_matches(app->name, app->id, normalized_query)) continue;
        if(normalized_query[0] == '\0' && shown >= 4) break;
        search_result_add(g_search_results, VQEAF_SYM_APPS, app->name,
            app->native ? "Gói QEAF · ứng dụng gốc" :
                          "Gói QEAF đã cài",
            0x1A3A5A, 0x6FA8FF, SEARCH_QEAPP_RESULT + i, 1);
        shown++;
    }
    if(shown == 0)
        vqeaf_label(g_search_results, "Không tìm thấy ứng dụng trên thiết bị.",
                    VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
}

static void search_value_changed_cb(lv_event_t *event)
{
    (void)event;
    search_results_refresh();
}

static void search_modal_open_cb(lv_event_t *event)
{
    (void)event;
    lv_display_t *display = lv_display_get_default();
    int width = (int)lv_display_get_horizontal_resolution(display);
    int height = (int)lv_display_get_vertical_resolution(display);
    if(g_search_modal != NULL) search_modal_close();
    g_search_modal = vqeaf_obj(lv_layer_top());
    lv_obj_set_size(g_search_modal, width, height);
    lv_obj_set_style_bg_color(g_search_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_search_modal, LV_OPA_70, 0);
    lv_obj_add_flag(g_search_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_search_modal, search_modal_overlay_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = vqeaf_obj(g_search_modal);
    lv_obj_set_size(card, width * 92 / 100, 620);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, height * 9 / 100);
    lv_obj_set_style_bg_color(card, VQEAF_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, VQEAF_C_LINE, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    if(vqeaf_theme_liquid_glass())
        vqeaf_glass_surface(card, 24, LV_OPA_90);

    lv_obj_t *heading = vqeaf_row(card);
    lv_obj_t *title = vqeaf_label(heading, "Tìm kiếm ứng dụng",
                                  VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_t *close = vqeaf_label(heading, "Đóng", VQEAF_FONT_BODY,
                                  VQEAF_C_ACCENT);
    vqeaf_on_click(close, search_modal_overlay_cb, NULL);

    g_search_textarea = lv_textarea_create(card);
    lv_obj_set_size(g_search_textarea, lv_pct(100), 64);
    lv_textarea_set_placeholder_text(g_search_textarea,
                                     "Tên ứng dụng, ví dụ: camera");
    lv_textarea_set_one_line(g_search_textarea, true);
    lv_obj_set_style_bg_color(g_search_textarea, VQEAF_C_SURFACE_2, 0);
    lv_obj_set_style_bg_opa(g_search_textarea, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_search_textarea, 20, 0);
    lv_obj_set_style_border_color(g_search_textarea, VQEAF_C_LINE, 0);
    lv_obj_set_style_border_width(g_search_textarea, 1, 0);
    lv_obj_set_style_text_font(g_search_textarea, VQEAF_FONT_BODY, 0);
    lv_obj_set_style_text_color(g_search_textarea, VQEAF_C_TEXT, 0);

    lv_obj_add_event_cb(g_search_textarea, ime_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(g_search_textarea, ime_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(g_search_textarea, search_value_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    vqeaf_label(card, "Tìm cục bộ trong ứng dụng tích hợp và gói QEAF đã cài. Không cần Internet.",
                VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    g_search_results = vqeaf_obj(card);
    lv_obj_set_width(g_search_results, lv_pct(100));
    lv_obj_set_flex_grow(g_search_results, 1);
    lv_obj_set_flex_flow(g_search_results, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_search_results, 8, 0);
    lv_obj_set_style_pad_right(g_search_results, 4, 0);
    lv_obj_set_scroll_dir(g_search_results, LV_DIR_VER);
    search_results_refresh();
    vqeaf_ime_show(g_search_textarea, NULL);
}

static lv_obj_t *search_pill(lv_obj_t *parent, const char *placeholder)
{
    lv_obj_t *pill = vqeaf_obj(parent);
    lv_obj_set_size(pill, lv_pct(100), 86);
    lv_obj_set_style_bg_color(pill, VQEAF_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pill, 43, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_border_color(pill, VQEAF_C_LINE, 0);
    if(vqeaf_theme_liquid_glass())
        vqeaf_glass_surface(pill, 43, (lv_opa_t)166);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(pill, 28, 0);
    lv_obj_set_style_pad_column(pill, 16, 0);

    vqeaf_glyph(pill, VQEAF_SYM_SEARCH, VQEAF_ICON_SM,
        vqeaf_theme_liquid_glass() ? lv_color_hex(0xFFF7FF) : VQEAF_C_TEXT_DIM);
    vqeaf_label(pill, placeholder, VQEAF_FONT_BODY,
        vqeaf_theme_liquid_glass() ? lv_color_hex(0xF4E8FA) : VQEAF_C_TEXT_MUTE);
    vqeaf_on_click(pill, search_modal_open_cb, NULL);
    return pill;
}

static lv_obj_t *section_row(lv_obj_t *parent, const char *title,
                             const char *action)
{
    lv_obj_t *row = vqeaf_row(parent);
    vqeaf_label(row, title, VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    if(action != NULL) {
        lv_obj_t *more = vqeaf_label(row, action, VQEAF_FONT_BODY,
                                     VQEAF_C_ACCENT);
        vqeaf_on_click(more, nav_cb, (void *)(intptr_t)VQEAF_PAGE_APPS);
    }
    return row;
}

/* ---- app tiles ----------------------------------------------------------- */

typedef vqeaf_app_t app_def_t;

/* The four system apps carried by the dock when no user drag has replaced
 * them. They stay launchable from the drawer too. */
static const vqeaf_app_t system_apps[] = {
    { VQEAF_SYM_HOME,      "Trang chủ",  0x0F3B36, 0x35E0C8, VQEAF_PAGE_HOME,     VQEAF_APP_ICON_HOME },
    { VQEAF_SYM_APPS,      "Ứng dụng",   0x1A3A5A, 0x6FA8FF, VQEAF_PAGE_APPS,     VQEAF_APP_ICON_APPS },
    { VQEAF_SYM_GRID_VIEW, "Điều khiển", 0x1D3A3D, 0x38C8E8, VQEAF_PAGE_CONTROL,  VQEAF_APP_ICON_CONTROL },
    { VQEAF_SYM_SETTINGS,  "Cài đặt",    0x1E2A2C, 0xB8CBCB, VQEAF_PAGE_SETTINGS, VQEAF_APP_ICON_SETTINGS },
};

/* Default dock: the four launchable apps the reference handset shows there.
 * Registry ids: 4 Phone, 5 Messages, 7 Browser, 9 Gallery (0..3 are the
 * Home/Apps/Control/Settings screens, still reachable from the drawer, the
 * status bar and the home bar). Drag-and-drop can still replace any slot. */
static int g_dock_apps[VQEAF_DOCK_SLOTS] = { 4, 5, 7, 9 };

/* The sheet is two siblings on the top layer: a full-screen scrim that closes
 * it and the card itself. They used to be one full-screen wrapper with the
 * card inside; on the handset that wrapper's own `coords` came back corrupted
 * (e.g. 0,20493528..2,8), and LVGL's hit-test only descends into children when
 * the point is inside the *parent's* coords - so every touch at the card landed
 * on the wrapper and the buttons never saw it, while the card's own coords
 * stayed correct. Keeping the card a direct child of the layer removes that
 * dependency: a touch at the card now walks straight to its buttons. */
static lv_obj_t *g_info_modal;   /* scrim */
static lv_obj_t *g_info_card;
static vqeaf_page_t g_info_page = VQEAF_PAGE_COUNT;
static int g_info_qeapp = -1;
static int g_info_app_id = -1;

static void app_info_close_cb(lv_event_t *event)
{
    (void)event;
    if(g_info_card != NULL) {
        lv_obj_delete(g_info_card);
        g_info_card = NULL;
    }
    if(g_info_modal != NULL) {
        lv_obj_delete(g_info_modal);
        g_info_modal = NULL;
    }
}

static void app_info_open_cb(lv_event_t *event)
{
    (void)event;
    app_info_close_cb(event);
    if(g_info_qeapp >= 0) {
        vqeaf_qeapp_select(g_info_qeapp);
        vqeaf_ui_show(VQEAF_PAGE_QEAPP);
    }
    else if(g_info_page != VQEAF_PAGE_COUNT) {
        vqeaf_ui_show(g_info_page);
    }
}

/* "Di chuyển"/"Thêm vào dock" arms the shell's dock move mode: the next dock
 * tap drops this app into that slot. The dock lives on Home only, so arming
 * from the app drawer first returns to the launcher to keep the drop target
 * visible, like an "add to home screen" action. This replaced long-press drag
 * so every long-press in the shell opens the info sheet. */
static void app_info_move_cb(lv_event_t *event)
{
    int app_id = g_info_app_id;
    app_info_close_cb(event);
    if(vqeaf_ui_page() != VQEAF_PAGE_HOME)
        vqeaf_ui_show(VQEAF_PAGE_HOME);
    vqeaf_ui_dock_move_arm(app_id);
}

static int default_role_for_app(int app_id);
static void show_toast(const char *message);

static void app_info_default_cb(lv_event_t *event)
{
    int app_id = g_info_app_id;
    int role = default_role_for_app(app_id);
    char sel[32];
    (void)event;
    if(role < 0) return;
    snprintf(sel, sizeof(sel), "app:%d", app_id);
    vqeaf_defaults_set(role, sel);
    app_info_close_cb(event);
    show_toast("Đã đặt làm ứng dụng mặc định");
}

static int app_in_dock(int app_id)
{
    for(int i = 0; i < VQEAF_DOCK_SLOTS; i++)
        if(vqeaf_dock_slot(i) == app_id) return 1;
    return 0;
}

/* Long-press app info sheet for both built-in apps and installed QEAPPs. */
int vqeaf_pages_app_info_visible(void)
{
    return g_info_modal != NULL;
}

static void app_info_show(const char *name, const char *kind,
                          const char *line1, const char *line2,
                          vqeaf_page_t page, int qeapp_index,
                          const lv_image_dsc_t *icon, int app_id)
{
    lv_display_t *display = lv_display_get_default();
    int width = (int)lv_display_get_horizontal_resolution(display);
    int height = (int)lv_display_get_vertical_resolution(display);

    g_info_page = page;
    g_info_qeapp = qeapp_index;
    g_info_app_id = app_id;
    if(g_info_card != NULL) {
        lv_obj_delete(g_info_card);
        g_info_card = NULL;
    }
    if(g_info_modal != NULL) {
        lv_obj_delete(g_info_modal);
        g_info_modal = NULL;
    }

    /* Scrim first so it stays below the card: a touch outside the card closes
     * the sheet instead of reaching the page behind it. */
    g_info_modal = vqeaf_obj(lv_layer_top());
    lv_obj_set_size(g_info_modal, width, height);
    lv_obj_set_style_bg_color(g_info_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_info_modal, LV_OPA_70, 0);
    lv_obj_add_flag(g_info_modal, LV_OBJ_FLAG_CLICKABLE);
    vqeaf_on_click(g_info_modal, app_info_close_cb, NULL);

    lv_obj_t *card = vqeaf_obj(lv_layer_top());
    g_info_card = card;
    lv_obj_set_size(card, width * 82 / 100, 430);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, VQEAF_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 30, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, VQEAF_C_LINE, 0);
    lv_obj_set_style_pad_all(card, 22, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 10, 0);

    if(vqeaf_icon_img(card, icon, 110) == NULL)
        vqeaf_tile(card, VQEAF_SYM_APPS, lv_color_hex(0x2A2E6E),
                   lv_color_hex(0xB6BCFF), 110, 32);
    vqeaf_label(card, name, VQEAF_FONT_HEADLINE, VQEAF_C_TEXT);
    vqeaf_label(card, kind, VQEAF_FONT_CAPTION, VQEAF_C_ACCENT);
    /* Single-line with an ellipsis: a wrapping label changes its own height
     * after the first layout, and on the handset that left the painted buttons
     * one layout above their touch areas (taps landed on the scrim instead).
     * The sheet keeps a stable height with dots. */
    lv_obj_t *l1 = vqeaf_label(card, line1, VQEAF_FONT_CAPTION,
                               VQEAF_C_TEXT_DIM);
    lv_obj_set_width(l1, lv_pct(100));
    lv_label_set_long_mode(l1, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(l1, LV_TEXT_ALIGN_CENTER, 0);
    if(line2 != NULL && line2[0] != '\0') {
        lv_obj_t *l2 = vqeaf_label(card, line2, VQEAF_FONT_CAPTION,
                                   VQEAF_C_TEXT_MUTE);
        lv_obj_set_width(l2, lv_pct(100));
        lv_label_set_long_mode(l2, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(l2, LV_TEXT_ALIGN_CENTER, 0);
    }

    lv_obj_t *buttons = vqeaf_obj(card);
    lv_obj_set_size(buttons, lv_pct(100), 70);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(buttons, 12, 0);
    lv_obj_t *close = vqeaf_obj(buttons);
    lv_obj_set_size(close, 136, 64);
    lv_obj_set_style_bg_color(close, VQEAF_C_SURFACE_3, 0);
    lv_obj_set_style_bg_opa(close, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(close, 32, 0);
    lv_obj_set_flex_flow(close, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(close, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    vqeaf_label(close, "Đóng", VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    vqeaf_on_click(close, app_info_close_cb, NULL);

    if(page != VQEAF_PAGE_COUNT || qeapp_index >= 0) {
        lv_obj_t *open = vqeaf_obj(buttons);
        lv_obj_set_size(open, 150, 64);
        lv_obj_set_style_bg_color(open, VQEAF_C_ACCENT, 0);
        lv_obj_set_style_bg_opa(open, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(open, 32, 0);
        lv_obj_set_flex_flow(open, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(open, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        /* Tapping an installed QEAPP already launches it, so this button is the
         * way to its detail page; a built-in app still needs "Mở" because the
         * button really opens it. */
        vqeaf_label(open, qeapp_index >= 0 ? "Thông tin" : "Mở",
                    VQEAF_FONT_CAPTION, VQEAF_C_BG_DEEP);
        vqeaf_on_click(open, app_info_open_cb, NULL);
    }

    if(app_id >= 0) {
        lv_obj_t *move = vqeaf_obj(buttons);
        lv_obj_set_size(move, 190, 64);
        lv_obj_set_style_bg_color(move, VQEAF_C_SURFACE_3, 0);
        lv_obj_set_style_bg_opa(move, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(move, 32, 0);
        lv_obj_set_style_border_width(move, 1, 0);
        lv_obj_set_style_border_color(move, VQEAF_C_ACCENT, 0);
        lv_obj_set_flex_flow(move, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(move, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        vqeaf_label(move, app_in_dock(app_id) ? "Di chuyển" : "Thêm vào dock",
                    VQEAF_FONT_CAPTION, VQEAF_C_ACCENT);
        vqeaf_on_click(move, app_info_move_cb, NULL);

        if(default_role_for_app(app_id) >= 0) {
            lv_obj_t *def = vqeaf_obj(buttons);
            lv_obj_set_size(def, 200, 64);
            lv_obj_set_style_bg_color(def, VQEAF_C_SURFACE_3, 0);
            lv_obj_set_style_bg_opa(def, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(def, 32, 0);
            lv_obj_set_style_border_width(def, 1, 0);
            lv_obj_set_style_border_color(def, VQEAF_C_ACCENT, 0);
            lv_obj_set_flex_flow(def, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(def, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            vqeaf_label(def, "Đặt mặc định", VQEAF_FONT_CAPTION,
                        VQEAF_C_ACCENT);
            lv_obj_add_event_cb(def, app_info_default_cb,
                                LV_EVENT_CLICKED, NULL);
        }
    }

    /* The message/kind labels are full-width and wrap, so their height - and
     * with it the row holding the buttons - is only final after a layout pass.
     * LVGL paints the sheet before that second pass, which left the drawn
     * buttons one single-line layout too high while their touch areas already
     * sat at the wrapped position: a tap on the pixels hit nothing (or the
     * scrim) and the sheet's buttons looked dead. Pin the layout now, then
     * invalidate so the first paint matches the hit boxes exactly. */
    lv_obj_update_layout(card);
    lv_obj_update_layout(card);   /* second pass: wrapped heights are final now */
    lv_obj_invalidate(lv_layer_top());
}

static void app_info_cb(lv_event_t *event)
{
    int app_id = (int)(intptr_t)lv_event_get_user_data(event);
    vqeaf_pages_app_info(app_id);
}

void vqeaf_pages_app_info(int app_id)
{
    const vqeaf_app_t *app = vqeaf_app_by_id(app_id);
    char line[128];

    if(app == NULL) return;
    if(app->page == VQEAF_PAGE_COUNT)
        snprintf(line, sizeof(line), "Chưa có trang nội dung trên ảnh này.");
    else
        snprintf(line, sizeof(line), "Màn hình nội bộ đã có backend.");
    app_info_show(app->name, "Ứng dụng hệ thống", line,
                  "không yêu cầu quyền", app->page, -1,
                  vqeaf_app_icon(app->icon), app_id);
}

static void qeapp_info_cb(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    const vqeaf_qeapp_t *app = vqeaf_qeapp_at(index);
    char line[160];

    if(app == NULL) return;
    snprintf(line, sizeof(line), "%s · v%s · đã ký QEAPP",
             app->id, app->version);
    const lv_image_dsc_t *icon = vqeaf_qeapp_icon(index);
    if(icon == NULL) icon = vqeaf_app_icon(VQEAF_APP_ICON_QEAPP);
    app_info_show(app->name, "QEAPP đã cài", line, app->message,
                  VQEAF_PAGE_COUNT, index, icon, -1);
}

static void app_open_cb(lv_event_t *event)
{
    vqeaf_page_t page = (vqeaf_page_t)(intptr_t)lv_event_get_user_data(event);
    vqeaf_ui_show(page);
}

static lv_obj_t *app_tile(lv_obj_t *parent, const app_def_t *app, int app_id)
{
    lv_obj_t *item = vqeaf_obj(parent);
    const lv_image_dsc_t *icon = vqeaf_app_icon(app->icon);
    lv_obj_set_size(item, 156, 176);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(item, 12, 0);

    if(vqeaf_theme_liquid_glass()) {
        lv_obj_t *glass = vqeaf_obj(item);
        lv_obj_set_size(glass, 112, 112);
        vqeaf_glass_surface(glass, 28, LV_OPA_50);
        if(icon != NULL) {
            lv_obj_t *image = vqeaf_icon_img(glass, icon, 88);
            lv_obj_center(image);
        }
        else {
            lv_obj_t *glyph = vqeaf_glyph(glass, app->symbol, VQEAF_ICON,
                                          lv_color_hex(0xFFF7FF));
            lv_obj_center(glyph);
        }
    }
    else {
        lv_obj_t *tile = vqeaf_icon_img(item, icon, 104);
        if(tile == NULL) {
            tile = vqeaf_tile(item, app->symbol, lv_color_hex(app->fg),
                              lv_color_white(), 104, 30);
            vqeaf_app_icon_style(tile, lv_color_hex(app->fg),
                                 lv_color_hex(app->bg));
        }
    }

    lv_obj_t *label = vqeaf_label(item, app->name, VQEAF_FONT_CAPTION,
        vqeaf_wallpaper_text_color());
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    /* Tap opens the page; long-press shows the app info sheet. SHORT_CLICKED
     * is suppressed after LONG_PRESSED on every indev type; CLICKED is not. */
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(item, app_info_cb, LV_EVENT_LONG_PRESSED,
                        (void *)(intptr_t)app_id);
    if(app->page != VQEAF_PAGE_COUNT)
        lv_obj_add_event_cb(item, app_open_cb, LV_EVENT_SHORT_CLICKED,
                            (void *)(intptr_t)app->page);
    return item;
}

static lv_obj_t *app_grid(lv_obj_t *parent, const app_def_t *apps, size_t count,
                          int first_id)
{
    lv_obj_t *grid = vqeaf_obj(parent);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 16, 0);
    lv_obj_set_style_pad_row(grid, 22, 0);
    for(size_t i = 0; i < count; i++)
        app_tile(grid, &apps[i], first_id + (int)i);
    return grid;
}

static const app_def_t home_apps[] = {
    { VQEAF_SYM_PHONE,        "Điện thoại", 0x0F3B36, 0x35E0C8, VQEAF_PAGE_COUNT, VQEAF_APP_ICON_PHONE },
    { VQEAF_SYM_MESSAGE,      "Tin nhắn",   0x123A44, 0x49C8E8, VQEAF_PAGE_COUNT, VQEAF_APP_ICON_MESSAGES },
    { VQEAF_SYM_CONTACTS,     "Danh bạ",    0x3A2A16, 0xF5B34A, VQEAF_PAGE_COUNT, VQEAF_APP_ICON_CONTACTS },
    { VQEAF_SYM_LANGUAGE,     "Trình duyệt",0x1A2A46, 0x6FA8FF, VQEAF_PAGE_BROWSER, VQEAF_APP_ICON_BROWSER },
    { VQEAF_SYM_CAMERA_ALT,   "Máy ảnh",    0x2C1B3E, 0xC79BFF, VQEAF_PAGE_COUNT, VQEAF_APP_ICON_CAMERA },
    { VQEAF_SYM_PHOTO_LIBRARY,"Thư viện",   0x3A1A2A, 0xFF8FB4, VQEAF_PAGE_GALLERY, VQEAF_APP_ICON_GALLERY },
    { VQEAF_SYM_MUSIC_NOTE,   "Nhạc",       0x103A26, 0x4BE08A, VQEAF_PAGE_MUSIC, VQEAF_APP_ICON_MUSIC },
    { VQEAF_SYM_MOVIE,        "Video",      0x3A1A1A, 0xFF7A7A, VQEAF_PAGE_VIDEO, VQEAF_APP_ICON_VIDEO },
};

static const app_def_t all_apps[] = {
    { VQEAF_SYM_PHONE,        "Điện thoại", 0x0F3B36, 0x35E0C8, VQEAF_PAGE_COUNT,    VQEAF_APP_ICON_PHONE },
    { VQEAF_SYM_MESSAGE,      "Tin nhắn",   0x123A44, 0x49C8E8, VQEAF_PAGE_COUNT,    VQEAF_APP_ICON_MESSAGES },
    { VQEAF_SYM_CONTACTS,     "Danh bạ",    0x3A2A16, 0xF5B34A, VQEAF_PAGE_COUNT,    VQEAF_APP_ICON_CONTACTS },
    { VQEAF_SYM_LANGUAGE,     "Trình duyệt",0x1A2A46, 0x6FA8FF, VQEAF_PAGE_BROWSER, VQEAF_APP_ICON_BROWSER },
    { VQEAF_SYM_CAMERA_ALT,   "Máy ảnh",    0x2C1B3E, 0xC79BFF, VQEAF_PAGE_COUNT,    VQEAF_APP_ICON_CAMERA },
    { VQEAF_SYM_PHOTO_LIBRARY,"Thư viện",   0x3A1A2A, 0xFF8FB4, VQEAF_PAGE_GALLERY,    VQEAF_APP_ICON_GALLERY },
    { VQEAF_SYM_MUSIC_NOTE,   "Nhạc",       0x103A26, 0x4BE08A, VQEAF_PAGE_MUSIC,    VQEAF_APP_ICON_MUSIC },
    { VQEAF_SYM_MOVIE,        "Video",      0x3A1A1A, 0xFF7A7A, VQEAF_PAGE_VIDEO,    VQEAF_APP_ICON_VIDEO },
    { VQEAF_SYM_FOLDER,       "Tệp",        0x3A2E14, 0xF3C64E, VQEAF_PAGE_FILES,    VQEAF_APP_ICON_FILES },
    { VQEAF_SYM_DOWNLOAD,     "Trình cài đặt", 0x1A2A46, 0x6FA8FF, VQEAF_PAGE_INSTALLER, VQEAF_APP_ICON_QEAPP },
    { VQEAF_SYM_STOREFRONT,   "Chợ ứng dụng",0x0F3B36, 0x35E0C8, VQEAF_PAGE_QEAPP_STORE, VQEAF_APP_ICON_STORE },
    { VQEAF_SYM_SPORTS_ESPORTS,"Trò chơi",  0x2A1A3E, 0xB48CFF, VQEAF_PAGE_COUNT,    VQEAF_APP_ICON_GAMES },
    { VQEAF_SYM_CALCULATE,    "Máy tính",   0x1A2A46, 0x6FA8FF, VQEAF_PAGE_COUNT,    VQEAF_APP_ICON_CALCULATOR },
    { VQEAF_SYM_MIC,          "Ghi âm",     0x3A1A2A, 0xFF8FB4, VQEAF_PAGE_COUNT,    VQEAF_APP_ICON_RECORDER },
    { VQEAF_SYM_CALENDAR_TODAY,"Lịch",      0x3A1A1A, 0xFF7A7A, VQEAF_PAGE_COUNT,    VQEAF_APP_ICON_CALENDAR },
    { VQEAF_SYM_NOTE,         "Ghi chú",    0x3A2E14, 0xF3C64E, VQEAF_PAGE_NOTES,    VQEAF_APP_ICON_NOTES },
    { VQEAF_SYM_WB_SUNNY,     "Thời tiết",  0x123A44, 0x49C8E8, VQEAF_PAGE_COUNT,    VQEAF_APP_ICON_WEATHER },
    { VQEAF_SYM_SECURITY,     "Bảo mật",    0x0F3B36, 0x35E0C8, VQEAF_PAGE_SECURITY, VQEAF_APP_ICON_SECURITY },
    { VQEAF_SYM_SETTINGS,     "Cài đặt",    0x1E2A2C, 0xB8CBCB, VQEAF_PAGE_SETTINGS, VQEAF_APP_ICON_SETTINGS },
    { VQEAF_SYM_SCHEDULE,     "Đồng hồ",    0x2C1B3E, 0xC79BFF, VQEAF_PAGE_COUNT,    VQEAF_APP_ICON_CLOCK },
    { VQEAF_SYM_SYSTEM_UPDATE,"Cập nhật",   0x1A2A46, 0x6FA8FF, VQEAF_PAGE_COUNT,    VQEAF_APP_ICON_UPDATE },
    { VQEAF_SYM_STORAGE,      "Lưu trữ",    0x3A2E14, 0xF3C64E, VQEAF_PAGE_COUNT,    VQEAF_APP_ICON_STORAGE },
    { VQEAF_SYM_INFO,         "Thông tin",  0x123A44, 0x49C8E8, VQEAF_PAGE_COUNT,    VQEAF_APP_ICON_ABOUT },
    { VQEAF_SYM_SHARE,        "Chia sẻ",    0x2A1A3E, 0xB48CFF, VQEAF_PAGE_COUNT,    VQEAF_APP_ICON_SHARE },
    { VQEAF_SYM_BUILD,        "Công cụ",    0x1E2A2C, 0xB8CBCB, VQEAF_PAGE_COUNT,    VQEAF_APP_ICON_TOOLS },
    { VQEAF_SYM_KEYBOARD,     "Terminal",     0x0F2A24, 0x7758,    VQEAF_PAGE_TERMINAL, VQEAF_APP_ICON_TOOLS },
    { VQEAF_SYM_FOLDER,       "QL tệp",       0x3A2E14, 0xF3C64E, VQEAF_PAGE_FILES,    VQEAF_APP_ICON_FILES },
    { VQEAF_SYM_SEARCH,       "Tìm NTFS",     0x1A2A46, 0x6FA8FF, VQEAF_PAGE_NTFS_SEARCH, VQEAF_APP_ICON_STORAGE },
    { VQEAF_SYM_MEMORY,       "Quản lý tác vụ",0x2C1B3E, 0xC79BFF, VQEAF_PAGE_TASK_MANAGER, VQEAF_APP_ICON_TOOLS },
    { VQEAF_SYM_DELETE_SWEEP, "Dọn dẹp ổ đĩa", 0x1E2A2C, 0xB8CBCB, VQEAF_PAGE_DISK_CLEANUP, VQEAF_APP_ICON_STORAGE },
};

static const vqeaf_app_t *const app_registry[] = {
    &system_apps[0], &system_apps[1], &system_apps[2], &system_apps[3],
    &all_apps[0], &all_apps[1], &all_apps[2], &all_apps[3],
    &all_apps[4], &all_apps[5], &all_apps[6], &all_apps[7],
    &all_apps[8], &all_apps[9], &all_apps[10], &all_apps[11],
    &all_apps[12], &all_apps[13], &all_apps[14], &all_apps[15],
    &all_apps[16], &all_apps[17], &all_apps[18], &all_apps[19],
    &all_apps[20], &all_apps[21], &all_apps[22], &all_apps[23],
    &all_apps[24], &all_apps[25], &all_apps[26], &all_apps[27],
    &all_apps[28], &all_apps[29],
};

/* App drawer paging state; RAM-only like the dock. */
static int g_apps_page;

int vqeaf_pages_apps_page(void)
{
    return g_apps_page;
}

int vqeaf_pages_apps_page_count(void)
{
    int count = (int)(sizeof(all_apps) / sizeof(all_apps[0]));
    return (count + VQEAF_APPS_PER_PAGE - 1) / VQEAF_APPS_PER_PAGE;
}

void vqeaf_pages_apps_page_set(int page)
{
    int last = vqeaf_pages_apps_page_count() - 1;
    if(page < 0) page = 0;
    if(page > last) page = last;
    g_apps_page = page;
}

const vqeaf_app_t *vqeaf_app_by_id(int id)
{
    if(id < 0 || id >= (int)(sizeof(app_registry) / sizeof(app_registry[0])))
        return NULL;
    return app_registry[id];
}

int vqeaf_app_count(void)
{
    return (int)(sizeof(app_registry) / sizeof(app_registry[0]));
}

int vqeaf_dock_slot(int slot)
{
    if(slot < 0 || slot >= VQEAF_DOCK_SLOTS) return -1;
    return g_dock_apps[slot];
}

void vqeaf_dock_move(int from, int to)
{
    int tmp;
    if(from < 0 || from >= VQEAF_DOCK_SLOTS ||
       to < 0 || to >= VQEAF_DOCK_SLOTS || from == to) return;
    tmp = g_dock_apps[from];
    g_dock_apps[from] = g_dock_apps[to];
    g_dock_apps[to] = tmp;
}

/* Drop swaps with the occupant; a second copy of the dragged app is removed
 * from its old slot so the dock never holds the same app twice. */
void vqeaf_dock_drop(int app_id, int slot)
{
    int old;
    if(app_id < 0 || app_id >= vqeaf_app_count()) return;
    if(slot < 0 || slot >= VQEAF_DOCK_SLOTS) return;
    old = g_dock_apps[slot];
    for(int i = 0; i < VQEAF_DOCK_SLOTS; i++)
        if(g_dock_apps[i] == app_id) g_dock_apps[i] = old;
    g_dock_apps[slot] = app_id;
}

void vqeaf_dock_clear(int slot)
{
    if(slot < 0 || slot >= VQEAF_DOCK_SLOTS) return;
    g_dock_apps[slot] = -1;
}

static const app_def_t dock_apps[] = {
    { VQEAF_SYM_BOLT,       "VQEAF",  0x0F3B36, 0x35E0C8, VQEAF_PAGE_HOME,     VQEAF_APP_ICON_VQEAF },
    { VQEAF_SYM_SETTINGS,   "Cài đặt",0x1E2A2C, 0xB8CBCB, VQEAF_PAGE_SETTINGS, VQEAF_APP_ICON_SETTINGS },
    { VQEAF_SYM_PHOTO_LIBRARY, "Thư viện", 0x3A1A2A, 0xFF8FB4, VQEAF_PAGE_GALLERY, VQEAF_APP_ICON_GALLERY },
    { VQEAF_SYM_STOREFRONT, "Chợ ứng dụng", 0x1A2A46, 0x6FA8FF, VQEAF_PAGE_QEAPP_STORE, VQEAF_APP_ICON_STORE },
};

static void home_dock_info_cb(lv_event_t *event)
{
    int i = (int)(intptr_t)lv_event_get_user_data(event);
    const app_def_t *app = &dock_apps[i];
    app_info_show(app->name, "Ứng dụng hệ thống",
                  app->page != VQEAF_PAGE_COUNT
                      ? "Màn hình nội bộ đã có backend."
                      : "Chưa có trang nội dung trên ảnh này.",
                  "không yêu cầu quyền", app->page, -1,
                  vqeaf_app_icon(app->icon), -1);
}

static void build_dock(lv_obj_t *parent)
{
    lv_obj_t *dock = vqeaf_obj(parent);
    lv_obj_set_size(dock, lv_pct(100), 148);
    lv_obj_set_style_bg_color(dock, VQEAF_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(dock, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dock, 0, 0);
    lv_obj_set_style_shadow_width(dock, 0, 0);
    lv_obj_set_style_shadow_opa(dock, LV_OPA_0, 0);
    lv_obj_set_flex_flow(dock, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dock, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for(size_t i = 0; i < sizeof(dock_apps) / sizeof(dock_apps[0]); i++) {
        const app_def_t *app = &dock_apps[i];
        lv_obj_t *item = vqeaf_obj(dock);
        lv_obj_set_size(item, 140, 144);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(item, 8, 0);
        if(vqeaf_theme_liquid_glass()) {
            lv_obj_t *glass = vqeaf_obj(item);
            lv_obj_set_size(glass, 104, 104);
            vqeaf_glass_surface(glass, 30, LV_OPA_50);
            lv_obj_t *image = vqeaf_icon_img(glass,
                                             vqeaf_app_icon(app->icon), 104);
            if(image != NULL) lv_obj_center(image);
            else {
                lv_obj_t *glyph = vqeaf_glyph(glass, app->symbol,
                    VQEAF_ICON, lv_color_hex(0xFFF7FF));
                lv_obj_center(glyph);
            }
        }
        else {
            lv_obj_t *tile = vqeaf_icon_img(item,
                                            vqeaf_app_icon(app->icon), 104);
            if(tile == NULL) {
                tile = vqeaf_tile(item, app->symbol, lv_color_hex(app->fg),
                                  lv_color_white(), 104, 30);
                vqeaf_app_icon_style(tile, lv_color_hex(app->fg),
                                     lv_color_hex(app->bg));
            }
        }
        lv_obj_t *label = vqeaf_label(item, app->name, VQEAF_FONT_CAPTION,
            vqeaf_wallpaper_text_color());
        lv_obj_set_style_text_font(label, VQEAF_FONT_CAPTION, 0);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(item, home_dock_info_cb, LV_EVENT_LONG_PRESSED,
                            (void *)(intptr_t)i);
        if(app->page != VQEAF_PAGE_COUNT)
            lv_obj_add_event_cb(item, app_open_cb, LV_EVENT_SHORT_CLICKED,
                                (void *)(intptr_t)app->page);
    }
}

/* ---- lock ---------------------------------------------------------------- */

/* Honest "no backend" feedback: the shell says what it cannot do instead of
 * flipping a fake switch. */
static void toast_action_cb(lv_event_t *event)
{
    lv_obj_t *bar = (lv_obj_t *)lv_event_get_user_data(event);
    if(bar != NULL) lv_obj_delete(bar);
}

static void show_toast(const char *message)
{
    lv_obj_t *action = NULL;
    lv_obj_t *bar = vqeaf_m3_snackbar(lv_layer_top(), message, "OK", &action);
    if(action != NULL)
        lv_obj_add_event_cb(action, toast_action_cb, LV_EVENT_CLICKED, bar);
}

/* ---- PIN lock -------------------------------------------------------------
 * The PIN lives in the VQEAF settings service, so it is one source of truth
 * shared by the lock screen and the Security page. On violet there is no
 * durable backend, so the store is RAM-only for the session: rebooting clears
 * it, which is the truthful behaviour of this read-only initramfs. */
#define PIN_SETTING_KEY "lock.pin"

static int pin_enabled(void)
{
    const char *pin = vqeaf_settings_get_str(PIN_SETTING_KEY, "");
    return pin != NULL && pin[0] != '\0';
}

static size_t pin_length(void)
{
    const char *pin = vqeaf_settings_get_str(PIN_SETTING_KEY, "");
    return pin != NULL ? strlen(pin) : 0;
}

static int pin_matches(const char *entered)
{
    const char *pin = vqeaf_settings_get_str(PIN_SETTING_KEY, "");
    return pin != NULL && entered != NULL && strcmp(pin, entered) == 0;
}

static void pin_store(const char *pin)
{
    vqeaf_settings_set_str(PIN_SETTING_KEY,
                           pin != NULL ? pin : "");
}

int vqeaf_lock_pin_enabled(void) { return pin_enabled(); }

void vqeaf_lock_unlock(void)
{
    if(pin_enabled())
        vqeaf_ui_show(VQEAF_PAGE_PIN);
    else
        vqeaf_ui_show(VQEAF_PAGE_HOME);
}

static void lock_torch_cb(lv_event_t *event)
{
    (void)event;
    show_toast("Máy không có đèn pin điều khiển được.");
}

static void lock_camera_cb(lv_event_t *event)
{
    (void)event;
    show_toast("Máy ảnh không có trên thiết bị này.");
}

static lv_obj_t *lock_weather_chip;

/* Lock-screen clock style. The editor changes these three values and the next
 * lock build uses them; the state is RAM-only until prefs exist on violet. */
typedef struct {
    const char *name;
    const lv_font_t *font;
} lock_font_t;

static const lock_font_t lock_fonts[] = {
    { "Lumen",  &vqeaf_font_bold_96 },
    { "Cao",    &vqeaf_font_bold_56 },
    { "Gọn",    &vqeaf_font_34 },
    { "Nhỏ",    &vqeaf_font_28 },
};

static const uint32_t lock_colors[] = {
    0xEAF7F5, 0xFFF1D6, 0xFFFFFF, 0xF2E3C6,
    0xFFB020, 0xFF7A59, 0xFF8FB4,
};

static int g_lock_font;
/* Zero tracks the current mode: dark ink in Light, pale ink in Dark. */
static uint32_t g_lock_color;
static int g_lock_weight = 100;

static uint32_t lock_clock_rgb(void)
{
    if(g_lock_color != 0) return g_lock_color;
    return vqeaf_theme_get() == VQEAF_THEME_LIGHT ? 0x1C1C1E : 0xEAF7F5;
}

static void apply_lock_clock_style(lv_obj_t *label)
{
    if(label == NULL) return;
    lv_obj_set_style_text_font(label, lock_fonts[g_lock_font].font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(lock_clock_rgb()), 0);
    lv_obj_set_style_opa(label, (lv_opa_t)(g_lock_weight * 255 / 100), 0);
}

static lv_obj_t *lock_stat_cell(lv_obj_t *parent, const char *symbol,
                                const char *value, const char *caption)
{
    lv_obj_t *cell = vqeaf_column(parent);
    lv_obj_set_width(cell, 170);
    lv_obj_set_style_pad_row(cell, 2, 0);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    vqeaf_glyph(cell, symbol, VQEAF_ICON_SM,
        vqeaf_theme_liquid_glass() ? lv_color_hex(0xFFE9FF) : VQEAF_C_ACCENT);
    vqeaf_label(cell, value, VQEAF_FONT_BODY,
        vqeaf_theme_liquid_glass() ? lv_color_hex(0xFFFFFF) : VQEAF_C_TEXT);
    vqeaf_label(cell, caption, VQEAF_FONT_CAPTION,
        vqeaf_theme_liquid_glass() ? lv_color_hex(0xF2DDF9) : VQEAF_C_TEXT_MUTE);
    return cell;
}

static lv_obj_t *lock_round_button(lv_obj_t *parent, const char *symbol,
                                   const char *caption, lv_event_cb_t cb)
{
    lv_obj_t *col = vqeaf_column(parent);
    lv_obj_set_width(col, 130);
    lv_obj_set_style_pad_row(col, 6, 0);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *btn = vqeaf_obj(col);
    lv_obj_set_size(btn, 84, 84);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn,
        vqeaf_theme_get() == VQEAF_THEME_LIGHT ? VQEAF_C_SURFACE :
                                                  lv_color_hex(0x0E2A2C), 0);
    lv_obj_set_style_bg_opa(btn,
        vqeaf_theme_get() == VQEAF_THEME_LIGHT ? LV_OPA_90 : LV_OPA_70, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, VQEAF_C_LINE, 0);
    if(vqeaf_theme_liquid_glass())
        vqeaf_glass_surface(btn, 42, LV_OPA_50);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    vqeaf_glyph(btn, symbol, VQEAF_ICON, VQEAF_C_TEXT);
    vqeaf_on_click(btn, cb, NULL);
    vqeaf_m3_ripple(btn);

    vqeaf_label(col, caption, VQEAF_FONT_CAPTION,
        vqeaf_theme_liquid_glass() ? lv_color_hex(0xF2DDF9) : VQEAF_C_TEXT_DIM);
    return col;
}

static void unlock_click_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_lock_unlock();
}

static void build_lock(lv_obj_t *root)
{
    lv_obj_t *wall = root;
    /* The content box was just resized, but LVGL has not recomputed its
     * coordinates yet, so read the height from the display instead. */
    lv_display_t *display = lv_obj_get_display(wall);
    int32_t height = (int32_t)lv_display_get_vertical_resolution(display);
    int32_t top = height < 900 ? height * 8 / 100 : height * 14 / 100;

    vqeaf_wallpaper(wall);

    /* One flex column carries the lock glyph, clock, date, the real status
     * widget and the newest notification, so the composition cannot overlap
     * itself in portrait or landscape. */
    lv_obj_t *stack = vqeaf_obj(wall);
    lv_obj_set_width(stack, lv_pct(100));
    lv_obj_set_height(stack, LV_SIZE_CONTENT);
    lv_obj_set_pos(stack, 0, top);
    lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(stack, height < 900 ? 12 : 18, 0);

    vqeaf_glyph(stack, VQEAF_SYM_LOCK, VQEAF_ICON_SM, VQEAF_C_ACCENT);
    page_clock = vqeaf_label(stack, vqeaf_state.clock_text,
                             VQEAF_FONT_CLOCK, VQEAF_C_TEXT);
    apply_lock_clock_style(page_clock);
    page_date = vqeaf_label(stack, vqeaf_state.date_text,
        VQEAF_FONT_BODY, vqeaf_theme_liquid_glass() ?
        lv_color_hex(0xF2DDF9) : VQEAF_C_TEXT_DIM);

    /* Device widget: only values the kernel actually reports. There is no
     * weather provider on this initramfs, so no invented temperature. */
    lock_weather_chip = vqeaf_obj(stack);
    lv_obj_set_size(lock_weather_chip, 600, 118);
    lv_obj_set_style_bg_color(lock_weather_chip,
        vqeaf_theme_get() == VQEAF_THEME_LIGHT ? VQEAF_C_SURFACE :
                                                  lv_color_hex(0x0E2A2C), 0);
    lv_obj_set_style_bg_opa(lock_weather_chip,
        vqeaf_theme_get() == VQEAF_THEME_LIGHT ? LV_OPA_90 : LV_OPA_70, 0);
    lv_obj_set_style_radius(lock_weather_chip, 34, 0);
    lv_obj_set_style_border_width(lock_weather_chip, 1, 0);
    lv_obj_set_style_border_color(lock_weather_chip, VQEAF_C_LINE, 0);
    if(vqeaf_theme_liquid_glass())
        vqeaf_glass_surface(lock_weather_chip, 34, LV_OPA_50);
    lv_obj_set_flex_flow(lock_weather_chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lock_weather_chip, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    {
        char value[32];
        if(vqeaf_state.battery_valid)
            snprintf(value, sizeof(value), "%d%%", vqeaf_state.battery_pct);
        else
            snprintf(value, sizeof(value), "--");
        lock_stat_cell(lock_weather_chip,
                       vqeaf_state.charging ? VQEAF_SYM_BATTERY_CHARGING_FULL :
                       VQEAF_SYM_BATTERY_FULL, value,
                       vqeaf_state.charging ? "Đang sạc" : "Pin");
        lock_stat_cell(lock_weather_chip,
                       vqeaf_state.wifi_up > 0 ? VQEAF_SYM_WIFI :
                       VQEAF_SYM_SIGNAL_WIFI_OFF,
                       vqeaf_state.wifi_up > 0 ? "Bật" :
                       vqeaf_state.wifi_up == 0 ? "Tắt" : "--", "Wi-Fi");
        if(vqeaf_state.storage_valid)
            snprintf(value, sizeof(value), "%d%%", vqeaf_state.storage_pct);
        else
            snprintf(value, sizeof(value), "--");
        lock_stat_cell(lock_weather_chip, VQEAF_SYM_STORAGE, value, "Lưu trữ");
    }

    /* Newest real event from the shell's notification log. */
    {
        const vqeaf_notification_t *note = vqeaf_ui_notification_at(0);
        lv_obj_t *card = vqeaf_obj(stack);
        lv_obj_set_size(card, 600, 128);
        lv_obj_set_style_bg_color(card,
            vqeaf_theme_get() == VQEAF_THEME_LIGHT ? VQEAF_C_SURFACE :
                                                      lv_color_hex(0x0E2A2C), 0);
        lv_obj_set_style_bg_opa(card,
            vqeaf_theme_get() == VQEAF_THEME_LIGHT ? LV_OPA_90 : LV_OPA_60, 0);
        lv_obj_set_style_radius(card, 30, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, VQEAF_C_LINE, 0);
        if(vqeaf_theme_liquid_glass())
            vqeaf_glass_surface(card, 30, LV_OPA_50);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(card, 16, 0);
        lv_obj_set_style_pad_left(card, 20, 0);
        lv_obj_set_style_pad_right(card, 20, 0);

        if(note != NULL) {
            vqeaf_tile(card, note->symbol[0] ? note->symbol :
                       VQEAF_SYM_NOTIFICATIONS,
                       lv_color_hex(note->color), VQEAF_C_BG, 56, 18);
            lv_obj_t *text = vqeaf_column(card);
            lv_obj_set_flex_grow(text, 1);
            lv_obj_set_style_pad_row(text, 2, 0);
            vqeaf_label(text, note->title, VQEAF_FONT_BODY, VQEAF_C_TEXT);
            lv_obj_t *body = vqeaf_label(text, note->body,
                                         VQEAF_FONT_CAPTION,
                vqeaf_theme_liquid_glass() ? lv_color_hex(0xF2DDF9) :
                                              VQEAF_C_TEXT_DIM);
            lv_obj_set_width(body, lv_pct(100));
            lv_label_set_long_mode(body, LV_LABEL_LONG_DOT);
            char ago[32];
            format_uptime(note->uptime_s, ago, sizeof(ago));
            vqeaf_label(card, ago, VQEAF_FONT_CAPTION,
                vqeaf_theme_liquid_glass() ? lv_color_hex(0xE8D4F2) :
                                              VQEAF_C_TEXT_MUTE);
        }
        else {
            vqeaf_tile(card, VQEAF_SYM_NOTIFICATIONS, VQEAF_C_SURFACE_3,
                       VQEAF_C_TEXT_DIM, 56, 18);
            lv_obj_t *text = vqeaf_column(card);
            lv_obj_set_style_pad_row(text, 2, 0);
            vqeaf_label(text, "Không có thông báo", VQEAF_FONT_BODY,
                        VQEAF_C_TEXT);
            vqeaf_label(text, "Sự kiện thật của hệ thống sẽ xuất hiện ở đây.",
                        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
        }
    }

    /* Bottom corners: the two hardware affordances a lock screen normally
     * offers. Neither exists on this device, and the toast says so instead of
     * opening a dead camera or toggling a flashlight that is not wired. */
    lv_obj_t *torch = lock_round_button(wall, VQEAF_SYM_FLASHLIGHT_ON,
                                        "Đèn pin", lock_torch_cb);
    lv_obj_align(torch, LV_ALIGN_BOTTOM_LEFT, 36, -104);
    lv_obj_t *camera = lock_round_button(wall, VQEAF_SYM_CAMERA_ALT,
                                         "Máy ảnh", lock_camera_cb);
    lv_obj_align(camera, LV_ALIGN_BOTTOM_RIGHT, -36, -104);

    lv_obj_t *hint = vqeaf_obj(wall);
    lv_obj_set_size(hint, 420, 84);
    lv_obj_set_style_bg_color(hint,
        vqeaf_theme_get() == VQEAF_THEME_LIGHT ? VQEAF_C_SURFACE :
                                                  lv_color_hex(0x0E2A2C), 0);
    lv_obj_set_style_bg_opa(hint,
        vqeaf_theme_get() == VQEAF_THEME_LIGHT ? LV_OPA_90 : LV_OPA_60, 0);
    lv_obj_set_style_radius(hint, 42, 0);
    lv_obj_set_style_border_width(hint, 1, 0);
    lv_obj_set_style_border_color(hint, VQEAF_C_LINE, 0);
    if(vqeaf_theme_liquid_glass())
        vqeaf_glass_surface(hint, 42, LV_OPA_50);
    lv_obj_set_flex_flow(hint, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hint, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hint, 10, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -30);
    vqeaf_label(hint, pin_enabled() ? "Nhấn để nhập PIN" :
                "Vuốt để mở khóa", VQEAF_FONT_BODY, VQEAF_C_TEXT);
    vqeaf_glyph(hint, VQEAF_SYM_CHEVRON_RIGHT, VQEAF_ICON_SM, VQEAF_C_ACCENT);
    lv_obj_add_event_cb(hint, unlock_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(wall, unlock_click_cb, LV_EVENT_CLICKED, NULL);
}

/* ---- home ---------------------------------------------------------------- */

static lv_obj_t *home_stat_cell(lv_obj_t *parent, lv_obj_t **value_out,
                               const char *caption)
{
    lv_obj_t *cell = vqeaf_column(parent);
    lv_obj_set_flex_grow(cell, 1);
    lv_obj_set_style_pad_row(cell, 2, 0);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    *value_out = vqeaf_label(cell, "--", VQEAF_FONT_TITLE,
        vqeaf_theme_liquid_glass() ? lv_color_hex(0xFFFFFF) : VQEAF_C_TEXT);
    vqeaf_label(cell, caption, VQEAF_FONT_CAPTION,
        vqeaf_theme_liquid_glass() ? lv_color_hex(0xF2DDF9) : VQEAF_C_TEXT_MUTE);
    return cell;
}

static void build_home(lv_obj_t *content)
{
    vqeaf_wallpaper(content);
    lv_obj_t *root = page_root(content);

    lv_obj_t *header = vqeaf_row(root);
    lv_obj_set_style_pad_top(header, 8, 0);

    lv_obj_t *when = vqeaf_column(header);
    lv_obj_set_width(when, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(when, 0, 0);
    page_clock = vqeaf_label(when, vqeaf_state.clock_text,
                             VQEAF_FONT_HOME_CLOCK, VQEAF_C_TEXT);
    page_date = vqeaf_label(when, vqeaf_state.date_text,
        VQEAF_FONT_CAPTION, vqeaf_theme_liquid_glass() ?
        lv_color_hex(0xF2DDF9) : VQEAF_C_TEXT_DIM);

    /* Battery chip: a real power_supply reading, not a weather placeholder. */
    lv_obj_t *battery = vqeaf_obj(header);
    lv_obj_set_size(battery, 210, 112);
    lv_obj_set_style_bg_color(battery, VQEAF_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(battery, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(battery, 28, 0);
    lv_obj_set_style_border_width(battery, 1, 0);
    lv_obj_set_style_border_color(battery, VQEAF_C_LINE, 0);
    if(vqeaf_theme_liquid_glass())
        vqeaf_glass_surface(battery, 28, LV_OPA_60);
    lv_obj_set_flex_flow(battery, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(battery, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(battery, 2, 0);
    vqeaf_glyph(battery,
                vqeaf_state.charging ? VQEAF_SYM_BATTERY_CHARGING_FULL :
                VQEAF_SYM_BATTERY_FULL, VQEAF_ICON_SM,
                vqeaf_theme_liquid_glass() ? lv_color_hex(0xFFE9FF) :
                                              VQEAF_C_ACCENT);
    live_battery_pct = vqeaf_label(battery, "--", VQEAF_FONT_TITLE,
        vqeaf_theme_liquid_glass() ? lv_color_hex(0xFFFFFF) : VQEAF_C_ACCENT);
    vqeaf_label(battery, "Pin", VQEAF_FONT_CAPTION,
        vqeaf_theme_liquid_glass() ? lv_color_hex(0xF2DDF9) : VQEAF_C_TEXT_MUTE);
    vqeaf_on_click(battery, nav_cb, (void *)(intptr_t)VQEAF_PAGE_CONTROL);
    vqeaf_m3_ripple(battery);

    /* Live system widget: uptime, RAM and storage from the kernel. */
    lv_obj_t *widget = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
    if(vqeaf_theme_liquid_glass())
        vqeaf_glass_surface(widget, 28, LV_OPA_60);
    lv_obj_set_flex_flow(widget, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(widget, 14, 0);
    lv_obj_t *whead = vqeaf_row(widget);
    vqeaf_label(whead, "Tiện ích hệ thống", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    lv_obj_t *edit = vqeaf_label(whead, "Sửa", VQEAF_FONT_BODY,
        vqeaf_theme_liquid_glass() ? lv_color_hex(0xF6CEFF) : VQEAF_C_ACCENT);
    vqeaf_on_click(edit, nav_cb, (void *)(intptr_t)VQEAF_PAGE_APPS_EDIT);
    lv_obj_t *stats = vqeaf_weighted_row(widget);
    lv_obj_set_style_pad_ver(stats, 4, 0);
    home_stat_cell(stats, &live_uptime, "Thời gian chạy");
    home_stat_cell(stats, &live_mem_pair, "RAM đã dùng");
    home_stat_cell(stats, &live_storage_pct, "Lưu trữ");

    search_pill(root, "Tìm trên thiết bị...");
    section_row(root, "Ứng dụng", "Tất cả");
    app_grid(root, home_apps, sizeof(home_apps) / sizeof(home_apps[0]), 4);
    build_dock(root);

    lv_obj_t *footer = vqeaf_label(root,
                                   "Chỉ hiển thị trạng thái thật",
        VQEAF_FONT_CAPTION, vqeaf_theme_liquid_glass() ?
        lv_color_hex(0xF2DDF9) : VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(footer, lv_pct(100));
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);
}

/* ---- apps ---------------------------------------------------------------- */

/* Tapping an installed app launches it: a native package runs on the device
 * straight away and the shell switches to the returned frame; a declarative
 * package opens the page that carries its print content. The info/detail page
 * (with "Chạy trên thiết bị" and "Gỡ ứng dụng") stays on long-press. */
static void qeapp_open_cb(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    const vqeaf_qeapp_t *app = vqeaf_qeapp_at(index);

    vqeaf_qeapp_select(index);
    if(app != NULL && app->native) {
        vqeaf_ui_run_app(index);
        return;
    }
    vqeaf_ui_show(VQEAF_PAGE_QEAPP);
}

static void pick_qeapp_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_ui_pick_qeapp();
}

static void apps_page_cb(lv_event_t *event)
{
    int page = (int)(intptr_t)lv_event_get_user_data(event);
    vqeaf_pages_apps_page_set(page);
    vqeaf_ui_show(VQEAF_PAGE_APPS);
}

/* A QEAPP may omit assets/icon.rgba, or inherit the SDK's identical default
 * artwork. Give missing/duplicate artwork a stable, distinct native tile;
 * unique embedded package artwork still wins. */
static int qeapp_icon_is_unique(int index)
{
    const vqeaf_qeapp_t *app = vqeaf_qeapp_at(index);
    if(app == NULL || !app->has_icon) return 0;
    for(int i = 0; i < vqeaf_qeapp_count(); i++) {
        const vqeaf_qeapp_t *other = vqeaf_qeapp_at(i);
        if(i == index || other == NULL || !other->has_icon) continue;
        if(memcmp(app->icon, other->icon, sizeof(app->icon)) == 0) return 0;
    }
    return 1;
}

static lv_obj_t *qeapp_icon_tile(lv_obj_t *parent, int index, int32_t size)
{
    static const uint32_t colors[] = {
        0x3285F6, 0x12A892, 0x8758D6, 0xE56A4D, 0xD94F83, 0xC58A28,
        0x397C8D, 0x4F9B57,
    };
    const vqeaf_qeapp_t *app = vqeaf_qeapp_at(index);
    const lv_image_dsc_t *icon;
    uint32_t hash = 2166136261u;
    char initials[3] = { 'Q', 'E', '\0' };
    const unsigned char *name = (const unsigned char *)app->name;
    const unsigned char *first = NULL;
    const unsigned char *word_end;
    const unsigned char *second_word;

    if(app == NULL) return NULL;
    icon = vqeaf_qeapp_icon(index);
    if(icon != NULL && qeapp_icon_is_unique(index))
        return vqeaf_icon_img(parent, icon, size);

    for(const unsigned char *p = (const unsigned char *)app->id; *p; p++) {
        hash ^= *p;
        hash *= 16777619u;
    }
    for(const unsigned char *p = name; *p; p++) {
        if((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') ||
           (*p >= 'a' && *p <= 'z')) {
            first = p;
            initials[0] = (char)(*p >= 'a' && *p <= 'z' ?
                                   *p - 'a' + 'A' : *p);
            break;
        }
    }
    word_end = first;
    while(word_end != NULL && *word_end != '\0' && *word_end != ' ')
        word_end++;
    second_word = word_end;
    while(second_word != NULL && *second_word == ' ') second_word++;
    if(second_word != NULL && *second_word >= '0' && *second_word <= '9') {
        const unsigned char *p = second_word;
        while(*p >= '0' && *p <= '9') initials[1] = (char)*p++;
    } else if(second_word != NULL && *second_word != '\0') {
        unsigned char c = *second_word;
        if(c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
        initials[1] = (char)c;
    } else if(first != NULL) {
        const unsigned char *p = first + 1;
        while(*p != '\0' && *p != ' ') {
            if((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= 'a' && *p <= 'z')) {
                initials[1] = (char)(*p >= 'a' && *p <= 'z' ?
                                       *p - 'a' + 'A' : *p);
                break;
            }
            p++;
        }
    }

    lv_color_t top = lv_color_hex(colors[hash %
        (sizeof(colors) / sizeof(colors[0]))]);
    lv_obj_t *tile = vqeaf_obj(parent);
    lv_obj_set_size(tile, size, size);
    lv_obj_set_style_radius(tile, size / 4, 0);
    vqeaf_app_icon_style(tile, top, lv_color_darken(top, 42));
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_t *mark = vqeaf_label(tile, initials, VQEAF_FONT_HEADLINE_BOLD,
                                  lv_color_white());
    lv_obj_center(mark);
    return tile;
}

static void build_apps(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);
    page_title(root, "Ứng dụng");
    search_pill(root, "Tìm trên thiết bị...");

    lv_obj_t *pick = vqeaf_card(root, lv_pct(100), 92);
    lv_obj_set_flex_flow(pick, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pick, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pick, 14, 0);
    vqeaf_tile(pick, VQEAF_SYM_FOLDER, lv_color_hex(0x2A2E6E),
               lv_color_hex(0xB6BCFF), 54, 17);
    vqeaf_label(pick, "Cài từ bộ nhớ trong", VQEAF_FONT_BODY, VQEAF_C_TEXT);
    lv_obj_t *pick_value = vqeaf_label(pick, "chọn tệp .qeapp",
                                       VQEAF_FONT_CAPTION, VQEAF_C_ACCENT);
    lv_obj_set_flex_grow(pick_value, 1);
    lv_obj_set_style_text_align(pick_value, LV_TEXT_ALIGN_RIGHT, 0);
    vqeaf_glyph(pick, VQEAF_SYM_CHEVRON_RIGHT, VQEAF_ICON_SM,
                VQEAF_C_TEXT_MUTE);
    lv_obj_add_flag(pick, LV_OBJ_FLAG_CLICKABLE);
    vqeaf_on_click(pick, pick_qeapp_cb, NULL);

    int total = (int)(sizeof(all_apps) / sizeof(all_apps[0]));
    int pages = vqeaf_pages_apps_page_count();
    if(g_apps_page >= pages) g_apps_page = pages - 1;
    int first = g_apps_page * VQEAF_APPS_PER_PAGE;
    int last = first + VQEAF_APPS_PER_PAGE;
    if(last > total) last = total;
    app_grid(root, &all_apps[first], (size_t)(last - first), 4 + first);

    lv_obj_t *dots = vqeaf_row(root);
    lv_obj_set_style_pad_column(dots, 10, 0);
    lv_obj_set_width(dots, lv_pct(100));
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    for(int i = 0; i < pages; i++) {
        lv_obj_t *dot = vqeaf_obj(dots);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_set_style_radius(dot, 6, 0);
        int active = i == g_apps_page;
        lv_obj_set_style_bg_color(dot, active ? VQEAF_C_ACCENT : VQEAF_C_LINE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        vqeaf_on_click(dot, apps_page_cb, (void *)(intptr_t)i);
    }

    int installed = vqeaf_qeapp_count();
    if(installed <= 0) return;
    lv_obj_t *installed_heading = vqeaf_row(root);
    lv_obj_set_width(installed_heading, lv_pct(100));
    vqeaf_label(installed_heading, "Đã cài", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    lv_obj_t *heading_spacer = vqeaf_obj(installed_heading);
    lv_obj_set_flex_grow(heading_spacer, 1);
    char installed_count[32];
    snprintf(installed_count, sizeof(installed_count), "%d QEAPP", installed);
    vqeaf_label(installed_heading, installed_count, VQEAF_FONT_CAPTION,
                VQEAF_C_ACCENT);
    lv_obj_t *grid = vqeaf_obj(root);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 16, 0);
    lv_obj_set_style_pad_row(grid, 22, 0);
    for(int i = 0; i < installed; i++) {
        const vqeaf_qeapp_t *app = vqeaf_qeapp_at(i);
        if(app == NULL) continue;
        lv_obj_t *item = vqeaf_obj(grid);
        lv_obj_set_size(item, 156, 152);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(item, 8, 0);
        (void)qeapp_icon_tile(item, i, 96);
        lv_obj_t *label = vqeaf_label(item, app->name, VQEAF_FONT_CAPTION,
                                      VQEAF_C_TEXT);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(item, qeapp_open_cb, LV_EVENT_SHORT_CLICKED,
                            (void *)(intptr_t)i);
        lv_obj_add_event_cb(item, qeapp_info_cb, LV_EVENT_LONG_PRESSED,
                            (void *)(intptr_t)i);
    }
    lv_obj_t *hint = vqeaf_label(root,
        "Chạm để chạy · nhấn giữ để xem thông tin và quyền",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
}

static void qeapp_uninstall_cb(lv_event_t *event)
{
    int index = vqeaf_qeapp_selected();
    const vqeaf_qeapp_t *app = vqeaf_qeapp_at(index);
    char name[64] = "Ứng dụng";
    (void)event;
    if(app != NULL) snprintf(name, sizeof(name), "%s", app->name);
    vqeaf_qeapp_uninstall(index);
    vqeaf_ui_notify(name, "Đã gỡ khỏi thiết bị", VQEAF_SYM_DELETE, 0xFF8A7A);
    vqeaf_ui_show(VQEAF_PAGE_APPS);
}

static void qeapp_store_detail_cb(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    (void)event;
    if(vqeaf_qeapp_at(index) == NULL) return;
    vqeaf_qeapp_select(index);
    vqeaf_ui_show(VQEAF_PAGE_QEAPP);
}

/* The Store is a native catalogue/manager. Network discovery is intentionally
 * not fabricated: install/update candidates are selected from local .qeapp
 * files by the existing signed-package installer. */
static void build_qeapp_store(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);
    int count = vqeaf_qeapp_count();
    char summary[96];

    detail_header(root, "Chợ ứng dụng QEAPP");
    lv_obj_t *hero = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(hero, 10, 0);
    vqeaf_label(hero, "Kho ứng dụng native của QEAFIVELS",
                VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    vqeaf_label(hero,
        "Cài đặt gói .qeapp đã ký, xem phiên bản, quyền khai báo, cập nhật "
        "hoặc gỡ ứng dụng. Định dạng gói và trình quản lý đều là của QEAFIVELS.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    snprintf(summary, sizeof(summary), "%d / %d ứng dụng đã cài",
             count, QEAPP_MAX);
    vqeaf_label(hero, summary, VQEAF_FONT_BODY, VQEAF_C_ACCENT);

    lv_obj_t *install = vqeaf_m3_button(root, "Cài hoặc cập nhật từ tệp .qeapp",
                                        VQEAF_M3_FILLED);
    lv_obj_add_event_cb(install, nav_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)VQEAF_PAGE_INSTALLER);

    vqeaf_label(root, "Thư viện trên thiết bị", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    if(count == 0) {
        lv_obj_t *empty = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
        vqeaf_label(empty, "Chưa có ứng dụng QEAPP",
                    VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
        vqeaf_label(empty, "Chép gói .qeapp vào QEAFRESCUE/Install/Apps "
                    "rồi chọn Cài hoặc cập nhật.",
                    VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
        return;
    }

    for(int i = 0; i < count; i++) {
        const vqeaf_qeapp_t *app = vqeaf_qeapp_at(i);
        if(app == NULL) continue;
        lv_obj_t *card = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(card, 14, 0);
        (void)qeapp_icon_tile(card, i, 72);
        lv_obj_t *details = vqeaf_column(card);
        lv_obj_set_flex_grow(details, 1);
        lv_obj_set_style_pad_row(details, 4, 0);
        lv_obj_t *name = vqeaf_label(details, app->name, VQEAF_FONT_BODY,
                                     VQEAF_C_TEXT);
        lv_obj_set_width(name, lv_pct(100));
        snprintf(summary, sizeof(summary), "Phiên bản %s · %s",
                 app->version, app->native ? "QEN native arm64" : "QEAPP");
        lv_obj_t *version = vqeaf_label(details, summary, VQEAF_FONT_CAPTION,
                                        VQEAF_C_TEXT_DIM);
        lv_obj_set_width(version, lv_pct(100));
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, qeapp_store_detail_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }

    lv_obj_t *note = vqeaf_label(root,
        "Cập nhật: chọn gói .qeapp mới trong Trình cài đặt. Hệ thống xác minh "
        "chữ ký và manifest rồi thay phiên bản theo cùng manifest.id; dữ liệu "
        "riêng được giữ lại. Gỡ ứng dụng trong trang chi tiết.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(note, lv_pct(100));
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
}

/* Launcher edit mode: wallpaper entry point, the live widget preview and the
 * "Xong" exit, in the same arrangement as the reference screen. Nothing here
 * changes the app registry; the grid is the real home grid. */
static void apps_edit_done_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_ui_show(VQEAF_PAGE_HOME);
}

static void apps_edit_widget_cb(lv_event_t *event)
{
    (void)event;
    show_toast("Thẻ trạng thái hệ thống đang hiển thị trên Home.");
}

static void build_apps_edit(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);
    page_title(root, "Sửa màn hình chính");
    lv_obj_t *hint = vqeaf_label(root,
        "Chạm giữ một ứng dụng để xem thông tin hoặc ghim vào dock.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    lv_obj_set_width(hint, lv_pct(100));

    /* Wallpaper preview is the shell's real gradient, not a stock photo. */
    lv_obj_t *preview = vqeaf_obj(root);
    lv_obj_set_size(preview, lv_pct(100), 176);
    lv_obj_set_style_radius(preview, 32, 0);
    lv_obj_set_style_clip_corner(preview, 1, 0);
    lv_obj_set_style_border_width(preview, 1, 0);
    lv_obj_set_style_border_color(preview, VQEAF_C_LINE, 0);
    vqeaf_wallpaper(preview);
    lv_obj_t *overlay = vqeaf_column(preview);
    lv_obj_set_width(overlay, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_align(overlay, LV_ALIGN_BOTTOM_LEFT, 22, -20);
    vqeaf_label(overlay, "Hình nền hiện tại", VQEAF_FONT_BODY, VQEAF_C_TEXT);
    lv_obj_t *change = vqeaf_label(overlay, "Đổi hình nền", VQEAF_FONT_CAPTION,
                                   VQEAF_C_ACCENT);
    vqeaf_on_click(change, nav_cb, (void *)(intptr_t)VQEAF_PAGE_LOCK_THEME);

    lv_obj_t *actions = vqeaf_row(root);
    lv_obj_t *wall = vqeaf_m3_button(actions, "Hình nền", VQEAF_M3_TONAL);
    lv_obj_add_event_cb(wall, nav_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)VQEAF_PAGE_LOCK_THEME);
    lv_obj_t *widgets = vqeaf_m3_button(actions, "Tiện ích",
                                        VQEAF_M3_TONAL);
    lv_obj_add_event_cb(widgets, apps_edit_widget_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *done = vqeaf_m3_button(actions, "Xong", VQEAF_M3_FILLED);
    lv_obj_add_event_cb(done, apps_edit_done_cb, LV_EVENT_CLICKED, NULL);

    app_grid(root, home_apps, sizeof(home_apps) / sizeof(home_apps[0]), 4);

    lv_obj_t *footer = vqeaf_label(root,
        "Bố cục áp dụng ngay trong phiên này; khởi động lại trở về mặc định.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(footer, lv_pct(100));
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);
}

int vqeaf_pages_frame_top_band_empty(const uint32_t *frame)
{
    uint32_t bg;
    int x, y;

    if(frame == NULL) return 0;
    bg = frame[0];
    for(y = 0; y < VQEAF_STATUS_H && y < VQEAF_QEAPP_DEVICE_HEIGHT; y++)
        for(x = 0; x < VQEAF_QEAPP_DEVICE_W; x++)
            if(frame[y * VQEAF_QEAPP_DEVICE_W + x] != bg) return 0;
    return y > 0;
}

static void native_back_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_ui_show(VQEAF_PAGE_APPS);
}

/* The running app's own frame IS its user interface: it fills the page at its
 * native 720x1560 size, with no shell title, lifecycle line or hash over the
 * pixels. The only chrome is a translucent back chip, because a QEAPP gets no
 * input events yet and would otherwise be a one-way page. Launch diagnostics
 * live on the app's detail page (`build_qeapp`) instead. */
static void build_qeapp_native(lv_obj_t *content)
{
    const uint32_t *frame = vqeaf_qeapp_device_frame();
    lv_obj_t *root;

    if(frame == NULL) {
        root = page_root(content);
        detail_header(root, "Ứng dụng");
        vqeaf_label(root, "Chưa có ứng dụng nào chạy trên máy.",
                    VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
        return;
    }

    root = vqeaf_obj(content);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    static lv_image_dsc_t dsc;
    memset(&dsc, 0, sizeof(dsc));
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
    dsc.header.w = VQEAF_QEAPP_DEVICE_W;
    dsc.header.h = VQEAF_QEAPP_DEVICE_HEIGHT;
    dsc.header.stride = VQEAF_QEAPP_DEVICE_W * 4;
    dsc.data_size = VQEAF_QEAPP_DEVICE_W * VQEAF_QEAPP_DEVICE_HEIGHT * 4u;
    dsc.data = (const uint8_t *)frame;

    lv_obj_t *img = lv_image_create(root);
    lv_image_set_src(img, &dsc);
    lv_image_set_scale(img, 256);      /* 1:1: the app drew for this canvas */
    lv_obj_align(img, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *back = vqeaf_tile(root, VQEAF_SYM_ARROW_BACK, VQEAF_C_SURFACE,
                                VQEAF_C_TEXT, 64, 22);
    lv_obj_set_style_bg_opa(back, LV_OPA_70, 0);
    /* Bottom-left: app frames draw their own header from the top, and the
     * bottom strip is where this shell can stay out of the app's way. */
    lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 14, -14);
    vqeaf_on_click(back, native_back_cb, NULL);
}

static lv_obj_t *g_native_modal;

static void native_run_cb(lv_event_t *event);

static void native_error_close_cb(lv_event_t *event)
{
    (void)event;
    if(g_native_modal != NULL) {
        lv_obj_delete(g_native_modal);
        g_native_modal = NULL;
    }
}

/* Grants the manifest-declared display permission on the user's tap, then
 * retries the launch. Installs start with a zero grant mask by policy, so
 * without this the only way to run a freshly installed app was to find the
 * app's detail page and its Permission Manager first. */
static void native_grant_run_cb(lv_event_t *event)
{
    int index = vqeaf_qeapp_selected();
    (void)event;
    native_error_close_cb(NULL);
    if(vqeaf_qeapp_set_permission(index, QEAF_PERM_UI_DISPLAY, 1) == 0)
        vqeaf_ui_notify("Quản lý quyền", "Đã cấp quyền Hiển thị giao diện",
                        VQEAF_SYM_CHECK, 0x69D6A5);
    native_run_cb(NULL);
}

/* `offer_grant` adds a "grant and run" action to the card; only the display
 * permission refusal uses it, because it is the one the launcher itself needs. */
static void native_error_show(const char *text, int offer_grant)
{
    lv_display_t *display = lv_display_get_default();
    int width = (int)lv_display_get_horizontal_resolution(display);
    int height = (int)lv_display_get_vertical_resolution(display);

    if(g_native_modal != NULL) lv_obj_delete(g_native_modal);
    g_native_modal = vqeaf_obj(lv_layer_top());
    lv_obj_set_size(g_native_modal, width, height);
    lv_obj_set_style_bg_color(g_native_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_native_modal, LV_OPA_70, 0);
    lv_obj_add_flag(g_native_modal, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *card = vqeaf_obj(g_native_modal);
    lv_obj_set_size(card, width * 82 / 100, LV_SIZE_CONTENT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, VQEAF_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 30, 0);
    lv_obj_set_style_pad_all(card, 26, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 16, 0);

    vqeaf_label(card, "Không chạy được ứng dụng", VQEAF_FONT_TITLE,
                VQEAF_C_DANGER);
    lv_obj_t *detail = vqeaf_label(card, text, VQEAF_FONT_BODY,
                                   VQEAF_C_TEXT_DIM);
    lv_obj_set_width(detail, lv_pct(100));

    lv_obj_t *actions = vqeaf_row(card);
    lv_obj_set_style_pad_column(actions, 14, 0);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    if(offer_grant) {
        lv_obj_t *grant = vqeaf_obj(actions);
        lv_obj_set_size(grant, 240, 64);
        lv_obj_set_style_bg_color(grant, VQEAF_C_ACCENT, 0);
        lv_obj_set_style_bg_opa(grant, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(grant, 32, 0);
        lv_obj_set_flex_flow(grant, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(grant, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        vqeaf_label(grant, "Cấp quyền và chạy", VQEAF_FONT_CAPTION,
                    lv_color_hex(0x06120F));
        vqeaf_on_click(grant, native_grant_run_cb, NULL);
    }

    lv_obj_t *close = vqeaf_obj(actions);
    lv_obj_set_size(close, 150, 64);
    lv_obj_set_style_bg_color(close, VQEAF_C_SURFACE_3, 0);
    lv_obj_set_style_bg_opa(close, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(close, 32, 0);
    lv_obj_set_flex_flow(close, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(close, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    vqeaf_label(close, "Đóng", VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    vqeaf_on_click(close, native_error_close_cb, NULL);
}

/* Runs the installed arm64 package on the phone. The guarded runner returns
 * after at most VQEAF_QEAPP_DEVICE_TIMEOUT_MS, so this blocks the LVGL thread
 * only for the module's own run (a hang is killed at the hard timeout). */
static void native_run_cb(lv_event_t *event)
{
    (void)event;
    const vqeaf_qeapp_t *app = vqeaf_qeapp_at(vqeaf_qeapp_selected());
    uint8_t *package = NULL;
    size_t size = 0;
    char error[192];
    char report[1024];
    int rc;

    if(app == NULL || !app->native) return;
    /* A fresh install starts with zero grants, so a run without ui.display
     * would draw nothing and look broken. Say so instead of launching. */
    if((vqeaf_qeapp_effective_permissions(vqeaf_qeapp_selected()) &
        QEAF_PERM_UI_DISPLAY) == 0u) {
        native_error_show("Ứng dụng chưa được cấp quyền \"Hiển thị giao diện\".",
                          app != NULL &&
                          (app->permissions & QEAF_PERM_UI_DISPLAY) != 0u);
        return;
    }
    if(vqeaf_qeapp_package(vqeaf_qeapp_selected(), &package, &size) != 0 ||
       package == NULL) {
        native_error_show("Không đọc lại được gói từ kho ứng dụng.", 0);
        return;
    }
    rc = vqeaf_qeapp_device_launch(
        package, size,
        vqeaf_qeapp_effective_permissions(vqeaf_qeapp_selected()),
        report, sizeof(report), error, sizeof(error));
    free(package);
    if(rc != 0) {
        native_error_show(error[0] ? error : "Module native không chạy được.", 0);
        return;
    }
    vqeaf_ui_show(VQEAF_PAGE_QEAPP_NATIVE);
}

void vqeaf_ui_run_app(int index)
{
    if(index < 0 || index >= vqeaf_qeapp_count()) return;
    vqeaf_qeapp_select(index);
    native_run_cb(NULL);
}

static void qeapp_fact(lv_obj_t *parent, const char *key, const char *value)
{
    lv_obj_t *row = vqeaf_row(parent);
    lv_obj_t *name = vqeaf_label(row, key, VQEAF_FONT_CAPTION,
                                 VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(name, 150);
    lv_obj_t *text = vqeaf_label(row, value, VQEAF_FONT_CAPTION,
                                 VQEAF_C_TEXT);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_DOTS);
}


static void qeapp_permission_toggle_cb(lv_event_t *event)
{
    uintptr_t packed = (uintptr_t)lv_event_get_user_data(event);
    int index = (int)((packed >> 8) & 0xffu);
    uint32_t bit = (uint32_t)(packed & 0xffu);
    const vqeaf_qeapp_t *app = vqeaf_qeapp_at(index);
    int grant;
    if(app == NULL || bit == 0u) return;
    grant = (vqeaf_qeapp_effective_permissions(index) & bit) == 0u;
    if(vqeaf_qeapp_set_permission(index, bit, grant) == 0) {
        vqeaf_ui_notify("Permission Manager",
                        grant ? "Đã cấp quyền" : "Đã thu hồi quyền",
                        VQEAF_SYM_CHECK, 0x69D6A5);
        vqeaf_ui_show(VQEAF_PAGE_QEAPP);
    }
}

static void build_qeapp(lv_obj_t *content)
{
    int index = vqeaf_qeapp_selected();
    const vqeaf_qeapp_t *app = vqeaf_qeapp_at(index);
    lv_obj_t *root = page_root(content);

    if(app == NULL) {
        detail_header(root, "Ứng dụng");
        vqeaf_label(root, "Chưa cài ứng dụng QEAPP nào.",
                    VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
        return;
    }

    detail_header(root, app->name);

    lv_obj_t *hero = vqeaf_obj(root);
    lv_obj_set_size(hero, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(hero, 12, 0);
    {
        (void)qeapp_icon_tile(hero, index, 128);
    }
    vqeaf_label(hero, app->name, VQEAF_FONT_HEADLINE, VQEAF_C_TEXT);
    char meta[96];
    snprintf(meta, sizeof(meta), "%s · v%s · %s", app->id, app->version,
             app->native ? "native arm64" : "QEAPP");
    vqeaf_label(hero, meta, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);

    lv_obj_t *body = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 10, 0);
    vqeaf_label(body, app->native ? "Module native arm64" : "Nội dung từ main.qe",
                VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_t *message = vqeaf_label(body,
        app->message[0] != '\0' ? app->message : "(không có print)",
        VQEAF_FONT_BODY, VQEAF_C_TEXT);
    lv_obj_set_width(message, lv_pct(100));

    /* Package facts come from the installed QEAPP record and the p77 catalog —
     * there is no store, rating or download count to invent here. */
    lv_obj_t *facts = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(facts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(facts, 10, 0);
    char perms[192] = "không yêu cầu quyền";
    char perm_buf[192];
    char runtime_buf[48];
    perms[0] = '\0';
    static const struct { uint32_t bit; const char *name; } perm_names[] = {
        { 1u << 0, "Hiển thị" }, { 1u << 1, "Đọc tệp" },
        { 1u << 2, "Ghi dữ liệu riêng" }, { 1u << 3, "Nhận input" },
        { 1u << 4, "Đọc media" },
    };
    for(size_t i = 0; i < sizeof(perm_names) / sizeof(perm_names[0]); i++) {
        if((app->permissions & perm_names[i].bit) == 0) continue;
        if(perms[0] != '\0') strncat(perms, ", ", sizeof(perms) - strlen(perms) - 1);
        strncat(perms, perm_names[i].name, sizeof(perms) - strlen(perms) - 1);
    }
    if(perms[0] == '\0') snprintf(perms, sizeof(perms), "không yêu cầu quyền");
    qeapp_fact(facts, "Định danh", app->id);
    qeapp_fact(facts, "Phiên bản", app->version);
    /* The ABI comes from the installed record, which the installer filled from
     * the package manifest: v2-v5 packages must not all read "v4". */
    if(!app->native)
        snprintf(runtime_buf, sizeof(runtime_buf), "kịch bản .qe");
    else if(app->abi_version > 0)
        snprintf(runtime_buf, sizeof(runtime_buf), "native arm64 · ABI v%d",
                 app->abi_version);
    else
        snprintf(runtime_buf, sizeof(runtime_buf), "native arm64");
    qeapp_fact(facts, "Thời gian chạy", runtime_buf);
    qeapp_fact(facts, "Chữ ký", "HMAC phát triển QEAPP đã xác minh");
    snprintf(perm_buf, sizeof(perm_buf), "%s", perms);
    qeapp_fact(facts, "Quyền", perm_buf);
    snprintf(perm_buf, sizeof(perm_buf), "%zu byte", app->data_size);
    qeapp_fact(facts, "Dữ liệu riêng", perm_buf);

    /* Permission Manager: requested capabilities are read-only manifest data;
     * each grant is a persisted user decision and can be revoked here. */
    {
        lv_obj_t *manager = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(manager, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(manager, 8, 0);
        vqeaf_label(manager, "Quản lý quyền", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
        vqeaf_label(manager, "Quyền chưa cấp mặc định bị từ chối. Quyền mới "
                    "sau cập nhật không tự động được bật.",
                    VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
        static const struct { uint32_t bit; const char *name; } controls[] = {
            { 1u << 0, "Hiển thị giao diện" },
            { 1u << 1, "Đọc tệp" },
            { 1u << 2, "Ghi dữ liệu riêng" },
            { 1u << 3, "Nhận input" },
            { 1u << 4, "Đọc media" },
        };
        for(size_t pi = 0; pi < sizeof(controls)/sizeof(controls[0]); pi++) {
            uint32_t bit = controls[pi].bit;
            if((app->permissions & bit) == 0u) continue;
            lv_obj_t *line = vqeaf_obj(manager);
            lv_obj_set_size(line, lv_pct(100), 58);
            lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(line, LV_FLEX_ALIGN_SPACE_BETWEEN,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_t *label = vqeaf_label(line, controls[pi].name,
                                         VQEAF_FONT_CAPTION, VQEAF_C_TEXT);
            lv_obj_set_flex_grow(label, 1);
            int enabled = (vqeaf_qeapp_effective_permissions(index) & bit) != 0u;
            lv_obj_t *action = vqeaf_obj(line);
            lv_obj_set_size(action, 132, 48);
            lv_obj_set_style_bg_color(action,
                enabled ? lv_color_hex(0x263F38) : VQEAF_C_SURFACE_3, 0);
            lv_obj_set_style_bg_opa(action, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(action, 24, 0);
            vqeaf_label(action, enabled ? "Đang cấp · Thu hồi" : "Đã chặn · Cấp",
                        VQEAF_FONT_CAPTION, enabled ? lv_color_hex(0x8DE0B6) :
                                                       VQEAF_C_TEXT);
            lv_obj_add_flag(action, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(action, qeapp_permission_toggle_cb,
                                LV_EVENT_SHORT_CLICKED,
                                (void *)(uintptr_t)(((uint32_t)index << 8) | bit));
        }
    }

    /* Last on-device run of this module. The running app now fills the screen
     * with its own frame, so the launch facts live here instead of on top of
     * the app's pixels. */
    if(app->native && vqeaf_qeapp_device_lifecycle()[0] != '\0') {
        char last[224];
        snprintf(last, sizeof(last), "Lần chạy gần nhất: %s · %s · hash=0x%08x",
                 vqeaf_qeapp_device_lifecycle(),
                 vqeaf_qeapp_device_process(), vqeaf_qeapp_device_hash());
        lv_obj_t *run_line = vqeaf_label(body, last, VQEAF_FONT_CAPTION,
                                        VQEAF_C_TEXT_MUTE);
        lv_obj_set_width(run_line, lv_pct(100));
        lv_label_set_long_mode(run_line, LV_LABEL_LONG_MODE_DOTS);
    }

    if(app->native) {
        lv_obj_t *run = vqeaf_obj(root);
        lv_obj_set_size(run, 260, 76);
        lv_obj_set_style_bg_color(run, VQEAF_C_ACCENT, 0);
        lv_obj_set_style_bg_opa(run, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(run, 38, 0);
        lv_obj_set_flex_flow(run, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(run, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        vqeaf_label(run, "Chạy ứng dụng", VQEAF_FONT_BODY,
                    lv_color_hex(0x06120F));
        vqeaf_on_click(run, native_run_cb, NULL);
    }

    lv_obj_t *remove = vqeaf_obj(root);
    lv_obj_set_size(remove, 200, 72);
    lv_obj_set_style_bg_color(remove, VQEAF_C_SURFACE_3, 0);
    lv_obj_set_style_bg_opa(remove, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(remove, 36, 0);
    lv_obj_set_flex_flow(remove, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(remove, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    vqeaf_label(remove, "Gỡ ứng dụng", VQEAF_FONT_CAPTION,
                VQEAF_C_DANGER);
    vqeaf_on_click(remove, qeapp_uninstall_cb, NULL);
}

/* ---- browser ------------------------------------------------------------- */
static void build_browser(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);

    detail_header(root, "Trình duyệt");

    lv_obj_t *bar = vqeaf_obj(root);
    lv_obj_set_size(bar, lv_pct(100), 72);
    lv_obj_set_style_bg_color(bar, VQEAF_C_SURFACE_2, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 36, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(bar, 20, 0);
    lv_obj_set_style_pad_right(bar, 20, 0);
    lv_obj_set_style_pad_column(bar, 12, 0);
    vqeaf_label(bar, "https://", VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_t *address = vqeaf_label(bar, "Nhập địa chỉ", VQEAF_FONT_BODY,
                                    VQEAF_C_TEXT_DIM);
    lv_obj_set_flex_grow(address, 1);

    lv_obj_t *card = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 10, 0);
    vqeaf_label(card, "Chưa có backend mạng và engine HTML",
                VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    lv_obj_t *note = vqeaf_label(card,
        "Trình duyệt này mới là giao diện. Hệ điều hành chưa có ngăn xếp "
        "mạng, TLS, HTML/CSS/JavaScript hay codec đa phương tiện, nên không "
        "tải được trang web. Wi-Fi và trình điều khiển cũng chưa có backend.",
        VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
    lv_obj_set_width(note, lv_pct(100));
}

/* ---- notes / text editing ------------------------------------------------ */
static char g_clipboard[4096];

static void notes_copy_cb(lv_event_t *event)
{
    lv_obj_t *area = (lv_obj_t *)lv_event_get_user_data(event);
    const char *text = lv_textarea_get_text(area);
    snprintf(g_clipboard, sizeof(g_clipboard), "%s", text != NULL ? text : "");
}

static void notes_paste_cb(lv_event_t *event)
{
    lv_obj_t *area = (lv_obj_t *)lv_event_get_user_data(event);
    if(g_clipboard[0] != '\0')
        lv_textarea_add_text(area, g_clipboard);
}

static void notes_clear_cb(lv_event_t *event)
{
    lv_obj_t *area = (lv_obj_t *)lv_event_get_user_data(event);
    lv_textarea_set_text(area, "");
}

static void build_notes(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);
    detail_header(root, "Ghi chú");

    lv_obj_t *area = lv_textarea_create(root);
    lv_obj_set_size(area, lv_pct(100), 260);
    lv_textarea_set_placeholder_text(area, "Nhập văn bản...");
    lv_textarea_set_text_selection(area, true);
    lv_obj_set_style_bg_color(area, VQEAF_C_SURFACE_2, 0);
    lv_obj_set_style_bg_opa(area, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(area, VQEAF_C_TEXT, 0);
    lv_obj_set_style_text_font(area, VQEAF_FONT_BODY, 0);
    lv_obj_set_style_border_color(area, VQEAF_C_LINE, 0);
    lv_obj_set_style_border_width(area, 1, 0);
    lv_obj_set_style_radius(area, 18, 0);

    lv_obj_t *row = vqeaf_obj(root);
    lv_obj_set_size(row, lv_pct(100), 64);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 12, 0);

    lv_obj_t *copy = vqeaf_obj(row);
    lv_obj_set_size(copy, 120, 56);
    lv_obj_set_style_bg_color(copy, VQEAF_C_SURFACE_3, 0);
    lv_obj_set_style_bg_opa(copy, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(copy, 28, 0);
    lv_obj_set_flex_flow(copy, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(copy, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    vqeaf_label(copy, "Sao chép", VQEAF_FONT_CAPTION, VQEAF_C_TEXT);
    vqeaf_on_click(copy, notes_copy_cb, area);

    lv_obj_t *paste = vqeaf_obj(row);
    lv_obj_set_size(paste, 120, 56);
    lv_obj_set_style_bg_color(paste, VQEAF_C_SURFACE_3, 0);
    lv_obj_set_style_bg_opa(paste, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(paste, 28, 0);
    lv_obj_set_flex_flow(paste, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(paste, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    vqeaf_label(paste, "Dán", VQEAF_FONT_CAPTION, VQEAF_C_TEXT);
    vqeaf_on_click(paste, notes_paste_cb, area);

    lv_obj_t *clear = vqeaf_obj(row);
    lv_obj_set_size(clear, 120, 56);
    lv_obj_set_style_bg_color(clear, VQEAF_C_SURFACE_3, 0);
    lv_obj_set_style_bg_opa(clear, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(clear, 28, 0);
    lv_obj_set_flex_flow(clear, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(clear, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    vqeaf_label(clear, "Xoá", VQEAF_FONT_CAPTION, VQEAF_C_TEXT);
    vqeaf_on_click(clear, notes_clear_cb, area);

    /* The independent IME window handles typing; focusing the field resizes
     * this page content by H (adjustResize) in sync with the slide-up. */
    lv_obj_add_event_cb(area, ime_focus_cb, LV_EVENT_FOCUSED, content);
    lv_obj_add_event_cb(area, ime_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
}

/* ---- music / video ------------------------------------------------------- */

/* Session-only player model. Tracks are scanned from the real /media tree and
 * their metadata parsed by vqeaf_media_inspect. The initramfs ships no audio
 * codec or mixer, so play/pause/seek are visibly disabled and say why, while
 * selection, queue order, likes, shuffle and repeat are real session state.
 * The full-bleed background is generative ambient art derived from the track
 * name (no embedded cover is extracted); the info sheet states this. */
#define MUSIC_MAX 16
#define MUSIC_LRC_MAX 40
#define MUSIC_ROW_H 96

typedef struct {
    char path[160];
    char title[96];
    char artist[96];
    char album[96];
    char codec[8];
    int rate, bits, channels, kbps, duration_ms;
    uint32_t size;
    int liked;
    uint32_t seed;
} music_track_t;

static music_track_t g_mu[MUSIC_MAX];
static int g_mu_n;
static int g_mu_order[MUSIC_MAX];
static int g_mu_cur;
static int g_mu_view;              /* 0 player, 1 canvas, 2 queue */
static int g_mu_shuffle, g_mu_repeat;   /* repeat: 0 off, 1 all, 2 one */
static int g_mu_loaded;
static char g_mu_lrc[MUSIC_LRC_MAX][128];
static int g_mu_lrc_n, g_mu_lrc_for = -1;
static int g_mu_drag = -1, g_mu_drag_from;
static int32_t g_mu_drag_y0;

static music_track_t *music_cur(void)
{
    if(g_mu_n <= 0 || g_mu_cur < 0 || g_mu_cur >= g_mu_n) return NULL;
    return &g_mu[g_mu_order[g_mu_cur]];
}

static void music_time(int ms, char *out, size_t cap, int negative)
{
    int s = ms / 1000;
    snprintf(out, cap, "%s%d:%02d", negative ? "-" : "", s / 60, s % 60);
}

static uint32_t music_hash(const char *s)
{
    uint32_t h = 2166136261u;
    for(; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; }
    return h;
}

static void music_load_lrc(int idx)
{
    static uint8_t buf[4096];
    char side[160];
    char *dot;

    g_mu_lrc_n = 0;
    g_mu_lrc_for = idx;
    snprintf(side, sizeof(side), "%s", g_mu[idx].path);
    dot = strrchr(side, '.');
    if(dot == NULL) return;
    snprintf(dot, sizeof(side) - (size_t)(dot - side), ".lrc");
    int got = vqeaf_files_read(side, buf, sizeof(buf) - 1);
    if(got <= 0) return;
    buf[got] = 0;
    char *line = (char *)buf;
    while(line != NULL && g_mu_lrc_n < MUSIC_LRC_MAX) {
        char *next = strchr(line, '\n');
        if(next != NULL) *next++ = 0;
        char *text = line;
        if(text[0] == '[') {
            char *close = strchr(text, ']');
            if(close != NULL) text = close + 1;
        }
        while(*text == ' ' || *text == '\r') text++;
        if(*text != 0)
            snprintf(g_mu_lrc[g_mu_lrc_n++], sizeof(g_mu_lrc[0]), "%s", text);
        line = next;
    }
}

static void music_scan(void)
{
    vqeaf_file_t files[64];
    static uint8_t buf[128 * 1024];
    int n = vqeaf_files_list("/media", files, 64);

    g_mu_n = 0;
    g_mu_cur = 0;
    for(int i = 0; i < n && g_mu_n < MUSIC_MAX; i++) {
        if(files[i].is_dir) continue;
        if(vqeaf_files_kind(files[i].name) != VQEAF_FILE_AUDIO) continue;
        music_track_t *t = &g_mu[g_mu_n];
        memset(t, 0, sizeof(*t));
        if(vqeaf_files_join("/media", files[i].name, t->path,
                            sizeof(t->path)) != 0)
            continue;
        int got = vqeaf_files_read(t->path, buf, sizeof(buf));
        if(got <= 0) continue;
        vqeaf_media_info_t info;
        memset(&info, 0, sizeof(info));
        if(vqeaf_media_inspect(buf, (size_t)got, &info) != 0) continue;
        if(info.type != VQEAF_MEDIA_WAV && info.type != VQEAF_MEDIA_MP3) continue;

        char stem[96];
        snprintf(stem, sizeof(stem), "%s", files[i].name);
        char *dot = strrchr(stem, '.');
        if(dot != NULL) *dot = 0;
        snprintf(t->title, sizeof(t->title), "%s",
                 info.title[0] != 0 ? info.title : stem);
        snprintf(t->artist, sizeof(t->artist), "%s",
                 info.artist[0] != 0 ? info.artist : "Không rõ nghệ sĩ");
        snprintf(t->album, sizeof(t->album), "%s",
                 info.album[0] != 0 ? info.album : "Không rõ album");
        t->rate = info.sample_rate;
        t->bits = info.bits;
        t->channels = info.channels;
        t->duration_ms = info.duration_ms;
        t->size = files[i].size;
        if(info.type == VQEAF_MEDIA_WAV) {
            snprintf(t->codec, sizeof(t->codec), "WAV");
            t->kbps = t->rate * t->bits * t->channels / 1000;
        } else {
            snprintf(t->codec, sizeof(t->codec), "MP3");
            t->kbps = info.bitrate_kbps;
        }
        t->seed = music_hash(files[i].name);
        g_mu_order[g_mu_n] = g_mu_n;
        g_mu_n++;
    }
    g_mu_loaded = 1;
    g_mu_lrc_for = -1;
    g_mu_lrc_n = 0;
    if(g_mu_n > 0) music_load_lrc(g_mu_order[0]);
}

/* Rebuilding deletes the very widget dispatching the click, so defer to the
 * async phase like the IME does. */
static void music_rebuild_async(void *user_data)
{
    (void)user_data;
    vqeaf_ui_show(VQEAF_PAGE_MUSIC);
}

static void music_rebuild(void)
{
    lv_async_call(music_rebuild_async, NULL);
}

void vqeaf_pages_music_view_set(int view)
{
    if(view < 0) view = 0;
    if(view > 2) view = 2;
    g_mu_view = view;
}

int vqeaf_pages_music_view(void)
{
    return g_mu_view;
}

static void music_dialog_close_cb(lv_event_t *event)
{
    lv_obj_delete((lv_obj_t *)lv_event_get_user_data(event));
}

static void music_show_dialog(const char *title, const char *body)
{
    lv_obj_t *confirm = NULL;
    lv_obj_t *scrim = vqeaf_m3_dialog(lv_layer_top(), title, body, "Đóng",
                                      NULL, &confirm, NULL);
    if(confirm != NULL) vqeaf_on_click(confirm, music_dialog_close_cb, scrim);
}

static void music_snack_timer_cb(lv_timer_t *timer)
{
    lv_obj_t *bar = (lv_obj_t *)lv_timer_get_user_data(timer);
    if(lv_obj_is_valid(bar)) lv_obj_delete(bar);
    lv_timer_delete(timer);
}

static void music_snack(const char *msg)
{
    lv_obj_t *bar = vqeaf_m3_snackbar(lv_layer_top(), msg, NULL, NULL);
    lv_timer_create(music_snack_timer_cb, 2600, bar);
}

/* ---- music callbacks ----------------------------------------------------- */

static void music_prev_cb(lv_event_t *event)
{
    (void)event;
    if(g_mu_n <= 0) return;
    if(g_mu_cur > 0) g_mu_cur--;
    else if(g_mu_repeat == 1 || g_mu_shuffle) g_mu_cur = g_mu_n - 1;
    else return;
    music_rebuild();
}

static void music_next_cb(lv_event_t *event)
{
    (void)event;
    if(g_mu_n <= 0) return;
    if(g_mu_repeat == 2) { music_rebuild(); return; }
    if(g_mu_cur + 1 < g_mu_n) g_mu_cur++;
    else if(g_mu_repeat == 1 || g_mu_shuffle) g_mu_cur = 0;
    else return;
    music_rebuild();
}

static void music_play_cb(lv_event_t *event)
{
    (void)event;
    music_snack("Chưa có codec âm thanh (WAV/MP3) hay mixer — không phát được.");
}

static void music_like_cb(lv_event_t *event)
{
    (void)event;
    music_track_t *t = music_cur();
    if(t == NULL) return;
    t->liked = !t->liked;
    music_rebuild();
}

static void music_more_cb(lv_event_t *event)
{
    (void)event;
    music_track_t *t = music_cur();
    if(t == NULL) return;
    char body[512];
    char dur[24];
    music_time(t->duration_ms, dur, sizeof(dur), 0);
    snprintf(body, sizeof(body),
             "%s\n%s · %d.%d kHz · %d kbps · %s · %s\n"
             "Dung lượng: %u byte · %s\n"
             "Ảnh nền: tạo thủ tục từ tên bài (chưa trích ảnh bìa nhúng).\n"
             "OS chưa có codec âm thanh nên chỉ xem metadata, không phát.",
             t->path, t->codec, t->rate / 1000, (t->rate % 1000) / 100,
             t->kbps, t->channels == 1 ? "Mono" : "Stereo", dur,
             (unsigned)t->size, t->album);
    music_show_dialog(t->title, body);
}

static void music_device_cb(lv_event_t *event)
{
    (void)event;
    music_show_dialog("Thiết bị phát",
                      "Chưa có backend âm thanh (mixer/codec) trên OS này nên "
                      "không chọn được đầu ra loa/tai nghe. Metadata và hàng "
                      "chờ vẫn hoạt động bình thường.");
}

static void music_person_cb(lv_event_t *event)
{
    (void)event;
    music_track_t *t = music_cur();
    if(t == NULL) return;
    int same = 0;
    for(int i = 0; i < g_mu_n; i++)
        if(strcmp(g_mu[i].artist, t->artist) == 0) same++;
    char body[256];
    snprintf(body, sizeof(body),
             "Nghệ sĩ: %s\nSố bài trong /media của nghệ sĩ này: %d\n"
             "Album: %s", t->artist, same, t->album);
    music_show_dialog("Nghệ sĩ", body);
}

static void music_lyrics_cb(lv_event_t *event)
{
    (void)event;
    g_mu_view = (g_mu_view == 1) ? 0 : 1;
    music_rebuild();
}

static void music_queue_cb(lv_event_t *event)
{
    (void)event;
    g_mu_view = (g_mu_view == 2) ? 0 : 2;
    music_rebuild();
}

static void music_shuffle_cb(lv_event_t *event)
{
    (void)event;
    g_mu_shuffle = !g_mu_shuffle;
    if(g_mu_n <= 0) { music_rebuild(); return; }
    int curtrack = g_mu_order[g_mu_cur];
    if(g_mu_shuffle) {
        int rest[MUSIC_MAX];
        int rn = 0;
        for(int i = 0; i < g_mu_n; i++)
            if(g_mu_order[i] != curtrack) rest[rn++] = g_mu_order[i];
        for(int i = rn - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            int v = rest[i]; rest[i] = rest[j]; rest[j] = v;
        }
        g_mu_order[0] = curtrack;
        for(int i = 0; i < rn; i++) g_mu_order[i + 1] = rest[i];
        g_mu_cur = 0;
    } else {
        for(int i = 0; i < g_mu_n; i++) g_mu_order[i] = i;
        g_mu_cur = curtrack;
    }
    music_rebuild();
}

static void music_repeat_cb(lv_event_t *event)
{
    (void)event;
    g_mu_repeat = (g_mu_repeat + 1) % 3;
    music_rebuild();
}

static void music_row_cb(lv_event_t *event)
{
    int pos = (int)(intptr_t)lv_event_get_user_data(event);
    if(pos < 0 || pos >= g_mu_n) return;
    g_mu_cur = pos;
    music_rebuild();
}

static void music_remove_cb(lv_event_t *event)
{
    int pos = (int)(intptr_t)lv_event_get_user_data(event);
    if(pos < 0 || pos >= g_mu_n) return;
    int idx = g_mu_order[pos];
    memmove(&g_mu[idx], &g_mu[idx + 1],
            (size_t)(g_mu_n - idx - 1) * sizeof(g_mu[0]));
    g_mu_n--;
    for(int i = 0; i < g_mu_n; i++) g_mu_order[i] = i;
    if(g_mu_cur >= g_mu_n) g_mu_cur = g_mu_n - 1;
    if(g_mu_cur < 0) g_mu_cur = 0;
    g_mu_lrc_for = -1;
    music_rebuild();
}

static void music_drag_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    int pos = (int)(intptr_t)lv_event_get_user_data(event);
    lv_indev_t *indev = lv_indev_active();
    if(indev == NULL) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if(code == LV_EVENT_PRESSED) {
        g_mu_drag = pos;
        g_mu_drag_from = pos;
        g_mu_drag_y0 = p.y;
    } else if(code == LV_EVENT_PRESSING && g_mu_drag >= 0) {
        int target = g_mu_drag_from + (int)((p.y - g_mu_drag_y0) / MUSIC_ROW_H);
        if(target < 0) target = 0;
        if(target >= g_mu_n) target = g_mu_n - 1;
        while(g_mu_drag != target) {
            int step = target > g_mu_drag ? 1 : -1;
            int from = g_mu_drag, to = from + step;
            int v = g_mu_order[from];
            g_mu_order[from] = g_mu_order[to];
            g_mu_order[to] = v;
            if(g_mu_cur == from) g_mu_cur = to;
            else if(g_mu_cur == to) g_mu_cur = from;
            g_mu_drag = to;
        }
    } else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if(g_mu_drag >= 0) { g_mu_drag = -1; music_rebuild(); }
    }
}

/* ---- music widgets ------------------------------------------------------- */

/* Generative ambient cover: a two-stop gradient plus two translucent blobs,
 * all derived from the track-name hash so each track keeps a stable look. */
static lv_obj_t *music_art(lv_obj_t *parent, uint32_t seed, int32_t w, int32_t h)
{
    lv_obj_t *art = vqeaf_obj(parent);
    lv_obj_set_size(art, w, h);
    uint16_t hue = (uint16_t)(seed % 360u);
    lv_obj_set_style_bg_color(art, lv_color_hsv_to_rgb(hue, 120, 60), 0);
    lv_obj_set_style_bg_grad_color(art,
        lv_color_hsv_to_rgb((uint16_t)((hue + 40) % 360), 160, 26), 0);
    lv_obj_set_style_bg_grad_dir(art, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(art, LV_OPA_COVER, 0);
    for(int i = 0; i < 2; i++) {
        lv_obj_t *blob = vqeaf_obj(art);
        int32_t d = (i == 0 ? w * 3 / 4 : w / 2);
        if(d < 8) d = 8;
        lv_obj_set_size(blob, d, d);
        lv_obj_set_style_radius(blob, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(blob,
            lv_color_hsv_to_rgb((uint16_t)((hue + 60 + i * 30) % 360), 140, 90), 0);
        lv_obj_set_style_bg_opa(blob, (lv_opa_t)(40 + i * 20), 0);
        int32_t mx = w - d > 0 ? w - d : 0;
        int32_t my = h - d > 0 ? h - d : 0;
        lv_obj_set_pos(blob, (int32_t)((seed >> (i * 5)) % (uint32_t)(mx + 1)),
                       (int32_t)((seed >> (i * 3 + 1)) % (uint32_t)(my + 1)));
    }
    return art;
}

static lv_obj_t *music_icon_btn(lv_obj_t *parent, const char *sym,
                                lv_color_t fg, int32_t size,
                                lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = vqeaf_tile(parent, sym, VQEAF_C_SURFACE_CONTAINER_HIGH,
                               fg, size, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(btn, 150, 0);
    if(cb != NULL) vqeaf_on_click(btn, cb, user_data);
    return btn;
}

static void music_title_block(lv_obj_t *parent, music_track_t *t)
{
    lv_obj_t *row = vqeaf_row(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_t *col = vqeaf_column(row);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_t *title = vqeaf_label(col, t->title, VQEAF_FONT_TITLE_BOLD,
                                  VQEAF_C_TEXT);
    lv_obj_set_width(title, lv_pct(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t *artist = vqeaf_label(col, t->artist, VQEAF_FONT_BODY,
                                   VQEAF_C_TEXT_DIM);
    lv_obj_set_width(artist, lv_pct(100));
    lv_label_set_long_mode(artist, LV_LABEL_LONG_MODE_DOTS);
    music_icon_btn(row, VQEAF_SYM_FAVORITE,
                   t->liked ? VQEAF_C_ACCENT : VQEAF_C_TEXT, 64,
                   music_like_cb, NULL);
    music_icon_btn(row, VQEAF_SYM_MORE_VERT, VQEAF_C_TEXT, 64,
                   music_more_cb, NULL);
}

static void music_lyric_row(lv_obj_t *parent)
{
    lv_obj_t *row = vqeaf_row(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    const char *text = g_mu_lrc_n > 0 ? g_mu_lrc[0] : "Chưa có lời bài hát";
    lv_obj_t *label = vqeaf_label(row, text, VQEAF_FONT_BODY,
                                  g_mu_lrc_n > 0 ? VQEAF_C_TEXT
                                                 : VQEAF_C_TEXT_MUTE);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    vqeaf_glyph(row, VQEAF_SYM_CHEVRON_RIGHT, VQEAF_ICON_SM, VQEAF_C_TEXT_DIM);
    vqeaf_on_click(row, music_lyrics_cb, NULL);
}

static void music_progress_block(lv_obj_t *parent, music_track_t *t)
{
    vqeaf_m3_progress_linear(parent, 0);
    lv_obj_t *times = vqeaf_row(parent);
    lv_obj_set_width(times, lv_pct(100));
    lv_obj_set_flex_align(times, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    char el[24], rem[24];
    music_time(0, el, sizeof(el), 0);
    music_time(t->duration_ms, rem, sizeof(rem), 1);
    vqeaf_label(times, el, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    vqeaf_label(times, rem, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    vqeaf_label(parent, "Không phát/tua được: OS chưa có codec âm thanh.",
                VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
}

static void music_transport(lv_obj_t *parent)
{
    lv_obj_t *row = vqeaf_row(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    music_icon_btn(row, VQEAF_SYM_SKIP_PREVIOUS, VQEAF_C_TEXT, 96,
                   music_prev_cb, NULL);
    lv_obj_t *play = music_icon_btn(row, VQEAF_SYM_PAUSE, VQEAF_C_TEXT, 112,
                                    music_play_cb, NULL);
    lv_obj_set_style_opa(play, LV_OPA_40, 0);
    music_icon_btn(row, VQEAF_SYM_SKIP_NEXT, VQEAF_C_TEXT, 96,
                   music_next_cb, NULL);
}

static void music_bottom_bar(lv_obj_t *parent)
{
    lv_obj_t *row = vqeaf_row(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    music_icon_btn(row, VQEAF_SYM_LYRICS,
                   g_mu_view == 1 ? VQEAF_C_ACCENT : VQEAF_C_TEXT, 64,
                   music_lyrics_cb, NULL);
    lv_obj_t *pill = vqeaf_row(row);
    lv_obj_set_width(pill, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(pill, VQEAF_C_SURFACE_CONTAINER_HIGH, 0);
    lv_obj_set_style_bg_opa(pill, 150, 0);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(pill, 8, 0);
    lv_obj_set_style_pad_column(pill, 4, 0);
    if(g_mu_view == 2) {
        const char *rsym = g_mu_repeat == 1 ? VQEAF_SYM_ALL_INCLUSIVE
                                            : VQEAF_SYM_REPEAT;
        lv_color_t rfg = g_mu_repeat != 0 ? VQEAF_C_ACCENT : VQEAF_C_TEXT_DIM;
        music_icon_btn(pill, rsym, rfg, 64, music_repeat_cb, NULL);
    } else {
        music_icon_btn(pill, VQEAF_SYM_SMARTPHONE, VQEAF_C_TEXT, 64,
                       music_device_cb, NULL);
    }
    music_icon_btn(pill, VQEAF_SYM_SHUFFLE,
                   g_mu_shuffle ? VQEAF_C_ACCENT : VQEAF_C_TEXT, 64,
                   music_shuffle_cb, NULL);
    if(g_mu_view == 2) {
        music_icon_btn(pill, VQEAF_SYM_ALL_INCLUSIVE,
                       g_mu_repeat == 2 ? VQEAF_C_ACCENT : VQEAF_C_TEXT_DIM,
                       64, music_repeat_cb, NULL);
    } else {
        music_icon_btn(pill, VQEAF_SYM_PERSON, VQEAF_C_TEXT, 64,
                       music_person_cb, NULL);
    }
    music_icon_btn(row, VQEAF_SYM_QUEUE_MUSIC,
                   g_mu_view == 2 ? VQEAF_C_ACCENT : VQEAF_C_TEXT, 64,
                   music_queue_cb, NULL);
}

static void music_codec_line(lv_obj_t *parent, music_track_t *t)
{
    char line[160];
    snprintf(line, sizeof(line), "%s · %d.%d kHz · %d kbps · %s",
             t->codec, t->rate / 1000, (t->rate % 1000) / 100, t->kbps,
             t->channels == 1 ? "Mono" : "Stereo");
    lv_obj_t *label = vqeaf_label(parent, line, VQEAF_FONT_CAPTION,
                                  VQEAF_C_TEXT_DIM);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

static void music_lyric_overlay(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *lyr = vqeaf_obj(parent);
    lv_obj_set_size(lyr, w, h);
    lv_obj_align(lyr, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_flex_flow(lyr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(lyr, 20, 0);
    lv_obj_set_style_pad_row(lyr, 14, 0);
    lv_obj_set_style_radius(lyr, 16, 0);
    lv_obj_set_style_bg_color(lyr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lyr, 110, 0);
    lv_obj_add_flag(lyr, LV_OBJ_FLAG_SCROLLABLE);
    if(g_mu_lrc_n == 0) {
        vqeaf_label(lyr, "Chưa có lời bài hát", VQEAF_FONT_BODY,
                    VQEAF_C_TEXT_DIM);
        return;
    }
    for(int i = 0; i < g_mu_lrc_n; i++)
        vqeaf_label(lyr, g_mu_lrc[i], VQEAF_FONT_TITLE,
                    i == 0 ? VQEAF_C_TEXT : VQEAF_C_TEXT_DIM);
}

static void music_controls(lv_obj_t *parent, music_track_t *t, int lyric_row)
{
    music_title_block(parent, t);
    if(lyric_row) music_lyric_row(parent);
    music_progress_block(parent, t);
    music_transport(parent);
    music_bottom_bar(parent);
    music_codec_line(parent, t);
}

static void music_queue_row(lv_obj_t *list, int pos)
{
    music_track_t *t = &g_mu[g_mu_order[pos]];
    lv_obj_t *row = vqeaf_obj(list);
    lv_obj_set_size(row, lv_pct(100), MUSIC_ROW_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_style_radius(row, 12, 0);
    if(pos == g_mu_cur) {
        lv_obj_set_style_bg_color(row, VQEAF_C_SURFACE_CONTAINER, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    }
    if(pos == g_mu_drag) {
        lv_obj_set_style_bg_color(row, VQEAF_C_ACCENT_DK, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    }

    lv_obj_t *handle = vqeaf_label(row, "=", VQEAF_FONT_TITLE,
                                   VQEAF_C_TEXT_MUTE);
    lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(handle, music_drag_cb, LV_EVENT_PRESSED,
                        (void *)(intptr_t)pos);
    lv_obj_add_event_cb(handle, music_drag_cb, LV_EVENT_PRESSING,
                        (void *)(intptr_t)pos);
    lv_obj_add_event_cb(handle, music_drag_cb, LV_EVENT_RELEASED,
                        (void *)(intptr_t)pos);
    lv_obj_add_event_cb(handle, music_drag_cb, LV_EVENT_PRESS_LOST,
                        (void *)(intptr_t)pos);

    lv_obj_t *thumb = music_art(row, t->seed, 64, 64);
    lv_obj_set_style_radius(thumb, 12, 0);
    lv_obj_set_style_clip_corner(thumb, true, 0);

    lv_obj_t *col = vqeaf_column(row);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_t *title = vqeaf_label(col, t->title, VQEAF_FONT_BODY_BOLD,
                                  VQEAF_C_TEXT);
    lv_obj_set_width(title, lv_pct(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t *artist = vqeaf_label(col, t->artist, VQEAF_FONT_CAPTION,
                                   VQEAF_C_TEXT_DIM);
    lv_obj_set_width(artist, lv_pct(100));
    lv_label_set_long_mode(artist, LV_LABEL_LONG_MODE_DOTS);
    vqeaf_on_click(row, music_row_cb, (void *)(intptr_t)pos);

    music_icon_btn(row, VQEAF_SYM_CLOSE, VQEAF_C_TEXT_DIM, 56,
                   music_remove_cb, (void *)(intptr_t)pos);
}

static void build_music_queue(lv_obj_t *root, music_track_t *t,
                              int32_t W, int32_t H)
{
    (void)W; (void)H;
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, VQEAF_PAD, 0);
    lv_obj_set_style_pad_row(root, 14, 0);
    lv_obj_set_style_bg_color(root, VQEAF_C_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    lv_obj_t *head = vqeaf_row(root);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 14, 0);
    lv_obj_t *back = vqeaf_tile(head, VQEAF_SYM_ARROW_BACK, VQEAF_C_SURFACE,
                                VQEAF_C_TEXT, 64, 20);
    vqeaf_on_click(back, nav_cb, (void *)(intptr_t)VQEAF_PAGE_HOME);
    lv_obj_t *thumb = music_art(head, t->seed, 64, 64);
    lv_obj_set_style_radius(thumb, 12, 0);
    lv_obj_set_style_clip_corner(thumb, true, 0);
    lv_obj_t *col = vqeaf_column(head);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_t *title = vqeaf_label(col, t->title, VQEAF_FONT_BODY_BOLD,
                                  VQEAF_C_TEXT);
    lv_obj_set_width(title, lv_pct(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    vqeaf_label(col, t->artist, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    music_icon_btn(head, VQEAF_SYM_FAVORITE,
                   t->liked ? VQEAF_C_ACCENT : VQEAF_C_TEXT, 56,
                   music_like_cb, NULL);
    music_icon_btn(head, VQEAF_SYM_MORE_VERT, VQEAF_C_TEXT, 56,
                   music_more_cb, NULL);

    vqeaf_label(root, "Hàng chờ", VQEAF_FONT_TITLE_BOLD, VQEAF_C_TEXT);
    char from[96];
    snprintf(from, sizeof(from), "Phát tiếp từ: /media · %d bài", g_mu_n);
    vqeaf_label(root, from, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);

    lv_obj_t *list = vqeaf_column(root);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    /* The list owns its vertical drags (row reorder). Opt out of gesture
     * bubbling so the shell's pull-down quick panel never steals a drag. */
    lv_obj_remove_flag(list, LV_OBJ_FLAG_GESTURE_BUBBLE);
    for(int i = 0; i < g_mu_n; i++) music_queue_row(list, i);

    music_progress_block(root, t);
    music_transport(root);
    music_bottom_bar(root);
    music_codec_line(root, t);
}

static void build_music(lv_obj_t *content)
{
    if(!g_mu_loaded) music_scan();
    lv_display_t *d = lv_display_get_default();
    int32_t W = lv_display_get_horizontal_resolution(d);
    int32_t H = lv_display_get_vertical_resolution(d);
    int landscape = H < W;

    lv_obj_t *root = vqeaf_obj(content);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    music_track_t *t = music_cur();
    if(t == NULL) {
        lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(root, VQEAF_PAD, 0);
        lv_obj_set_style_pad_row(root, 16, 0);
        lv_obj_t *back = vqeaf_tile(root, VQEAF_SYM_ARROW_BACK,
                                    VQEAF_C_SURFACE, VQEAF_C_TEXT, 64, 20);
        vqeaf_on_click(back, nav_cb, (void *)(intptr_t)VQEAF_PAGE_HOME);
        vqeaf_label(root, "Nhạc", VQEAF_FONT_HEADLINE_BOLD, VQEAF_C_TEXT);
        vqeaf_label(root, "Không có tệp âm thanh nào trong /media.",
                    VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
        return;
    }
    if(g_mu_lrc_for != g_mu_order[g_mu_cur])
        music_load_lrc(g_mu_order[g_mu_cur]);

    if(g_mu_view == 2) {
        build_music_queue(root, t, W, H);
        return;
    }

    if(landscape) {
        /* Sideways: art left, controls right; lyrics overlay the art. */
        lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
        lv_obj_t *left = vqeaf_obj(root);
        lv_obj_set_size(left, W * 45 / 100, lv_pct(100));
        music_art(left, t->seed, W * 45 / 100, H);
        if(g_mu_view == 1)
            music_lyric_overlay(left, W * 45 / 100 - 32, H * 70 / 100);
        lv_obj_t *back = vqeaf_tile(left, VQEAF_SYM_ARROW_BACK,
                                    lv_color_black(), VQEAF_C_TEXT, 64, 20);
        lv_obj_set_style_bg_opa(back, 120, 0);
        lv_obj_set_pos(back, 16, 16);
        vqeaf_on_click(back, nav_cb, (void *)(intptr_t)VQEAF_PAGE_HOME);

        lv_obj_t *right = vqeaf_column(root);
        lv_obj_set_size(right, W * 55 / 100, lv_pct(100));
        lv_obj_set_style_pad_all(right, 24, 0);
        lv_obj_set_style_pad_row(right, 12, 0);
        lv_obj_set_style_bg_color(right, VQEAF_C_BG, 0);
        lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
        lv_obj_add_flag(right, LV_OBJ_FLAG_SCROLLABLE);
        music_controls(right, t, 1);
        return;
    }

    /* Portrait: full-bleed art with a scrim-backed control panel at the
     * bottom, exactly like the reference player. */
    music_art(root, t->seed, W, H);
    lv_obj_t *back = vqeaf_tile(root, VQEAF_SYM_ARROW_BACK, lv_color_black(),
                                VQEAF_C_TEXT, 64, 20);
    lv_obj_set_style_bg_opa(back, 120, 0);
    lv_obj_set_pos(back, 16, 16);
    vqeaf_on_click(back, nav_cb, (void *)(intptr_t)VQEAF_PAGE_HOME);
    if(g_mu_view == 1) music_lyric_overlay(root, W * 86 / 100, H * 55 / 100);

    lv_obj_t *panel = vqeaf_obj(root);
    lv_obj_set_width(panel, lv_pct(100));
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(panel, 24, 0);
    lv_obj_set_style_pad_row(panel, 14, 0);
    lv_obj_set_style_bg_color(panel, lv_color_black(), 0);
    lv_obj_set_style_bg_grad_color(panel, lv_color_black(), 0);
    lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    music_controls(panel, t, 1);
}

static void build_video(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);
    vqeaf_video_entry_t entries[8];
    vqeaf_video_entry_t *preview = NULL;
    int count;
    char line[256];

    detail_header(root, "Video");

    count = vqeaf_video_scan("/media", entries,
                             (int)(sizeof(entries) / sizeof(entries[0])));
    if(count <= 0) {
        vqeaf_label(root,
                    "Khong co tep video nao trong /media.",
                    VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
        return;
    }

    lv_obj_t *card = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 10, 0);
    vqeaf_label(card, "Trình phát video", VQEAF_FONT_TITLE, VQEAF_C_TEXT);

    for(int i = 0; i < count; i++) {
        const vqeaf_video_entry_t *e = &entries[i];
        if(e->kind == VQEAF_VIDEO_QVF) {
            snprintf(line, sizeof(line),
                     "%s — QVF %ux%u @%ufps, %u khung — phát được",
                     e->name, e->width, e->height, e->fps, e->frames);
            if(preview == NULL) preview = &entries[i];
        }
        else if(e->kind == VQEAF_VIDEO_MP4) {
            snprintf(line, sizeof(line),
                     "%s — MP4; codec H.264/AAC: OS chưa hỗ trợ — "
                     "chuyển đổi sang QVF để phát",
                     e->name);
        }
        else {
            snprintf(line, sizeof(line),
                     "%s — định dạng video không nhận diện được (%u byte)",
                     e->name, e->size);
        }
        vqeaf_label(card, line, VQEAF_FONT_BODY, VQEAF_C_TEXT);
    }

    if(preview == NULL) {
        vqeaf_label(card,
                    "Chưa có tệp QVF nào — trang này chỉ giải mã được QVF; "
                    "MP4 cần chuyển đổi bằng công cụ build.",
                    VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
        return;
    }

    /* Real preview: frame 0 decoded from the actual file. */
    static uint32_t frame[VQEAF_VIDEO_MAX_W * VQEAF_VIDEO_MAX_H];
    int rc = vqeaf_video_qvf_frame(preview->path, 0, frame,
                                   VQEAF_VIDEO_MAX_W);
    if(rc != VQEAF_VIDEO_OK) {
        snprintf(line, sizeof(line), "%s: %s", preview->name,
                 vqeaf_video_strerror(rc));
        vqeaf_label(card, line, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
        return;
    }

    static lv_image_dsc_t dsc;
    memset(&dsc, 0, sizeof(dsc));
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
    dsc.header.w = (uint32_t)preview->width;
    dsc.header.h = (uint32_t)preview->height;
    dsc.header.stride = (uint32_t)preview->width * 4u;
    dsc.data_size = (uint32_t)preview->width * (uint32_t)preview->height * 4u;
    dsc.data = (const uint8_t *)frame;

    lv_obj_t *img = lv_image_create(card);
    lv_image_set_src(img, &dsc);
    lv_image_set_scale(img, 220);
    lv_obj_set_style_border_width(img, 1, 0);
    lv_obj_set_style_border_color(img, VQEAF_C_LINE, 0);

    snprintf(line, sizeof(line), "Khung 1/%u — giải mã thật từ %s",
             preview->frames, preview->name);
    vqeaf_label(card, line, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
}

/* ---- gallery / library --------------------------------------------------- */
static void gallery_open_image(const char *path);
static int view_decode_jpg(const uint8_t *data, size_t len);
static int gallery_make_thumb(const char *path, int idx);

#define GALLERY_THUMB_W 160u
#define GALLERY_THUMB_H 120u
#define GALLERY_MAX_THUMBS 12u
static char g_gallery_paths[24][256];
static uint8_t g_gallery_thumb_px[GALLERY_MAX_THUMBS]
                                 [GALLERY_THUMB_W * GALLERY_THUMB_H * 4u];
static lv_image_dsc_t g_gallery_thumb_dsc[GALLERY_MAX_THUMBS];
static int g_gallery_thumb_ok[GALLERY_MAX_THUMBS];

static void gallery_tile_cb(lv_event_t *event)
{
    const char *path = (const char *)lv_event_get_user_data(event);
    if(path != NULL) gallery_open_image(path);
}

static void build_gallery(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);
    detail_header(root, "Thư viện");
    vqeaf_file_t files[24];
    int count = vqeaf_files_list("/Pictures", files, 24);
    int shown = 0;

    lv_obj_t *card = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_style_pad_column(card, 8, 0);

    if(count <= 0) {
        lv_obj_t *note = vqeaf_label(card,
            "Chưa có ảnh. Dùng Chụp màn hình trong Trung tâm điều khiển "
            "hoặc tổ hợp Power + Volume; ảnh lưu tại UserData/Pictures.",
            VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
        lv_obj_set_width(note, lv_pct(100));
        return;
    }
    int thumb = 0;
    for(int i = 0; i < count && i < 24 && thumb < (int)GALLERY_MAX_THUMBS; i++) {
        if(files[i].is_dir) continue;
        char label[160];
        snprintf(g_gallery_paths[i], sizeof(g_gallery_paths[i]),
                 "/Pictures/%s", files[i].name);

        lv_obj_t *tile = vqeaf_card(card, 140, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_event_cb(tile, gallery_tile_cb, LV_EVENT_CLICKED,
                            g_gallery_paths[i]);
        if(gallery_make_thumb(g_gallery_paths[i], thumb) == 0) {
            lv_obj_t *img = lv_image_create(tile);
            lv_image_set_src(img, &g_gallery_thumb_dsc[thumb]);
            lv_obj_set_width(img, lv_pct(100));
        }
        snprintf(label, sizeof(label), "%.18s (%u B)", files[i].name,
                 (unsigned)files[i].size);
        lv_obj_t *lbl = lv_label_create(tile);
        lv_label_set_text(lbl, label);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);
        thumb++;
        shown++;
    }
    if(shown == 0)
        vqeaf_label(card, "Không có tệp ảnh hợp lệ.", VQEAF_FONT_BODY,
                    VQEAF_C_TEXT_DIM);
}

/* ---- recents / multitasking --------------------------------------------- */
static const char *page_name(vqeaf_page_t page)
{
    switch(page) {
    case VQEAF_PAGE_HOME: return "Trang chủ";
    case VQEAF_PAGE_APPS: return "Ứng dụng";
    case VQEAF_PAGE_SETTINGS: return "Cài đặt";
    case VQEAF_PAGE_FILES: return "Tệp";
    case VQEAF_PAGE_INSTALLER: return "Trình cài đặt";
    case VQEAF_PAGE_QEAPP: return "QEAPP";
    case VQEAF_PAGE_BROWSER: return "Trình duyệt";
    case VQEAF_PAGE_NOTES: return "Ghi chú";
    case VQEAF_PAGE_MUSIC: return "Nhạc";
    case VQEAF_PAGE_VIDEO: return "Video";
    case VQEAF_PAGE_WIFI: return "Wi-Fi";
    case VQEAF_PAGE_M3: return "Thiết kế QEAF";
    case VQEAF_PAGE_APPS_EDIT: return "Sửa màn hình chính";
    case VQEAF_PAGE_QEAPP_STORE: return "Chợ QEAPP";
    case VQEAF_PAGE_SECURITY: return "Bảo mật";
    case VQEAF_PAGE_PIN: return "Nhập PIN";
    case VQEAF_PAGE_DEFAULT_APPS: return "Ứng dụng mặc định";
    case VQEAF_PAGE_TERMINAL: return "Terminal";
    case VQEAF_PAGE_KEYBOARD_FEEDBACK: return "Phản hồi bàn phím";
    case VQEAF_PAGE_STORAGE: return "Lưu trữ";
    case VQEAF_PAGE_ABOUT: return "Giới thiệu";
    default: return "Ứng dụng";
    }
}

static void recents_open_cb(lv_event_t *event)
{
    vqeaf_page_t page = (vqeaf_page_t)(intptr_t)lv_event_get_user_data(event);
    if(page < VQEAF_PAGE_COUNT)
        vqeaf_ui_show(page);
}

/* The switcher is the card carousel phones use: the wallpaper shows through,
 * every open task is one rounded card peeking in from both sides, the newest
 * card is centred and snapped, and each card carries its own close button.
 * The order is the real most-recent-first order of this session. */
static const vqeaf_app_t *recents_app(vqeaf_page_t page)
{
    for(int i = 0; i < vqeaf_app_count(); i++) {
        const vqeaf_app_t *app = vqeaf_app_by_id(i);
        if(app != NULL && app->page == page) return app;
    }
    return NULL;
}

static void recents_close_cb(lv_event_t *event)
{
    vqeaf_page_t page = (vqeaf_page_t)(intptr_t)lv_event_get_user_data(event);
    vqeaf_ui_recent_close(page);
    vqeaf_ui_show(VQEAF_PAGE_RECENTS);
}

static void recents_clear_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_ui_recent_clear();
    vqeaf_ui_show(VQEAF_PAGE_RECENTS);
}

static lv_obj_t *recents_card(lv_obj_t *parent, vqeaf_page_t page, int w, int h)
{
    const vqeaf_app_t *app = recents_app(page);
    const lv_image_dsc_t *dsc = app != NULL ? vqeaf_app_icon(app->icon) : NULL;
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_radius(card, 36, 0);
    lv_obj_set_style_bg_color(card, VQEAF_C_SURFACE_2, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, VQEAF_C_LINE, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    vqeaf_m3_elevate(card, 2);

    lv_obj_t *header = vqeaf_row(card);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 12, 0);
    if(dsc != NULL)
        vqeaf_icon_img(header, dsc, 56);
    else
        vqeaf_tile(header, app != NULL ? app->symbol : VQEAF_SYM_APPS,
                   VQEAF_C_SURFACE_3, VQEAF_C_TEXT, 56, 16);
    lv_obj_t *name = vqeaf_label(header, page_name(page), VQEAF_FONT_BODY,
                                VQEAF_C_TEXT);
    lv_obj_set_flex_grow(name, 1);
    lv_obj_t *close = vqeaf_tile(header, VQEAF_SYM_CLOSE, VQEAF_C_SURFACE_3,
                                 VQEAF_C_TEXT_DIM, 48, LV_RADIUS_CIRCLE);
    lv_obj_add_flag(close, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(close, recents_close_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)page);

    /* The preview area is the app's own colour plate, not a fake screenshot:
     * the shell has no per-page frame buffer to replay. */
    lv_obj_t *preview = vqeaf_obj(card);
    lv_obj_set_size(preview, lv_pct(100), 0);
    lv_obj_set_flex_grow(preview, 1);
    lv_obj_set_style_radius(preview, 26, 0);
    lv_obj_set_style_bg_color(preview,
        lv_color_hex(app != NULL ? app->bg : 0x112629), 0);
    lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(preview, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(preview, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(preview, 18, 0);
    if(dsc != NULL)
        vqeaf_icon_img(preview, dsc, 164);
    else
        vqeaf_glyph(preview, app != NULL ? app->symbol : VQEAF_SYM_APPS,
                    VQEAF_ICON, VQEAF_C_TEXT_DIM);
    lv_obj_t *hint = vqeaf_label(preview, "Chạm để mở", VQEAF_FONT_CAPTION,
                                 VQEAF_C_TEXT_DIM);
    lv_obj_set_style_opa(hint, LV_OPA_70, 0);

    vqeaf_on_click(card, recents_open_cb, (void *)(intptr_t)page);
    vqeaf_m3_ripple(card);
    return card;
}

static void build_recents(lv_obj_t *content)
{
    int count = vqeaf_ui_recent_count();
    int32_t height = lv_obj_get_height(content);

    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(content, 0, 0);
    vqeaf_wallpaper(content);

    if(count <= 0) {
        lv_obj_t *root = page_root(content);
        lv_obj_t *card = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 10, 0);
        vqeaf_label(card, "Chưa có ứng dụng gần đây", VQEAF_FONT_TITLE,
                    VQEAF_C_TEXT);
        lv_obj_t *note = vqeaf_label(card,
            "Vuốt lên từ cạnh dưới khi đang dùng ứng dụng để mở bộ chuyển "
            "đổi; mỗi ứng dụng đã mở sẽ thành một thẻ ở đây.",
            VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
        lv_obj_set_width(note, lv_pct(100));
        return;
    }

    lv_obj_t *head = vqeaf_obj(content);
    lv_obj_set_size(head, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(head, VQEAF_PAD, 0);
    lv_obj_set_style_pad_top(head, 24, 0);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *titles = vqeaf_column(head);
    lv_obj_set_style_pad_row(titles, 4, 0);
    vqeaf_label(titles, "Ứng dụng gần đây", VQEAF_FONT_HEADLINE_BOLD,
                VQEAF_C_TEXT);
    vqeaf_label(titles, "Mọi ứng dụng đang mở, chỉ cách một lần vuốt.",
                VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);

    lv_obj_t *clear = vqeaf_obj(head);
    lv_obj_set_size(clear, 196, 66);
    lv_obj_set_style_radius(clear, 33, 0);
    lv_obj_set_style_bg_color(clear, VQEAF_C_SURFACE_2, 0);
    lv_obj_set_style_bg_opa(clear, LV_OPA_90, 0);
    lv_obj_set_flex_flow(clear, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(clear, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(clear, 8, 0);
    lv_obj_add_flag(clear, LV_OBJ_FLAG_CLICKABLE);
    vqeaf_glyph(clear, VQEAF_SYM_DELETE_SWEEP, VQEAF_ICON_SM,
                VQEAF_C_TEXT_DIM);
    vqeaf_label(clear, "Đóng tất cả", VQEAF_FONT_CAPTION, VQEAF_C_TEXT);
    vqeaf_m3_elevate(clear, 1);
    lv_obj_add_event_cb(clear, recents_clear_cb, LV_EVENT_CLICKED, NULL);
    vqeaf_m3_ripple(clear);

    lv_obj_t *row = vqeaf_obj(content);
    lv_obj_set_pos(row, 0, 150);
    lv_obj_set_size(row, lv_pct(100), height - 150 - 96);
    lv_obj_set_style_pad_hor(row, (VQEAF_SCREEN_W - 540) / 2, 0);
    lv_obj_set_style_pad_column(row, 24, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_x(row, LV_SCROLL_SNAP_CENTER);
    lv_obj_update_layout(row);
    int32_t card_h = lv_obj_get_height(row) - 24;
    if(card_h < 200) card_h = 200;
    /* Oldest first, then scroll the newest card into the centre: that is the
     * phone layout where the app you just left sits in front with the previous
     * one peeking in from the left. */
    lv_obj_t *newest = NULL;
    for(int i = count - 1; i >= 0; i--) {
        lv_obj_t *card = recents_card(row, vqeaf_ui_recent_page(i), 540, card_h);
        if(i == 0) newest = card;
    }
    if(newest != NULL) {
        lv_obj_update_layout(row);
        lv_obj_scroll_to_view(newest, LV_ANIM_OFF);
    }

    lv_obj_t *footer = vqeaf_label(content,
        "Chạm thẻ để mở · × để đóng · vuốt qua lại để xem thẻ khác",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_pos(footer, 0, height - 70);
    lv_obj_set_width(footer, lv_pct(100));
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);
}

/* ---- control centre ------------------------------------------------------ */

static void brightness_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);
    lv_obj_t *value = lv_event_get_user_data(event);
    int percent = vqeaf_m3_slider_get(slider);
    char label[24];

    if(percent < 5) {  /* panel floor, same as the previous slider range */
        percent = 5;
        vqeaf_m3_slider_set(slider, percent);
    }
    if(vqeaf_state_set_brightness(percent) == 0)
        snprintf(label, sizeof(label), "%d%%", percent);
    else
        snprintf(label, sizeof(label), "Không hỗ trợ");
    if(value) lv_label_set_text(value, label);
}

typedef enum {
    CTL_WIFI = 0,
    CTL_BLUETOOTH,
    CTL_MOBILE_DATA,
    CTL_HOTSPOT,
    CTL_AIRPLANE,
    CTL_TORCH,
    CTL_LOCATION,
    CTL_DND,
    CTL_BATTERY_SAVER,
    CTL_MODES,
    CTL_ROTATE,
    CTL_DARK,
    CTL_NAVKEYS,
    /* Screen capture: appended so every existing tile index keeps its value. */
    CTL_SHOT,
    CTL_RECORD,
    CTL_COUNT,
} ctl_kind_t;

typedef struct {
    const char *symbol;
    const char *name;
    ctl_kind_t kind;
    int group;
} ctl_def_t;

/* Grouped like the reference panel: Kết nối / Tiện ích / Màn hình. Every tile
 * still reports a real provider state or says "không hỗ trợ" — there is no
 * shell-local toggle pretending to drive a radio the kernel does not expose. */
static const ctl_def_t control_tiles[] = {
    { VQEAF_SYM_WIFI,                "Wi-Fi",            CTL_WIFI,          0 },
    { VQEAF_SYM_BLUETOOTH,           "Bluetooth",        CTL_BLUETOOTH,     0 },
    { VQEAF_SYM_NETWORK_CELL,        "Dữ liệu di động",  CTL_MOBILE_DATA,   0 },
    { VQEAF_SYM_WIFI_TETHERING,      "Phát Wi-Fi",       CTL_HOTSPOT,       0 },
    { VQEAF_SYM_AIRPLANEMODE_ACTIVE, "Máy bay",          CTL_AIRPLANE,      0 },
    { VQEAF_SYM_FLASHLIGHT_ON,       "Đèn pin",          CTL_TORCH,         1 },
    { VQEAF_SYM_LOCATION_ON,         "Vị trí",           CTL_LOCATION,      1 },
    { VQEAF_SYM_BEDTIME,             "Không làm phiền",  CTL_DND,           1 },
    { VQEAF_SYM_BATTERY_SAVER,       "Tiết kiệm pin",    CTL_BATTERY_SAVER, 1 },
    { VQEAF_SYM_MODE,                "Chế độ",           CTL_MODES,         1 },
    { VQEAF_SYM_SCREEN_ROTATION,     "Xoay màn hình",    CTL_ROTATE,        2 },
    { VQEAF_SYM_CONTRAST,            "Chế độ tối",       CTL_DARK,          2 },
    { VQEAF_SYM_APPS,                "Phím ảo",          CTL_NAVKEYS,       2 },
    { VQEAF_SYM_IMAGE,               "Chụp màn hình",    CTL_SHOT,          2 },
    { VQEAF_SYM_MOVIE,               "Quay màn hình",    CTL_RECORD,        2 },
};

static const char *const control_groups[] = {
    "Kết nối", "Tiện ích", "Màn hình"
};

/* Tile editing only changes the visible list in RAM; nothing is persisted. */
static int g_control_hidden[CTL_COUNT];
static int g_control_edit;

static void control_done_cb(lv_event_t *event)
{
    (void)event;
    g_control_edit = 0;
    vqeaf_ui_show(VQEAF_PAGE_CONTROL);
}

static void control_reset_cb(lv_event_t *event)
{
    (void)event;
    for(int i = 0; i < (int)CTL_COUNT; i++) g_control_hidden[i] = 0;
    vqeaf_ui_show(VQEAF_PAGE_CONTROL);
}

static void control_edit_cb(lv_event_t *event)
{
    (void)event;
    g_control_edit = 1;
    vqeaf_ui_show(VQEAF_PAGE_CONTROL);
}

static void control_remove_cb(lv_event_t *event)
{
    ctl_kind_t kind = (ctl_kind_t)(intptr_t)lv_event_get_user_data(event);
    if((int)kind >= 0 && (int)kind < (int)CTL_COUNT)
        g_control_hidden[(int)kind] = 1;
    vqeaf_ui_show(VQEAF_PAGE_CONTROL);
}

static void control_tile_cb(lv_event_t *event)
{
    ctl_kind_t kind = (ctl_kind_t)(intptr_t)lv_event_get_user_data(event);

    switch(kind) {
    case CTL_WIFI:
        vqeaf_ui_show(VQEAF_PAGE_WIFI);
        break;
    case CTL_ROTATE:
        vqeaf_ui_set_auto_rotate(!vqeaf_ui_auto_rotate());
        vqeaf_ui_show(VQEAF_PAGE_CONTROL);
        break;
    case CTL_DARK:
        vqeaf_ui_set_theme(vqeaf_theme_get() == VQEAF_THEME_DARK ?
                           VQEAF_THEME_LIGHT : VQEAF_THEME_DARK);
        break;
    case CTL_NAVKEYS:
        vqeaf_ui_set_virtual_keys(!vqeaf_ui_virtual_keys());
        vqeaf_ui_show(VQEAF_PAGE_CONTROL);
        break;
    case CTL_SHOT: {
        /* A real capture of the live scanout, encoded and written before the
         * toast answers: no placeholder is ever shown as a screenshot. */
        int bytes = vio_screenshot_now();
        vqeaf_ui_show(VQEAF_PAGE_CONTROL);
        if(bytes > 0)
            show_toast("Đã lưu ảnh vào UserData/Pictures");
        else
            show_toast("Không chụp được màn hình");
        break;
    }
    case CTL_RECORD:
        if(vio_rec_active()) {
            vio_rec_stop();
            show_toast("Đã lưu video vào UserData/Videos");
        }
        else if(vio_rec_start(8, 10) == 0) {
            show_toast("Đang quay màn hình 10 giây");
        }
        else {
            show_toast("Không quay được màn hình");
        }
        vqeaf_ui_show(VQEAF_PAGE_CONTROL);
        break;
    case CTL_BLUETOOTH:
        show_toast("Chưa có lệnh điều khiển Bluetooth trên thiết bị này.");
        break;
    case CTL_MOBILE_DATA:
        show_toast("Chưa có modem di động trên thiết bị này.");
        break;
    case CTL_HOTSPOT:
        show_toast("Chưa có phát Wi-Fi trên thiết bị này.");
        break;
    case CTL_LOCATION:
        show_toast("Chưa có GNSS trên thiết bị này.");
        break;
    case CTL_MODES:
        show_toast("Chưa có dịch vụ chế độ trên thiết bị này.");
        break;
    default:
        show_toast("Thiết bị không hỗ trợ chức năng này.");
        break;
    }
}

static void volume_slider_cb(lv_event_t *event)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(event);
    lv_obj_t *value = (lv_obj_t *)lv_event_get_user_data(event);
    int v = vqeaf_m3_slider_get(slider);
    vqeaf_state_set_volume(v);
    if(value != NULL) {
        char text[8];
        snprintf(text, sizeof(text), "%d%%", v);
        lv_label_set_text(value, text);
    }
    vqeaf_ui_volume_show();
}

static void notifications_clear_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_ui_notification_clear();
    vqeaf_ui_show(VQEAF_PAGE_CONTROL);
}

static void control_power_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_ui_power_menu_show();
}

static void build_control(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);
    lv_obj_t *head = page_title(root, "Trung tâm điều khiển");
    if(g_control_edit) {
        lv_obj_t *reset = vqeaf_label(head, "Đặt lại", VQEAF_FONT_BODY,
                                      VQEAF_C_TEXT_DIM);
        vqeaf_on_click(reset, control_reset_cb, NULL);
        lv_obj_t *done = vqeaf_label(head, "Xong", VQEAF_FONT_BODY_BOLD,
                                     VQEAF_C_ACCENT);
        vqeaf_on_click(done, control_done_cb, NULL);
    }
    else {
        lv_obj_t *edit = vqeaf_label(head, "Sửa", VQEAF_FONT_BODY,
                                     VQEAF_C_ACCENT);
        vqeaf_on_click(edit, control_edit_cb, NULL);
    }

    const size_t tile_count = sizeof(control_tiles) / sizeof(control_tiles[0]);
    for(int group = 0;
        group < (int)(sizeof(control_groups) / sizeof(control_groups[0]));
        group++) {
        int visible = 0;
        for(size_t i = 0; i < tile_count; i++)
            if(control_tiles[i].group == group &&
               !g_control_hidden[control_tiles[i].kind]) visible++;
        if(visible == 0) continue;

        lv_obj_t *section = vqeaf_row(root);
        lv_obj_set_style_pad_top(section, 6, 0);
        vqeaf_label(section, control_groups[group], VQEAF_FONT_TITLE,
                    VQEAF_C_TEXT);
        if(g_control_edit)
            vqeaf_label(section, "Chạm vào ô để bỏ", VQEAF_FONT_CAPTION,
                        VQEAF_C_TEXT_MUTE);

        lv_obj_t *grid = vqeaf_obj(root);
        lv_obj_set_width(grid, lv_pct(100));
        lv_obj_set_height(grid, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_column(grid, 16, 0);
        lv_obj_set_style_pad_row(grid, 16, 0);

        for(size_t i = 0; i < tile_count; i++) {
            const ctl_def_t *def = &control_tiles[i];
            if(def->group != group || g_control_hidden[def->kind]) continue;
            int on = 0;
            int supported = 1;
            const char *status = "Không hỗ trợ";

            switch(def->kind) {
            case CTL_WIFI:
                on = vqeaf_state.wifi_up > 0;
                supported = vqeaf_state.wifi_up >= 0;
                status = wifi_text();
                break;
            case CTL_BLUETOOTH:
                on = vqeaf_state.bt_up > 0;
                supported = vqeaf_state.bt_up >= 0;
                status = vqeaf_state.bt_up > 0 ? "Đang bật" :
                         vqeaf_state.bt_up == 0 ? "Đang tắt" :
                         "Không có thiết bị";
                break;
            case CTL_ROTATE:
                on = vqeaf_ui_auto_rotate();
                status = on ? "Tự động" : "Đang tắt";
                break;
            case CTL_DARK:
                on = vqeaf_theme_get() == VQEAF_THEME_DARK;
                status = on ? "Đang bật" : "Đang tắt";
                break;
            case CTL_NAVKEYS:
                on = vqeaf_ui_virtual_keys();
                status = on ? "Đang bật" : "Cử chỉ";
                break;
            case CTL_SHOT:
                status = "Lưu vào Pictures";
                break;
            case CTL_RECORD:
                on = vio_rec_active();
                status = on ? "Đang quay" : "10 giây, 8 fps";
                break;
            default:
                supported = 0;
                status = "Không hỗ trợ";
                break;
            }

            lv_obj_t *tile = vqeaf_obj(grid);
            lv_obj_set_size(tile, 328, 128);
            lv_obj_set_style_bg_color(tile, on ? lv_color_hex(0x0F3B36) :
                                      VQEAF_C_SURFACE, 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(tile, 30, 0);
            lv_obj_set_style_border_width(tile, 1, 0);
            lv_obj_set_style_border_color(tile, on ? VQEAF_C_ACCENT :
                                          VQEAF_C_LINE, 0);
            lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_left(tile, 18, 0);
            lv_obj_set_style_pad_column(tile, 16, 0);

            vqeaf_tile(tile, def->symbol,
                       on ? lv_color_hex(0x123F3A) : VQEAF_C_SURFACE_3,
                       on ? VQEAF_C_ACCENT :
                       (supported ? VQEAF_C_TEXT_DIM : VQEAF_C_TEXT_MUTE),
                       66, 18);
            lv_obj_t *labels = vqeaf_column(tile);
            lv_obj_set_flex_grow(labels, 1);
            lv_obj_set_style_pad_row(labels, 4, 0);
            vqeaf_label(labels, def->name, VQEAF_FONT_BODY,
                        supported ? VQEAF_C_TEXT : VQEAF_C_TEXT_MUTE);
            if(g_control_edit) {
                lv_obj_t *remove = vqeaf_label(labels, "× Bỏ",
                                               VQEAF_FONT_CAPTION,
                                               VQEAF_C_ACCENT);
                vqeaf_on_click(remove, control_remove_cb,
                               (void *)(intptr_t)def->kind);
            }
            else {
                vqeaf_label(labels, status, VQEAF_FONT_CAPTION,
                            supported ? VQEAF_C_TEXT_MUTE : VQEAF_C_TEXT_MUTE);
                lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
                vqeaf_on_click(tile, control_tile_cb,
                               (void *)(intptr_t)def->kind);
                vqeaf_m3_ripple(tile);
            }
        }
    }

    /* Backlight and volume in one card: both write real state (the backlight
     * sysfs node, the shell's volume model). There is no audio codec, so the
     * volume slider drives the HUD only and the card says that. */
    lv_obj_t *audio = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(audio, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(audio, 8, 0);

    lv_obj_t *bright_head = vqeaf_row(audio);
    vqeaf_label(bright_head, "Độ sáng màn hình", VQEAF_FONT_BODY,
                VQEAF_C_TEXT);
    char bright_text[8];
    snprintf(bright_text, sizeof(bright_text), "%d%%", vqeaf_state.brightness);
    lv_obj_t *bright_value = vqeaf_label(bright_head, bright_text,
                                         VQEAF_FONT_BODY, VQEAF_C_ACCENT);
    lv_obj_t *slider = vqeaf_m3_slider(audio, vqeaf_state.brightness);
    lv_obj_add_event_cb(slider, brightness_cb, LV_EVENT_VALUE_CHANGED,
                        bright_value);

    lv_obj_t *vol_head = vqeaf_row(audio);
    vqeaf_label(vol_head, "Âm lượng", VQEAF_FONT_BODY, VQEAF_C_TEXT);
    char vol_text[8];
    snprintf(vol_text, sizeof(vol_text), "%d%%", vqeaf_state.volume);
    lv_obj_t *vol_value = vqeaf_label(vol_head, vol_text, VQEAF_FONT_BODY,
                                      VQEAF_C_ACCENT);
    lv_obj_t *vol_slider = vqeaf_m3_slider(audio, vqeaf_state.volume);
    lv_obj_add_event_cb(vol_slider, volume_slider_cb,
                        LV_EVENT_VALUE_CHANGED, vol_value);
    vqeaf_label(audio, "Không có bộ giải mã âm thanh; mức âm lượng chỉ là HUD.",
                VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);

    lv_obj_t *media = vqeaf_card(root, lv_pct(100), 132);
    lv_obj_set_flex_flow(media, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(media, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(media, 18, 0);
    vqeaf_tile(media, VQEAF_SYM_MUSIC_NOTE, VQEAF_C_SURFACE_3,
               VQEAF_C_TEXT_DIM, 68, 20);
    lv_obj_t *media_text = vqeaf_column(media);
    lv_obj_set_flex_grow(media_text, 1);
    lv_obj_set_style_pad_row(media_text, 2, 0);
    vqeaf_label(media_text, "Phương tiện", VQEAF_FONT_CAPTION,
                VQEAF_C_TEXT_MUTE);
    vqeaf_label(media_text, "Không có nhạc đang phát", VQEAF_FONT_BODY,
                VQEAF_C_TEXT);
    vqeaf_glyph(media, VQEAF_SYM_PAUSE, VQEAF_ICON, VQEAF_C_TEXT_MUTE);

    /* Notification log: real events only, newest first. */
    lv_obj_t *note_head = vqeaf_row(root);
    vqeaf_label(note_head, "Thông báo", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    if(vqeaf_ui_notification_count() > 0) {
        lv_obj_t *clear = vqeaf_label(note_head, "Xoá", VQEAF_FONT_BODY,
                                      VQEAF_C_ACCENT);
        vqeaf_on_click(clear, notifications_clear_cb, NULL);
    }

    lv_obj_t *notes = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(notes, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(notes, 12, 0);
    int note_count = vqeaf_ui_notification_count();
    if(note_count == 0) {
        lv_obj_t *empty = vqeaf_row(notes);
        lv_obj_set_flex_align(empty, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(empty, 12, 0);
        vqeaf_glyph(empty, VQEAF_SYM_NOTIFICATIONS, VQEAF_ICON_SM,
                    VQEAF_C_TEXT_MUTE);
        vqeaf_label(empty, "Chưa có sự kiện nào trong phiên này.",
                    VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    }
    else {
        int shown = note_count < 3 ? note_count : 3;
        for(int i = 0; i < shown; i++) {
            const vqeaf_notification_t *note = vqeaf_ui_notification_at(i);
            if(note == NULL) break;
            lv_obj_t *row = vqeaf_row(notes);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(row, 12, 0);
            vqeaf_tile(row, note->symbol[0] ? note->symbol :
                       VQEAF_SYM_NOTIFICATIONS, lv_color_hex(note->color),
                       VQEAF_C_BG, 48, 15);
            lv_obj_t *text = vqeaf_column(row);
            lv_obj_set_flex_grow(text, 1);
            lv_obj_set_style_pad_row(text, 1, 0);
            vqeaf_label(text, note->title, VQEAF_FONT_CAPTION, VQEAF_C_TEXT);
            lv_obj_t *body = vqeaf_label(text, note->body, VQEAF_FONT_CAPTION,
                                         VQEAF_C_TEXT_DIM);
            lv_obj_set_width(body, lv_pct(100));
            lv_label_set_long_mode(body, LV_LABEL_LONG_DOT);
            char ago[32];
            format_uptime(note->uptime_s, ago, sizeof(ago));
            vqeaf_label(row, ago, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
        }
    }

    lv_obj_t *actions = vqeaf_row(root);
    lv_obj_t *settings = vqeaf_m3_button(actions, "Cài đặt",
                                         VQEAF_M3_TONAL);
    lv_obj_add_event_cb(settings, nav_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)VQEAF_PAGE_SETTINGS);
    lv_obj_t *power = vqeaf_m3_button(actions, "Nguồn", VQEAF_M3_ELEVATED);
    lv_obj_add_event_cb(power, control_power_cb, LV_EVENT_CLICKED, NULL);
}

/* ---- settings ------------------------------------------------------------ */

typedef struct {
    const char *symbol;
    const char *title;
    const char *status;
    uint32_t color;
    /* Page opened by the row, or VQEAF_PAGE_COUNT when the device has no
     * backend for it; such a row is inert and says why. */
    vqeaf_page_t page;
} setting_def_t;

static void build_setting_row(lv_obj_t *parent, const setting_def_t *def)
{
    lv_obj_t *row = vqeaf_card(parent, lv_pct(100), 124);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 18, 0);
    lv_obj_set_style_pad_left(row, 18, 0);
    lv_obj_set_style_pad_right(row, 18, 0);

    vqeaf_tile(row, def->symbol, lv_color_hex(def->color), VQEAF_C_TEXT,
               68, 20);

    lv_obj_t *text = vqeaf_column(row);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_style_pad_row(text, 2, 0);
    vqeaf_label(text, def->title, VQEAF_FONT_BODY, VQEAF_C_TEXT);
    if(def->status != NULL)
        vqeaf_label(text, def->status, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);

    if(def->page != VQEAF_PAGE_COUNT) {
        vqeaf_glyph(row, VQEAF_SYM_CHEVRON_RIGHT, VQEAF_ICON_SM,
                    VQEAF_C_TEXT_MUTE);
        vqeaf_on_click(row, nav_cb, (void *)(intptr_t)def->page);
        vqeaf_m3_ripple(row);
    }
    else {
        /* A muted dot instead of a chevron: nothing will open. */
        vqeaf_label(row, "·", VQEAF_FONT_BODY, VQEAF_C_TEXT_MUTE);
    }
}

static void screen_timeout_cycle_cb(lv_event_t *event)
{
    static const int timeouts[] = { 30, 60, 300, 1800, 0 };
    static const char *names[] = { "30 giây", "1 phút", "5 phút", "30 phút",
                                    "Không bao giờ" };
    lv_obj_t *value = (lv_obj_t *)lv_event_get_user_data(event);
    int current = vqeaf_state.screen_timeout_s;
    int idx = -1;
    for(int i = 0; i < 5; i++)
        if(timeouts[i] == current) { idx = (i + 1) % 5; break; }
    /* An out-of-table value (e.g. a test clock) starts from 30 giây instead
     * of silently jumping to "Không bao giờ". */
    if(idx < 0) idx = 0;
    vqeaf_state_set_screen_timeout(timeouts[idx]);
    vqeaf_ui_activity();
    if(value != NULL) lv_label_set_text(value, names[idx]);
}

static void theme_toggle_cb(lv_event_t *event)
{
    lv_obj_t *value = (lv_obj_t *)lv_event_get_user_data(event);
    vqeaf_theme_mode_t mode = vqeaf_theme_get() == VQEAF_THEME_LIGHT ?
                              VQEAF_THEME_DARK : VQEAF_THEME_LIGHT;
    vqeaf_theme_set(mode);
    if(value != NULL)
        lv_label_set_text(value, mode == VQEAF_THEME_LIGHT ? "Sáng" : "Tối");
    vqeaf_ui_show(vqeaf_ui_page());
}

static void liquid_glass_toggle_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_theme_set_liquid_glass(!vqeaf_theme_liquid_glass());
    vqeaf_ui_show(vqeaf_ui_page());
}

/* Dynamic colour: the seed is the payload, so one card of swatches covers the
 * whole palette. Rebuilds the page because every style was built from the old
 * one (the same path the dark/light toggle uses). */
static void dynamic_color_apply(uint32_t seed)
{
    vqeaf_theme_set_seed(seed);
    vqeaf_ui_show(vqeaf_ui_page());
}

static void dynamic_color_cb(lv_event_t *event)
{
    dynamic_color_apply((uint32_t)(uintptr_t)lv_event_get_user_data(event));
}

/* Material You automatic mode: the palette follows the wallpaper's dominant
 * colour. Toggling it rebuilds the page, exactly like a manual seed, so the
 * nav bar, buttons and backgrounds all pick up the derived roles. */
static void auto_color_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_theme_set_auto(vqeaf_theme_auto() ? 0 : 1);
    vqeaf_ui_show(vqeaf_ui_page());
}

static void bold_toggle_cb(lv_event_t *event)
{
    lv_obj_t *value = (lv_obj_t *)lv_event_get_user_data(event);
    int on = vqeaf_type_get_bold() ? 0 : 1;
    vqeaf_type_set_bold(on);
    if(value != NULL) lv_label_set_text(value, on ? "Bật" : "Tắt");
    vqeaf_ui_show(vqeaf_ui_page());
}

/* Material 3 gallery overlays. The dialog and snackbar are live components on
 * the top layer, not pictures: dismiss/action really close them and the dialog
 * confirm really resets the dynamic colour (the same path the swatches use). */
static void m3_overlay_close_cb(lv_event_t *event)
{
    lv_obj_t *overlay = (lv_obj_t *)lv_event_get_user_data(event);
    if(overlay != NULL) lv_obj_delete(overlay);
    lv_obj_invalidate(lv_layer_top());
}

static void m3_gallery_reset_cb(lv_event_t *event)
{
    lv_obj_t *scrim = (lv_obj_t *)lv_event_get_user_data(event);
    if(scrim != NULL) lv_obj_delete(scrim);
    dynamic_color_apply(0);
}

static void m3_gallery_dialog_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_t *confirm = NULL, *dismiss = NULL;
    lv_obj_t *scrim = vqeaf_m3_dialog(lv_layer_top(), "Đặt lại bảng màu?",
        "Bảng màu động trở về mặc định. Chỉ áp dụng trong phiên này.",
        "Đặt lại", "Hủy", &confirm, &dismiss);
    lv_obj_add_event_cb(scrim, m3_overlay_close_cb, LV_EVENT_CLICKED, scrim);
    if(dismiss != NULL)
        lv_obj_add_event_cb(dismiss, m3_overlay_close_cb, LV_EVENT_CLICKED, scrim);
    if(confirm != NULL)
        lv_obj_add_event_cb(confirm, m3_gallery_reset_cb, LV_EVENT_CLICKED, scrim);
    lv_obj_invalidate(lv_layer_top());
}

static void m3_gallery_snackbar_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_t *action = NULL;
    lv_obj_t *bar = vqeaf_m3_snackbar(lv_layer_top(),
        "Bảng màu chỉ áp dụng trong phiên này.", "Ẩn", &action);
    if(action != NULL)
        lv_obj_add_event_cb(action, m3_overlay_close_cb, LV_EVENT_CLICKED, bar);
    lv_obj_invalidate(lv_layer_top());
}

/* The gallery navigation bar drives the real shell navigation, so selecting an
 * item actually changes page; "Cài đặt" is the current page (selected). */
static void m3_gallery_nav_cb(lv_event_t *event)
{
    vqeaf_page_t page = (vqeaf_page_t)(intptr_t)lv_event_get_user_data(event);
    vqeaf_ui_show(page);
}

/* ---- Material components ------------------------------------------------- */

/* The page is the reference layout of the design system, ordered the way
 * Material 3 orders surfaces: a pinned top app bar (level 0 at rest, 2 once
 * the body scrolls) above a scrolling body whose cards climb the elevation
 * scale (filled 0, elevated 1, raised panel 3), an extended FAB at level 3
 * floating over the body, and a dialog on the 40% scrim of the top layer above
 * everything. */
static void m3_layout_snackbar_cb(lv_event_t *event)
{
    lv_obj_t *action = NULL;
    lv_obj_t *bar = vqeaf_m3_snackbar(lv_layer_top(),
        "Đã tạo thẻ mẫu. Thẻ không được lưu: chưa có backend trên thiết bị.",
        "Ẩn", &action);
    if(action != NULL)
        lv_obj_add_event_cb(action, m3_overlay_close_cb, LV_EVENT_CLICKED, bar);
    lv_obj_invalidate(lv_layer_top());
    (void)event;
}

static void m3_layout_confirm_cb(lv_event_t *event)
{
    lv_obj_t *scrim = (lv_obj_t *)lv_event_get_user_data(event);
    if(scrim != NULL) lv_obj_delete(scrim);
    m3_layout_snackbar_cb(NULL);
}

static void m3_layout_dialog_cb(lv_event_t *event)
{
    lv_obj_t *confirm = NULL, *dismiss = NULL;
    lv_obj_t *scrim = vqeaf_m3_dialog(lv_layer_top(), "Tạo thẻ mới?",
        "Hộp thoại ở lớp trên cùng, trên lớp phủ 40%, mang elevation 3 "
        "(6 dp) — cao hơn app bar, thẻ và FAB của trang.",
        "Tạo", "Huỷ", &confirm, &dismiss);
    lv_obj_add_event_cb(scrim, m3_overlay_close_cb, LV_EVENT_CLICKED, scrim);
    if(dismiss != NULL)
        lv_obj_add_event_cb(dismiss, m3_overlay_close_cb, LV_EVENT_CLICKED, scrim);
    if(confirm != NULL)
        lv_obj_add_event_cb(confirm, m3_layout_confirm_cb, LV_EVENT_CLICKED, scrim);
    lv_obj_invalidate(lv_layer_top());
    (void)event;
}

/* M3: the app bar sits flat at rest and lifts to level 2 as soon as content
 * scrolls under it. */
static void m3_layout_scroll_cb(lv_event_t *event)
{
    lv_obj_t *body = lv_event_get_target_obj(event);
    lv_obj_t *bar = (lv_obj_t *)lv_event_get_user_data(event);
    if(bar == NULL) return;
    vqeaf_m3_elevate(bar, lv_obj_get_scroll_y(body) > 4 ? 2 : 0);
}

static void m3_layout_card(lv_obj_t *parent, const char *tag,
                           const char *title, const char *text,
                           vqeaf_m3_card_style_t style)
{
    lv_obj_t *card = vqeaf_m3_card(parent, lv_pct(100), LV_SIZE_CONTENT,
                                   style);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);
    vqeaf_label(card, tag, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    vqeaf_label(card, title, VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    lv_obj_t *body = vqeaf_label(card, text, VQEAF_FONT_BODY,
                                 VQEAF_C_TEXT_DIM);
    lv_obj_set_width(body, lv_pct(100));
}

static void build_m3_layout(lv_obj_t *content)
{
    int32_t body_h;
    lv_obj_t *bar, *body, *intro, *trailing;

    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(content, 0, 0);

    bar = vqeaf_m3_top_app_bar(content, "Thiết kế QEAF",
                               VQEAF_SYM_ARROW_BACK, NULL, nav_cb,
                               (void *)(intptr_t)VQEAF_PAGE_SETTINGS);
    lv_obj_set_pos(bar, 0, 0);
    vqeaf_m3_elevate(bar, 0);

    body = vqeaf_obj(content);
    body_h = (int32_t)lv_obj_get_height(content) - 64;
    if(body_h < 240) body_h = 240;
    lv_obj_set_pos(body, 0, 64);
    lv_obj_set_size(body, lv_pct(100), body_h);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, VQEAF_PAD, 0);
    lv_obj_set_style_pad_row(body, 20, 0);
    lv_obj_set_style_pad_bottom(body, 150, 0);
    lv_obj_add_event_cb(body, m3_layout_scroll_cb, LV_EVENT_SCROLL, bar);

    intro = vqeaf_label(body,
        "Xếp theo đúng thứ bậc thị giác của QEAF: app bar phẳng khi ở "
        "đầu trang rồi nổi lên khi nội dung cuộn qua, thẻ tăng dần độ nổi, "
        "FAB nổi trên nội dung, hộp thoại nằm trên lớp phủ ở lớp cao nhất. "
        "Thang elevation: 0 · 1 · 3 · 6 · 8 · 12 dp.",
        VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
    lv_obj_set_width(intro, lv_pct(100));

    m3_layout_card(body, "Thẻ filled · elevation 0 · 0 dp", "Bề mặt nền",
                   "Thẻ phẳng, không đổ bóng: lớp thấp nhất, dùng cho nội dung "
                   "chính chạy trong trang.", VQEAF_M3_CARD_FILLED);
    m3_layout_card(body, "Thẻ elevated · elevation 1 · 1 dp",
                   "Bề mặt nhấc nhẹ",
                   "Bóng mờ 2 px, lệch 1 px: đủ để tách khỏi nền mà không "
                   "tranh chấp với FAB.", VQEAF_M3_CARD_ELEVATED);

    lv_obj_t *raised = vqeaf_m3_card(body, lv_pct(100), LV_SIZE_CONTENT,
                                     VQEAF_M3_CARD_ELEVATED);
    lv_obj_set_style_bg_color(raised, VQEAF_C_SURFACE_CONTAINER_HIGH, 0);
    vqeaf_m3_elevate(raised, 3);
    lv_obj_set_flex_flow(raised, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(raised, 10, 0);
    vqeaf_label(raised, "Bảng nổi · elevation 3 · 6 dp", VQEAF_FONT_CAPTION,
                VQEAF_C_TEXT_MUTE);
    vqeaf_label(raised, "Hộp thoại & Snackbar", VQEAF_FONT_TITLE,
                VQEAF_C_TEXT);
    lv_obj_t *raised_note = vqeaf_label(raised,
        "Cùng độ nổi với FAB và hộp thoại trong chuẩn M3: hai nút dưới đây mở "
        "đúng component thật trên lớp trên cùng.",
        VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
    lv_obj_set_width(raised_note, lv_pct(100));
    lv_obj_t *actions = vqeaf_row(raised);
    lv_obj_set_style_pad_column(actions, 12, 0);
    lv_obj_t *b_dialog = vqeaf_m3_button(actions, "Hộp thoại",
                                         VQEAF_M3_FILLED);
    lv_obj_add_event_cb(b_dialog, m3_layout_dialog_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *b_snack = vqeaf_m3_button(actions, "Snackbar", VQEAF_M3_TONAL);
    lv_obj_add_event_cb(b_snack, m3_layout_snackbar_cb, LV_EVENT_CLICKED, NULL);

    vqeaf_label(body, "Danh sách · bề mặt phẳng, không nổi",
                VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    trailing = NULL;
    vqeaf_m3_list_item(body, VQEAF_SYM_NOTIFICATIONS, "Thông báo đẩy",
                       "Chưa có dịch vụ trên thiết bị", &trailing);
    trailing = NULL;
    lv_obj_t *switch_item = vqeaf_m3_list_item(body, VQEAF_SYM_BRIGHTNESS_6,
        "Nền tối theo giờ", "Bật là đổi ngay giao diện Sáng/Tối",
        &trailing);
    if(trailing != NULL)
        vqeaf_m3_switch(trailing, vqeaf_theme_get() == VQEAF_THEME_DARK);
    lv_obj_add_event_cb(switch_item, theme_toggle_cb, LV_EVENT_CLICKED, NULL);

    /* The FAB is created after the body, so it paints above it: level 3 over a
     * level 0..1 surface is the whole point of the elevation scale. */
    lv_obj_t *fab = vqeaf_m3_fab_extended(content, VQEAF_SYM_ADD, "Tạo");
    /* Above the gesture pill: a FAB sitting in the system gesture strip would
     * be swallowed by the home gesture instead of receiving the tap. */
    lv_obj_align(fab, LV_ALIGN_BOTTOM_RIGHT, -28, -84);
    lv_obj_add_event_cb(fab, m3_layout_dialog_cb, LV_EVENT_CLICKED, NULL);
}

/* ---- USB mode row --------------------------------------------------------
 * Settings exposes the same chooser the cable sheet shows, so the mode can be
 * changed at any time and not only on a plug event. The row displays the mode
 * the platform applied (never a guess) and the sheet still only reports a
 * pick: main remains the one writer of the gadget. */
static const char *usb_mode_text(void)
{
    switch(vqeaf_ui_usb_mode()) {
    case VQEAF_USB_FILE:   return "Truyền tệp · ổ QEAFRESCUE";
    case VQEAF_USB_CHARGE: return "Chỉ sạc · máy ẩn khỏi máy tính";
    default:               return "Không có USB trên thiết bị này";
    }
}

static void usb_row_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_ui_activity();
    vqeaf_ui_usb_dialog_show();
}

static void build_settings(lv_obj_t *content)
{
    static char wifi_status[32];
    lv_obj_t *root = page_root(content);
    /* The dock floats over the bottom of the scroll area: leave enough tail
     * padding that the timeout note can be scrolled fully above it. */
    lv_obj_set_style_pad_bottom(root, 140, 0);
    page_title(root, "Cài đặt");
    search_pill(root, "Tìm ứng dụng trên thiết bị...");

    if(vqeaf_state.wifi_up > 0)
        snprintf(wifi_status, sizeof(wifi_status), "Đang bật");
    else if(vqeaf_state.wifi_up == 0)
        snprintf(wifi_status, sizeof(wifi_status), "Đang tắt");
    else
        snprintf(wifi_status, sizeof(wifi_status), "Không có thiết bị");

    {
        lv_obj_t *usb_row = vqeaf_card(root, lv_pct(100), 124);
        lv_obj_t *usb_text;

        lv_obj_set_flex_flow(usb_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(usb_row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(usb_row, 18, 0);
        lv_obj_set_style_pad_left(usb_row, 18, 0);
        lv_obj_set_style_pad_right(usb_row, 18, 0);
        vqeaf_tile(usb_row, VQEAF_SYM_LINK, lv_color_hex(0x1A4A5A),
                   VQEAF_C_TEXT, 68, 20);
        usb_text = vqeaf_column(usb_row);
        lv_obj_set_flex_grow(usb_text, 1);
        lv_obj_set_style_pad_row(usb_text, 2, 0);
        vqeaf_label(usb_text, "USB", VQEAF_FONT_BODY, VQEAF_C_TEXT);
        live_usb = vqeaf_label(usb_text, usb_mode_text(), VQEAF_FONT_CAPTION,
                               VQEAF_C_TEXT_DIM);
        vqeaf_glyph(usb_row, VQEAF_SYM_CHEVRON_RIGHT, VQEAF_ICON_SM,
                    VQEAF_C_TEXT_MUTE);
        lv_obj_add_flag(usb_row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(usb_row, usb_row_cb, LV_EVENT_CLICKED, NULL);
        vqeaf_m3_ripple(usb_row);
    }

    const setting_def_t rows[] = {
        { VQEAF_SYM_WIFI,             "Wi-Fi",             wifi_status,        0x1A5A6A, VQEAF_PAGE_WIFI },
        { VQEAF_SYM_BLUETOOTH,        "Bluetooth",         "Không có thiết bị", 0x1A3A6A, VQEAF_PAGE_COUNT },
        { VQEAF_SYM_SIGNAL_CELLULAR_4_BAR, "Mạng di động", "Không có thiết bị", 0x1A4A3A, VQEAF_PAGE_COUNT },
        { VQEAF_SYM_AIR,              "Điểm phát sóng",    "Tắt",              0x5A3A1A, VQEAF_PAGE_COUNT },
        { VQEAF_SYM_BRIGHTNESS_6,     "Hiển thị",          "Độ sáng màn hình", 0x3A2A5A, VQEAF_PAGE_CONTROL },
        { VQEAF_SYM_STORAGE,          "Lưu trữ",           NULL,               0x3A2E14, VQEAF_PAGE_STORAGE },
        { VQEAF_SYM_MEMORY,           "Bộ nhớ",            NULL,               0x1A5A5A, VQEAF_PAGE_STORAGE },
        { VQEAF_SYM_BUILD,            "Trình điều khiển",  "DRM, cảm ứng, USB, ổ đĩa", 0x1E2A2C, VQEAF_PAGE_DRIVERS },
        { VQEAF_SYM_KEYBOARD,         "Bàn phím",          "Bố cục mặc định · English", 0x2A2E6E, VQEAF_PAGE_KEYBOARD },
        { VQEAF_SYM_VOLUME_UP,        "Âm thanh & rung",   "Chưa có dịch vụ",  0x5A1A2A, VQEAF_PAGE_COUNT },
        { VQEAF_SYM_NOTIFICATIONS,    "Thông báo",         "Chưa có dịch vụ",  0x5A4A1A, VQEAF_PAGE_COUNT },
        { VQEAF_SYM_SECURITY,         "Bảo mật",           "PIN · vân tay · khóa túi", 0x1A5A3A, VQEAF_PAGE_SECURITY },
        { VQEAF_SYM_APPS,             "Ứng dụng mặc định", "Trình duyệt · nhạc · ảnh · tệp", 0x1A3A5A, VQEAF_PAGE_DEFAULT_APPS },
        { VQEAF_SYM_APPS,             "Ứng dụng",          NULL,               0x1A3A5A, VQEAF_PAGE_APPS },
        { VQEAF_SYM_MEMORY,           "Hệ thống",          "Chưa có dịch vụ",  0x3A3A3A, VQEAF_PAGE_COUNT },
        { VQEAF_SYM_LOCK,             "Màn hình khóa",     "Đồng hồ, font & màu", 0x2A1A3E, VQEAF_PAGE_LOCK_THEME },
        { VQEAF_SYM_PALETTE,          "Bộ thành phần QEAF", "App bar · thẻ · FAB · hộp thoại", 0x2A1A3E, VQEAF_PAGE_M3 },
        { VQEAF_SYM_FOLDER_OPEN,      "Tệp & đồng bộ",     "QEAFRESCUE",       0x3A2E14, VQEAF_PAGE_SYNC },
        { VQEAF_SYM_INFO,             "Giới thiệu VQEAF OS", NULL,             0x1A5A5A, VQEAF_PAGE_ABOUT },
    };
    for(size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++)
        build_setting_row(root, &rows[i]);

    static const int timeouts[] = { 30, 60, 300, 1800, 0 };
    static const char *timeout_names[] = { "30 giây", "1 phút", "5 phút",
                                          "30 phút", "Không bao giờ" };
    lv_obj_t *timeout_row = vqeaf_card(root, lv_pct(100), 88);
    lv_obj_set_flex_flow(timeout_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(timeout_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(timeout_row, 14, 0);
    lv_obj_add_flag(timeout_row, LV_OBJ_FLAG_CLICKABLE);
    vqeaf_tile(timeout_row, VQEAF_SYM_SCHEDULE, lv_color_hex(0x3A2A5A),
               VQEAF_C_ACCENT, 54, 17);
    vqeaf_label(timeout_row, "Tắt màn hình", VQEAF_FONT_BODY, VQEAF_C_TEXT);
    int timeout_idx = 1;
    for(int i = 0; i < 5; i++)
        if(timeouts[i] == vqeaf_state.screen_timeout_s) { timeout_idx = i; break; }
    lv_obj_t *timeout_value = vqeaf_label(timeout_row,
                                          timeout_names[timeout_idx],
                                          VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    lv_obj_set_flex_grow(timeout_value, 1);
    lv_obj_set_style_text_align(timeout_value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_event_cb(timeout_row, screen_timeout_cycle_cb, LV_EVENT_CLICKED, timeout_value);
    vqeaf_m3_ripple(timeout_row);

    lv_obj_t *timeout_note = vqeaf_label(root,
        "Hết giờ: mờ rồi tắt màn hình, vào chế độ ngủ để tiết kiệm pin. "
        "Chạm hoặc nhấn phím nguồn để thức.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(timeout_note, lv_pct(100));

    lv_obj_t *theme_row = vqeaf_card(root, lv_pct(100), 88);
    lv_obj_set_flex_flow(theme_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(theme_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(theme_row, 14, 0);
    lv_obj_add_flag(theme_row, LV_OBJ_FLAG_CLICKABLE);
    vqeaf_tile(theme_row, VQEAF_SYM_BRIGHTNESS_6, lv_color_hex(0x3A2A5A),
               VQEAF_C_ACCENT, 54, 17);
    vqeaf_label(theme_row, "Giao diện", VQEAF_FONT_BODY, VQEAF_C_TEXT);
    lv_obj_t *theme_value = vqeaf_label(theme_row,
        vqeaf_theme_get() == VQEAF_THEME_LIGHT ? "Sáng" : "Tối",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    lv_obj_set_flex_grow(theme_value, 1);
    lv_obj_set_style_text_align(theme_value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_event_cb(theme_row, theme_toggle_cb, LV_EVENT_CLICKED, theme_value);
    vqeaf_m3_ripple(theme_row);

    {
        lv_obj_t *trailing = NULL;
        lv_obj_t *glass_item = vqeaf_m3_list_item(root, VQEAF_SYM_WATER_DROP,
            "Liquid Glass", "Kính tím trong mờ, viền sáng và nền hoàng hôn",
            &trailing);
        vqeaf_m3_switch(trailing, vqeaf_theme_liquid_glass());
        lv_obj_add_event_cb(glass_item, liquid_glass_toggle_cb,
                            LV_EVENT_CLICKED, NULL);
    }

    /* ---- Màu động (Material You) ---- */
    {
        static const uint32_t seeds[] = {
            0x1EE0C8, 0x1E88E5, 0x7C4DFF, 0x43A047,
            0xFDD835, 0xFB8C00, 0xE53935, 0xD81B60,
        };
        char seed_text[48];
        uint32_t seed = vqeaf_theme_seed();
        lv_obj_t *dyn = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(dyn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(dyn, 18, 0);

        lv_obj_t *head = vqeaf_row(dyn);
        lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(head, 14, 0);
        vqeaf_tile(head, VQEAF_SYM_WATER_DROP, VQEAF_C_SURFACE_3,
                   VQEAF_C_ACCENT, 54, 17);
        vqeaf_label(head, "Màu động", VQEAF_FONT_BODY, VQEAF_C_TEXT);
        if(seed != 0) snprintf(seed_text, sizeof(seed_text), "#%06X", (unsigned)seed);
        else snprintf(seed_text, sizeof(seed_text), "Mặc định");
        lv_obj_t *value = vqeaf_label(head, seed_text, VQEAF_FONT_CAPTION,
                                      VQEAF_C_TEXT_DIM);
        lv_obj_set_flex_grow(value, 1);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);

        /* Material You automatic mode: an M3 list item whose trailing switch
         * reflects the real auto state; tapping the row toggles it and rebuilds
         * the page so every role picks up the wallpaper-derived seed. */
        {
            int on = vqeaf_theme_auto();
            char auto_support[64];
            snprintf(auto_support, sizeof(auto_support),
                     "Màu nguồn hình nền #%06X",
                     (unsigned)vqeaf_theme_wallpaper_seed());
            lv_obj_t *trailing = NULL;
            lv_obj_t *auto_item = vqeaf_m3_list_item(dyn, VQEAF_SYM_IMAGE,
                "Theo hình nền", auto_support, &trailing);
            vqeaf_m3_switch(trailing, on);
            lv_obj_add_event_cb(auto_item, auto_color_cb, LV_EVENT_CLICKED,
                                NULL);
        }

        lv_obj_t *swatches = vqeaf_row(dyn);
        lv_obj_set_size(swatches, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(swatches, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_column(swatches, 16, 0);
        lv_obj_set_style_pad_row(swatches, 16, 0);
        for(size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
            lv_obj_t *dot = vqeaf_obj(swatches);
            lv_obj_set_size(dot, 62, 62);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(seeds[i]), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            if(seed == seeds[i]) {
                lv_obj_set_style_border_width(dot, 4, 0);
                lv_obj_set_style_border_color(dot, VQEAF_C_TEXT, 0);
            }
            lv_obj_add_flag(dot, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(dot, dynamic_color_cb, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)seeds[i]);
        }

        if(seed != 0) {
            lv_obj_t *reset = vqeaf_label(dyn, "Về bảng màu mặc định",
                                          VQEAF_FONT_CAPTION,
                                          VQEAF_C_ACCENT);
            lv_obj_add_flag(reset, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(reset, dynamic_color_cb, LV_EVENT_CLICKED,
                                NULL);
        }

        lv_obj_t *dyn_note = vqeaf_label(dyn,
            "Một màu nguồn sinh ra cả bảng màu: nền và bề mặt pha theo màu đó "
            "ở độ bão hoà thấp, màu nhấn giữ đúng màu nguồn ở độ rực cao "
            "(xấp xỉ HSL của HCT, không phải bộ giải HCT đầy đủ). Bật \"Theo "
            "hình nền\" để tự lấy màu chủ đạo của hình nền làm màu nguồn. "
            "Chỉ trong phiên này.",
            VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
        lv_obj_set_width(dyn_note, lv_pct(100));
    }

    lv_obj_t *bold_row = vqeaf_card(root, lv_pct(100), 88);
    lv_obj_set_flex_flow(bold_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bold_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bold_row, 14, 0);
    lv_obj_add_flag(bold_row, LV_OBJ_FLAG_CLICKABLE);
    vqeaf_tile(bold_row, VQEAF_SYM_TEXT_FIELDS, lv_color_hex(0x2A1A3E),
               VQEAF_C_ACCENT, 54, 17);
    vqeaf_label(bold_row, "Chữ đậm", VQEAF_FONT_BODY, VQEAF_C_TEXT);
    lv_obj_t *bold_value = vqeaf_label(bold_row,
        vqeaf_type_get_bold() ? "Bật" : "Tắt",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    lv_obj_set_flex_grow(bold_value, 1);
    lv_obj_set_style_text_align(bold_value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_event_cb(bold_row, bold_toggle_cb, LV_EVENT_CLICKED, bold_value);
    vqeaf_m3_ripple(bold_row);

    lv_obj_t *bold_note = vqeaf_label(root,
        "Chữ đậm được lưu qua Settings Service. Violet khởi động từ initramfs "
        "chỉ đọc, chưa có phân vùng prefs ghi được, nên chế độ này chỉ trong "
        "phiên; sẽ tự bền vững khi có volume prefs.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(bold_note, lv_pct(100));

    /* ---- Material Design 3 ------------------------------------------------
     * The Material 3 design system: colour roles, shape scale, elevation,
     * state layers/ripple and the core components. Everything is drawn by the
     * shell's own LVGL widgets, so no assets are needed. */
    {
        lv_obj_t *m3 = vqeaf_m3_card(root, lv_pct(100), LV_SIZE_CONTENT,
                                     VQEAF_M3_CARD_FILLED);
        lv_obj_set_flex_flow(m3, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(m3, 18, 0);

        lv_obj_t *m3_head = vqeaf_row(m3);
        lv_obj_set_flex_align(m3_head, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(m3_head, 14, 0);
        vqeaf_tile(m3_head, VQEAF_SYM_PALETTE, VQEAF_C_PRIMARY_CONTAINER,
                   VQEAF_C_ON_PRIMARY_CONTAINER, 54, 17);
        vqeaf_label(m3_head, "Thiết kế QEAF", VQEAF_FONT_BODY,
                    VQEAF_C_TEXT);
        lv_obj_t *m3_tag = vqeaf_label(m3_head, "Bộ thành phần QEAF",
                                       VQEAF_FONT_CAPTION,
                                       VQEAF_C_TEXT_DIM);
        lv_obj_set_flex_grow(m3_tag, 1);
        lv_obj_set_style_text_align(m3_tag, LV_TEXT_ALIGN_RIGHT, 0);

        lv_obj_t *m3_buttons = vqeaf_row(m3);
        lv_obj_set_flex_align(m3_buttons, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(m3_buttons, 12, 0);
        /* Each style drives a real action: dialog, snackbar, colour reset. */
        lv_obj_t *b_dialog = vqeaf_m3_button(m3_buttons, "Hộp thoại",
                                             VQEAF_M3_FILLED);
        lv_obj_add_event_cb(b_dialog, m3_gallery_dialog_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *b_snack = vqeaf_m3_button(m3_buttons, "Snackbar",
                                            VQEAF_M3_TONAL);
        lv_obj_add_event_cb(b_snack, m3_gallery_snackbar_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *b_reset = vqeaf_m3_button(m3_buttons, "Đặt lại màu",
                                            VQEAF_M3_OUTLINED);
        lv_obj_add_event_cb(b_reset, m3_gallery_reset_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *m3_chips = vqeaf_row(m3);
        lv_obj_set_flex_align(m3_chips, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(m3_chips, 10, 0);
        vqeaf_m3_chip(m3_chips, "Tất cả", 1);
        vqeaf_m3_chip(m3_chips, "Ảnh", 0);
        vqeaf_m3_chip(m3_chips, "Video", 0);
        vqeaf_m3_chip(m3_chips, "Tệp", 0);

        /* The switch drives the real dark/light mode, so it is not a mock. */
        lv_obj_t *m3_switch_row = vqeaf_row(m3);
        lv_obj_set_size(m3_switch_row, lv_pct(100), 64);
        lv_obj_set_flex_align(m3_switch_row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(m3_switch_row, 14, 0);
        vqeaf_label(m3_switch_row, "Chế độ tối", VQEAF_FONT_BODY,
                    VQEAF_C_TEXT);
        lv_obj_t *m3_spacer = vqeaf_obj(m3_switch_row);
        lv_obj_set_flex_grow(m3_spacer, 1);
        vqeaf_m3_switch(m3_switch_row,
                        vqeaf_theme_get() == VQEAF_THEME_DARK);
        lv_obj_add_flag(m3_switch_row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(m3_switch_row, theme_toggle_cb,
                            LV_EVENT_CLICKED, NULL);

        lv_obj_t *m3_fab_row = vqeaf_row(m3);
        lv_obj_set_flex_align(m3_fab_row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(m3_fab_row, 14, 0);
        vqeaf_m3_fab(m3_fab_row, VQEAF_SYM_ADD);
        lv_obj_t *m3_fab_note = vqeaf_label(m3_fab_row,
            "FAB · elevation 3 · ripple", VQEAF_FONT_CAPTION,
            VQEAF_C_TEXT_MUTE);
        lv_obj_set_flex_grow(m3_fab_note, 1);

        /* Phase-2 components: a top app bar (the action icon opens the dialog)
         * and a navigation bar that drives the real shell navigation. */
        vqeaf_m3_top_app_bar(m3, "Thanh ứng dụng", NULL, VQEAF_SYM_MORE_VERT,
                             m3_gallery_dialog_cb, NULL);
        lv_obj_t *m3_nav = vqeaf_m3_navigation_bar(m3);
        lv_obj_t *nav_home = vqeaf_m3_nav_item(m3_nav, VQEAF_SYM_HOME,
                                               "Home", 0);
        lv_obj_add_event_cb(nav_home, m3_gallery_nav_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)VQEAF_PAGE_HOME);
        lv_obj_t *nav_set = vqeaf_m3_nav_item(m3_nav, VQEAF_SYM_SETTINGS,
                                              "Cài đặt", 1);
        lv_obj_add_event_cb(nav_set, m3_gallery_nav_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)VQEAF_PAGE_SETTINGS);
        lv_obj_t *nav_about = vqeaf_m3_nav_item(m3_nav, VQEAF_SYM_INFO,
                                                "Giới thiệu", 0);
        lv_obj_add_event_cb(nav_about, m3_gallery_nav_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)VQEAF_PAGE_ABOUT);

        lv_obj_t *m3_note = vqeaf_label(m3,
            "Vai trò màu, thang bo góc, elevation, state layer, ripple và thang "
            "chữ QEAF; thanh ứng dụng, thanh điều hướng, hộp thoại và "
            "Snackbar là component thật. Màu tự đổi theo bảng màu động ở trên.",
            VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
        lv_obj_set_width(m3_note, lv_pct(100));
    }

    lv_obj_t *note = vqeaf_label(root,
        "Mục ghi “Chưa có dịch vụ” không có backend trên thiết bị này.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(note, lv_pct(100));
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
}

/* ---- storage ------------------------------------------------------------- */

static void build_storage(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);
    detail_header(root, "Lưu trữ");

    lv_obj_t *disk = vqeaf_card(root, lv_pct(100), 226);
    lv_obj_set_flex_flow(disk, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(disk, 14, 0);

    lv_obj_t *disk_head = vqeaf_row(disk);
    vqeaf_label(disk_head, "Bộ nhớ trong", VQEAF_FONT_BODY, VQEAF_C_TEXT);
    live_storage_pct = vqeaf_label(disk_head, "--", VQEAF_FONT_BODY,
                                   VQEAF_C_ACCENT);

    live_storage_bar = lv_bar_create(disk);
    style_progress(live_storage_bar);

    lv_obj_t *usage = vqeaf_row(disk);
    vqeaf_label(usage, "Đã dùng / tổng", VQEAF_FONT_CAPTION,
                VQEAF_C_TEXT_DIM);
    live_storage_pair = vqeaf_label(usage, "--", VQEAF_FONT_CAPTION,
                                    VQEAF_C_TEXT_DIM);

    lv_obj_t *disk_note = vqeaf_label(disk,
        "Phân vùng gốc “/” đọc từ statvfs; không ước lượng.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(disk_note, lv_pct(100));

    lv_obj_t *ram = vqeaf_card(root, lv_pct(100), 150);
    lv_obj_set_flex_flow(ram, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ram, 14, 0);

    lv_obj_t *ram_head = vqeaf_row(ram);
    vqeaf_label(ram_head, "Bộ nhớ RAM", VQEAF_FONT_BODY, VQEAF_C_TEXT);
    live_mem_pair = vqeaf_label(ram_head, "--", VQEAF_FONT_BODY,
                                VQEAF_C_ACCENT);
    lv_obj_set_width(live_mem_pair, lv_pct(55));
    lv_obj_set_style_text_align(live_mem_pair, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *ram_note = vqeaf_label(ram,
        "Đã dùng / tổng cộng, theo /proc/meminfo.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(ram_note, lv_pct(100));
}

/* ---- about --------------------------------------------------------------- */

static void build_about(lv_obj_t *content)
{
    char text[96];
    lv_display_t *display = lv_obj_get_display(content);
    lv_obj_t *root = page_root(content);

    detail_header(root, "Giới thiệu");

    lv_obj_t *identity = vqeaf_card(root, lv_pct(100), 196);
    lv_obj_set_flex_flow(identity, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(identity, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(identity, 22, 0);
    vqeaf_tile(identity, VQEAF_SYM_BOLT, lv_color_hex(0x0F3B36),
               VQEAF_C_ACCENT, 96, 30);

    lv_obj_t *name = vqeaf_column(identity);
    lv_obj_set_width(name, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(name, 2, 0);
    snprintf(text, sizeof(text), "%s · violet", QEAF_OS_VERSION);
    vqeaf_label(name, text, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    vqeaf_label(name, "Qeafivels Software", VQEAF_FONT_CAPTION,
                VQEAF_C_TEXT_MUTE);

    lv_obj_t *info = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(info, 18, 0);

    snprintf(text, sizeof(text), "%d × %d",
             (int)lv_display_get_horizontal_resolution(display),
             (int)lv_display_get_vertical_resolution(display));
    info_row(info, "Độ phân giải", text);
    info_row(info, "Nền tảng", "violet · PID 1");
    info_row(info, "Giao thức khởi động", "VQEAF v3");
    info_row(info, "Bản dựng", __DATE__);
    live_uptime = info_row(info, "Thời gian hoạt động", "--");
    live_mem_pair = info_row(info, "Bộ nhớ RAM", "--");
    live_storage_pct = info_row(info, "Lưu trữ", "--");
    live_brightness = info_row(info, "Độ sáng màn hình", "--");
    live_battery = info_row(info, "Pin", "--");
    live_wifi = info_row(info, "Wi-Fi", "--");

    lv_obj_t *note = vqeaf_label(root,
        "Mọi giá trị trên đọc từ thiết bị; không có mục nào được giả lập.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(note, lv_pct(100));
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
}

/* ---- keyboard ------------------------------------------------------------ */

/* Keyboard settings mirror the two reference screens: the "Keyboard" hub with
 * a hero tile and two setting groups, then "Keyboard themes" with a breadcrumb
 * and a grid of built-in theme cards. The mini keyboards are drawn from plain
 * rounded rectangles, so no bitmap assets are needed. */

typedef struct {
    const char *name;
    const char *sub;
    uint32_t bg;
    uint32_t key;
    uint32_t accent;
    uint8_t dot_count;
    uint32_t dots[5];
    int round_keys;
} kb_theme_t;

static const kb_theme_t kb_themes[] = {
    { "Nightfall", "Ocean",     0x0C1A22, 0x1B3B4A, 0x2F9BD8, 5,
      { 0x4C8DFF, 0x2FBF71, 0xE8A33D, 0xE86A8A, 0x9B6BFF }, 0 },
    { "Pastel",    "Snow",      0xE8F0F8, 0xFFFFFF, 0x3B82D6, 5,
      { 0x3B82D6, 0x2FBF71, 0xE86A8A, 0xC9A227, 0x38BDF8 }, 0 },
    { "Gradients", "Deep sea",  0x0A2540, 0x123C63, 0x1E88E5, 4,
      { 0x1E88E5, 0xE85D3D, 0x2FBF71, 0x8B5CF6, 0 }, 0 },
    { "Bubble",    "Mint",      0x0B211A, 0x123A2C, 0x17C3A2, 4,
      { 0x17C3A2, 0x2FBF71, 0x38BDF8, 0xA78BFA, 0 }, 1 },
    { "Facet",     "Graphite",  0x1A1D1F, 0x2C3134, 0x9AA0A6, 4,
      { 0x9AA0A6, 0x6B7280, 0x38BDF8, 0xE8A33D, 0 }, 1 },
    { "Frost",     "Frost",     0x1D3F8F, 0x3F6FD1, 0x7FB2FF, 4,
      { 0x3B82F6, 0x1E88E5, 0x38BDF8, 0xA78BFA, 0 }, 1 },
};

/* g_kb_theme is the previewed card; g_kb_theme_applied is what every real
 * LVGL keyboard in the OS uses. Both are session-only: the initramfs root has
 * no persistent prefs partition, so a reboot restores Nightfall. */
static int g_kb_theme;
static int g_kb_theme_applied;
static lv_obj_t *g_applied_label;
#define VQEAF_LIVE_KEYBOARDS 4
static lv_obj_t *g_live_keyboards[VQEAF_LIVE_KEYBOARDS];

int vqeaf_pages_kb_theme_count(void)
{
    return (int)(sizeof(kb_themes) / sizeof(kb_themes[0]));
}

int vqeaf_pages_kb_theme_applied(void)
{
    return g_kb_theme_applied;
}

uint32_t vqeaf_pages_kb_theme_key_color(int index)
{
    if(index < 0 || index >= vqeaf_pages_kb_theme_count()) return 0;
    return kb_themes[index].key;
}

/* Light keys need dark glyphs (Pastel), dark keys need light glyphs. */
static lv_color_t kb_text_color(uint32_t rgb)
{
    uint32_t r = (rgb >> 16) & 0xFFu;
    uint32_t g = (rgb >> 8) & 0xFFu;
    uint32_t b = rgb & 0xFFu;
    uint32_t lum = (r * 299u + g * 587u + b * 114u) / 1000u;
    return lum > 128u ? lv_color_hex(0x101418) : lv_color_hex(0xEAF7F5);
}

static void kb_apply_style(lv_obj_t *kb, const kb_theme_t *theme)
{
    lv_obj_set_style_bg_color(kb, lv_color_hex(theme->bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(kb, kb_text_color(theme->bg), LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(kb, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(kb, 6, LV_PART_MAIN);

    lv_obj_set_style_bg_color(kb, lv_color_hex(theme->key), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, kb_text_color(theme->key), LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, VQEAF_FONT_BODY, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, theme->round_keys ? 16 : 6, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, theme->round_keys ? 1 : 0, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, lv_color_hex(theme->accent),
                                  LV_PART_ITEMS);
    /* Accent is a real pressed-state colour, not a fake "special key" claim. */
    lv_obj_set_style_bg_color(kb, lv_color_hex(theme->accent),
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER,
                            LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, kb_text_color(theme->accent),
                                LV_PART_ITEMS | LV_STATE_PRESSED);
}

static void kb_unregister_cb(lv_event_t *event)
{
    lv_obj_t *kb = lv_event_get_target(event);
    int i, j;
    for(i = 0; i < VQEAF_LIVE_KEYBOARDS; i++) {
        if(g_live_keyboards[i] != kb) continue;
        for(j = i; j + 1 < VQEAF_LIVE_KEYBOARDS; j++)
            g_live_keyboards[j] = g_live_keyboards[j + 1];
        g_live_keyboards[VQEAF_LIVE_KEYBOARDS - 1] = NULL;
        break;
    }
}

void vqeaf_pages_kb_theme_style(lv_obj_t *keyboard)
{
    int i;
    if(keyboard == NULL) return;
    kb_apply_style(keyboard, &kb_themes[g_kb_theme_applied]);
    for(i = 0; i < VQEAF_LIVE_KEYBOARDS; i++) {
        if(g_live_keyboards[i] == keyboard) return;
        if(g_live_keyboards[i] == NULL) {
            g_live_keyboards[i] = keyboard;
            lv_obj_add_event_cb(keyboard, kb_unregister_cb, LV_EVENT_DELETE,
                                NULL);
            return;
        }
    }
}

static void kb_restyle_live(void)
{
    int i;
    for(i = 0; i < VQEAF_LIVE_KEYBOARDS; i++) {
        if(g_live_keyboards[i] == NULL) continue;
        kb_apply_style(g_live_keyboards[i], &kb_themes[g_kb_theme_applied]);
        lv_obj_invalidate(g_live_keyboards[i]);
    }
    /* The independent IME window is not an lv_keyboard, so restyle it too. */
    vqeaf_ime_restyle();
}

static void kb_theme_pick_cb(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    if(index < 0 || index >= (int)(sizeof(kb_themes) / sizeof(kb_themes[0])))
        return;
    g_kb_theme = index;
    vqeaf_ui_show(vqeaf_ui_page());
}

static void kb_theme_apply_cb(lv_event_t *event)
{
    (void)event;
    g_kb_theme_applied = g_kb_theme;
    /* Real change: restyle every live LVGL keyboard, not just the preview. */
    kb_restyle_live();
    if(g_applied_label != NULL)
        lv_label_set_text_fmt(g_applied_label,
                              "Đang dùng: %s · Có hiệu lực trong phiên này",
                              kb_themes[g_kb_theme_applied].name);
    vio_log("keyboard: applied theme %s",
            kb_themes[g_kb_theme_applied].name);
    vqeaf_ui_activity();
}

static void kb_key_row(lv_obj_t *parent, const kb_theme_t *theme,
                       int count, int inset)
{
    lv_obj_t *row = vqeaf_obj(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_hor(row, inset, 0);
    lv_obj_set_style_pad_column(row, 4, 0);

    for(int i = 0; i < count; i++) {
        lv_obj_t *key = vqeaf_obj(row);
        lv_obj_set_height(key, 16);
        lv_obj_set_flex_grow(key, 1);
        lv_obj_set_style_radius(key, theme->round_keys ? 8 : 4, 0);
        lv_obj_set_style_bg_color(key, lv_color_hex(theme->key), 0);
        lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
    }
}

static void kb_bottom_row(lv_obj_t *parent, const kb_theme_t *theme)
{
    static const int grow[] = { 3, 8, 3 };
    lv_obj_t *row = vqeaf_obj(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 4, 0);

    for(int i = 0; i < 3; i++) {
        lv_obj_t *key = vqeaf_obj(row);
        lv_obj_set_height(key, 16);
        lv_obj_set_flex_grow(key, grow[i]);
        lv_obj_set_style_radius(key, theme->round_keys ? 8 : 4, 0);
        lv_obj_set_style_bg_color(key, lv_color_hex(i == 2 ? theme->accent :
                                                          theme->key), 0);
        lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
    }
}

static void kb_preview(lv_obj_t *card, const kb_theme_t *theme)
{
    lv_obj_t *panel = vqeaf_obj(card);
    lv_obj_set_width(panel, lv_pct(100));
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(panel, lv_color_hex(theme->bg), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 18, 0);
    lv_obj_set_style_pad_all(panel, 10, 0);
    lv_obj_set_style_pad_row(panel, 4, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);

    kb_key_row(panel, theme, 10, 0);
    kb_key_row(panel, theme, 9, 8);
    kb_key_row(panel, theme, 7, 20);
    kb_bottom_row(panel, theme);
}

static void kb_dots(lv_obj_t *card, const kb_theme_t *theme)
{
    if(theme->dot_count == 0) return;

    lv_obj_t *row = vqeaf_obj(card);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_style_pad_top(row, 6, 0);
    lv_obj_set_style_pad_bottom(row, 2, 0);

    for(int i = 0; i < theme->dot_count; i++) {
        lv_obj_t *dot = vqeaf_obj(row);
        lv_obj_set_size(dot, 34, 34);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(theme->dots[i]), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 2, 0);
        lv_obj_set_style_border_color(dot,
            i == 0 ? VQEAF_C_TEXT : VQEAF_C_LINE, 0);
    }
}

static void kb_card_actions(lv_obj_t *card)
{
    lv_obj_t *row = vqeaf_obj(card);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 30, 0);
    lv_obj_set_style_pad_top(row, 2, 0);
    vqeaf_glyph(row, VQEAF_SYM_EDIT, VQEAF_ICON_SM, VQEAF_C_TEXT_DIM);
    vqeaf_glyph(row, VQEAF_SYM_UPLOAD, VQEAF_ICON_SM, VQEAF_C_TEXT_DIM);
    vqeaf_glyph(row, VQEAF_SYM_PUSH_PIN, VQEAF_ICON_SM, VQEAF_C_TEXT_DIM);
}

static lv_obj_t *kb_theme_card(lv_obj_t *parent, int index)
{
    const kb_theme_t *theme = &kb_themes[index];
    int selected = index == g_kb_theme;
    lv_obj_t *card = vqeaf_obj(parent);

    lv_obj_set_width(card, lv_pct(lv_display_get_horizontal_resolution(
                                     lv_obj_get_display(parent)) >= 1200 ?
                                 31 : 48));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, VQEAF_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, selected ? 2 : 1, 0);
    lv_obj_set_style_border_color(card,
        selected ? VQEAF_C_ACCENT : VQEAF_C_LINE, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    kb_preview(card, theme);
    kb_dots(card, theme);

    lv_obj_t *name_row = vqeaf_row(card);
    lv_obj_set_style_pad_top(name_row, 2, 0);
    vqeaf_label(name_row, theme->name, VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    if(selected)
        vqeaf_glyph(name_row, VQEAF_SYM_CHECK_CIRCLE, VQEAF_ICON_SM,
                    VQEAF_C_ACCENT);
    if(theme->sub != NULL)
        vqeaf_label(card, theme->sub, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    kb_card_actions(card);

    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, kb_theme_pick_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)index);
    return card;
}

static lv_obj_t *crumb_pill(lv_obj_t *parent, const char *symbol,
                            const char *text, int active,
                            vqeaf_page_t page)
{
    lv_obj_t *pill = vqeaf_obj(parent);
    lv_obj_set_height(pill, 58);
    lv_obj_set_style_bg_color(pill, VQEAF_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pill, 29, 0);
    lv_obj_set_style_border_width(pill, active ? 2 : 0, 0);
    lv_obj_set_style_border_color(pill, VQEAF_C_ACCENT, 0);
    /* Keep the whole breadcrumb inside the 720 px canvas: three pills plus
     * two chevrons must not push the active pill off the right edge. */
    lv_obj_set_style_pad_hor(pill, 10, 0);
    lv_obj_set_style_pad_column(pill, 4, 0);
    lv_obj_set_width(pill, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    if(symbol != NULL)
        vqeaf_glyph(pill, symbol, VQEAF_ICON_SM,
                    active ? VQEAF_C_ACCENT : VQEAF_C_TEXT_DIM);
    vqeaf_label(pill, text, VQEAF_FONT_CAPTION,
                active ? VQEAF_C_ACCENT : VQEAF_C_TEXT_DIM);
    if(page != VQEAF_PAGE_COUNT)
        vqeaf_on_click(pill, nav_cb, (void *)(intptr_t)page);
    return pill;
}

static void build_keyboard(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);

    lv_obj_t *hero = vqeaf_obj(root);
    lv_obj_set_width(hero, lv_pct(100));
    lv_obj_set_height(hero, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(hero, 12, 0);
    lv_obj_set_style_pad_row(hero, 10, 0);

    lv_obj_t *plate = vqeaf_obj(hero);
    lv_obj_set_size(plate, 132, 132);
    lv_obj_set_style_radius(plate, 38, 0);
    lv_obj_set_style_bg_color(plate, lv_color_hex(0x2A2E6E), 0);
    lv_obj_set_style_bg_opa(plate, LV_OPA_COVER, 0);
    lv_obj_t *glyph = vqeaf_glyph(plate, VQEAF_SYM_KEYBOARD, VQEAF_ICON,
                                  lv_color_hex(0xB6BCFF));
    lv_obj_center(glyph);

    vqeaf_label(hero, "Bàn phím", VQEAF_FONT_HEADLINE, VQEAF_C_TEXT);

    lv_obj_t *active = vqeaf_obj(hero);
    lv_obj_set_size(active, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(active, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(active, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(active, 10, 0);
    vqeaf_glyph(active, VQEAF_SYM_CHECK_CIRCLE, VQEAF_ICON_SM,
                VQEAF_C_GREEN);
    vqeaf_label(active, "Bố cục mặc định · English", VQEAF_FONT_BODY,
                VQEAF_C_TEXT_DIM);

    vqeaf_label(root, "Nhập liệu", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    static const setting_def_t typing_rows[] = {
        { VQEAF_SYM_LANGUAGE,   "Ngôn ngữ & bố cục", "English",
          0x1E4A2A, VQEAF_PAGE_COUNT },
        { VQEAF_SYM_TEXT_FIELDS,"Nhập liệu", "Tự sửa, gợi ý, cử chỉ",
          0x1A3A5A, VQEAF_PAGE_COUNT },
        { VQEAF_SYM_TOUCH_APP,  "Nhấn phím", "Rung, âm thanh, bong bóng phím",
          0x3A2A5A, VQEAF_PAGE_KEYBOARD_FEEDBACK },
    };
    for(size_t i = 0; i < sizeof(typing_rows) / sizeof(typing_rows[0]); i++)
        build_setting_row(root, &typing_rows[i]);

    vqeaf_label(root, "Bàn phím", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    static const setting_def_t look_rows[] = {
        { VQEAF_SYM_PALETTE,     "Giao diện", "Chủ đề và kiểu phím",
          0x4A1A2A, VQEAF_PAGE_KEYBOARD_THEMES },
        { VQEAF_SYM_ASPECT_RATIO,"Bố cục & kích thước",
          "Cỡ phím, hàng phím, một tay", 0x1A2A4A, VQEAF_PAGE_COUNT },
        { VQEAF_SYM_VIEW_STREAM, "Hàng & thanh",
          "Hàng ký hiệu, thanh, thứ tự hàng", 0x0F3B36, VQEAF_PAGE_COUNT },
    };
    for(size_t i = 0; i < sizeof(look_rows) / sizeof(look_rows[0]); i++)
        build_setting_row(root, &look_rows[i]);

    lv_obj_t *note = vqeaf_label(root,
        "Ngôn ngữ và một số bố cục chưa có trang cấu hình.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(note, lv_pct(100));
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
}

static void keyboard_feedback_toggle_cb(lv_event_t *event)
{
    const char *key = (const char *)lv_event_get_user_data(event);
    if(key == NULL) return;
    vqeaf_settings_set_bool(key, !vqeaf_settings_get_bool(key, 0));
    vqeaf_ui_show(VQEAF_PAGE_KEYBOARD_FEEDBACK);
}

static void keyboard_feedback_row(lv_obj_t *root, const char *symbol,
                                  const char *title, const char *description,
                                  const char *setting_key)
{
    lv_obj_t *trailing = NULL;
    lv_obj_t *row = vqeaf_m3_list_item(root, symbol, title, description,
                                       &trailing);
    (void)vqeaf_m3_switch(trailing,
                          vqeaf_settings_get_bool(setting_key, 0));
    lv_obj_add_event_cb(row, keyboard_feedback_toggle_cb, LV_EVENT_CLICKED,
                        (void *)setting_key);
}

static void build_keyboard_feedback(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);
    lv_obj_t *header = vqeaf_row(root);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 16, 0);
    lv_obj_t *back = vqeaf_tile(header, VQEAF_SYM_ARROW_BACK,
                                VQEAF_C_SURFACE, VQEAF_C_TEXT, 64, 22);
    vqeaf_on_click(back, nav_cb, (void *)(intptr_t)VQEAF_PAGE_KEYBOARD);
    vqeaf_label(header, "Phản hồi khi nhấn", VQEAF_FONT_HEADLINE,
                VQEAF_C_TEXT);

    lv_obj_t *intro = vqeaf_label(root,
        "Chọn phản hồi cho bàn phím hệ thống. Các thay đổi áp dụng ngay.",
        VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
    lv_obj_set_width(intro, lv_pct(100));

    keyboard_feedback_row(root, VQEAF_SYM_TOUCH_APP, "Rung phím",
        vqeaf_ime_haptics_available() ?
            "Driver vibrator có sẵn · nhấn nhẹ mỗi phím" :
            "Đã lưu lựa chọn · kernel chưa có driver vibrator",
        "keyboard.feedback.vibration");
    keyboard_feedback_row(root, VQEAF_SYM_VOLUME_UP, "Âm thanh phím",
        vqeaf_ime_sound_available() ?
            "Âm thanh nhấn phím" :
            "Đã lưu lựa chọn · hệ thống chưa có backend phát âm thanh",
        "keyboard.feedback.sound");

    lv_obj_t *note = vqeaf_label(root,
        vqeaf_settings_persistent() ?
            "Lựa chọn được lưu trên thiết bị." :
            "Lựa chọn chỉ tồn tại trong phiên; Violet chưa có phân vùng cấu hình ghi được.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(note, lv_pct(100));
}

static void build_keyboard_themes(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);
    /* The apply button is the last row: leave tail padding so the scope note
     * below it can still be scrolled fully into view. */
    lv_obj_set_style_pad_bottom(root, 120, 0);

    lv_obj_t *header = vqeaf_row(root);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 18, 0);
    lv_obj_set_style_pad_top(header, 4, 0);
    lv_obj_t *back = vqeaf_tile(header, VQEAF_SYM_ARROW_BACK,
                                VQEAF_C_SURFACE, VQEAF_C_TEXT, 72, 24);
    vqeaf_on_click(back, nav_cb, (void *)(intptr_t)VQEAF_PAGE_KEYBOARD);
    vqeaf_label(header, "Chủ đề bàn phím", VQEAF_FONT_HEADLINE,
                VQEAF_C_TEXT);
    lv_obj_t *spacer = vqeaf_obj(header);
    lv_obj_set_flex_grow(spacer, 1);
    vqeaf_glyph(header, VQEAF_SYM_STAR, VQEAF_ICON_SM, VQEAF_C_TEXT_MUTE);

    lv_obj_t *crumbs = vqeaf_obj(root);
    lv_obj_set_width(crumbs, lv_pct(100));
    lv_obj_set_height(crumbs, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(crumbs, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(crumbs, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(crumbs, 6, 0);
    lv_obj_set_style_pad_row(crumbs, 8, 0);
    crumb_pill(crumbs, NULL, "Trang chủ", 0, VQEAF_PAGE_HOME);
    vqeaf_glyph(crumbs, VQEAF_SYM_CHEVRON_RIGHT, VQEAF_ICON_SM,
                VQEAF_C_TEXT_MUTE);
    crumb_pill(crumbs, VQEAF_SYM_PALETTE, "Giao diện", 0,
               VQEAF_PAGE_KEYBOARD);
    vqeaf_glyph(crumbs, VQEAF_SYM_CHEVRON_RIGHT, VQEAF_ICON_SM,
                VQEAF_C_TEXT_MUTE);
    crumb_pill(crumbs, VQEAF_SYM_PALETTE, "Chủ đề", 1,
               VQEAF_PAGE_COUNT);

    lv_obj_t *section = vqeaf_row(root);
    vqeaf_label(section, "Có sẵn", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    vqeaf_glyph(section, VQEAF_SYM_INFO, VQEAF_ICON_SM, VQEAF_C_TEXT_MUTE);

    lv_obj_t *note = vqeaf_label(root,
        "Chạm card để xem trước; bấm Áp dụng để đổi bàn phím nhập liệu của "
        "hệ thống (tìm kiếm, ghi chú, mật khẩu Wi-Fi).",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(note, lv_pct(100));

    lv_obj_t *grid = vqeaf_obj(root);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 16, 0);
    lv_obj_set_style_pad_row(grid, 18, 0);
    for(size_t i = 0; i < sizeof(kb_themes) / sizeof(kb_themes[0]); i++)
        kb_theme_card(grid, (int)i);

    /* Applied state and the commit button. The card border means "preview";
     * this line says which theme the OS keyboards actually use. */
    g_applied_label = vqeaf_label(root, "", VQEAF_FONT_CAPTION,
                                  VQEAF_C_TEXT_DIM);
    lv_label_set_text_fmt(g_applied_label,
                          "Đang dùng: %s · Có hiệu lực trong phiên này",
                          kb_themes[g_kb_theme_applied].name);
    lv_obj_set_width(g_applied_label, lv_pct(100));

    lv_obj_t *apply = vqeaf_m3_button(root, "Áp dụng", VQEAF_M3_FILLED);
    lv_obj_set_width(apply, lv_pct(100));
    lv_obj_add_event_cb(apply, kb_theme_apply_cb, LV_EVENT_CLICKED, NULL);
}


/* ---- wi-fi --------------------------------------------------------------- */

static const char *bool_text(int value)
{
    return value ? "Có" : "Không";
}

static const char *wifi_state_text(void)
{
    if(!vqeaf_state.wifi_if_present)
        return vqeaf_state.wifi_driver_loaded ?
               "Không có giao diện mạng" : "Không có thiết bị";
    return vqeaf_state.wifi_up > 0 ? "Đang kết nối" : "Đang tắt";
}

static int g_wifi_count;
static const char *wifi_gateway_mode(void)
{
    const char *mode = vqeaf_settings_get_str("wifi.gateway", "dhcp");
    return (mode != NULL && strcmp(mode, "static") == 0) ? "static" : "dhcp";
}

static void wifi_gateway_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_settings_set_str("wifi.gateway",
        strcmp(wifi_gateway_mode(), "dhcp") == 0 ? "static" : "dhcp");
    vqeaf_ui_show(VQEAF_PAGE_WIFI);
}

static vqeaf_wifi_net_t g_wifi_nets[VQEAF_WIFI_MAX];
static char g_wifi_scan_note[96];

static lv_obj_t *g_wifi_modal;
static lv_obj_t *g_wifi_modal_status;
static lv_obj_t *g_wifi_password;
static vqeaf_wifi_net_t g_wifi_pick;

static void wifi_scan_now(void)
{
    int count = vqeaf_wifi_scan(g_wifi_nets, VQEAF_WIFI_MAX);

    if(count < 0) {
        g_wifi_count = 0;
        snprintf(g_wifi_scan_note, sizeof(g_wifi_scan_note), "%s",
                 vqeaf_wifi_error(count));
    }
    else {
        g_wifi_count = count;
        g_wifi_scan_note[0] = '\0';
    }
}

static void wifi_modal_close(void)
{
    vqeaf_ime_hide();
    if(g_wifi_modal != NULL) {
        lv_obj_delete(g_wifi_modal);
        g_wifi_modal = NULL;
        g_wifi_modal_status = NULL;
        g_wifi_password = NULL;
    }
}

static void wifi_modal_close_cb(lv_event_t *event)
{
    (void)event;
    wifi_modal_close();
}

static void wifi_modal_connect_cb(lv_event_t *event)
{
    const char *password;
    int rc;

    (void)event;
    if(g_wifi_modal_status == NULL) return;
    password = g_wifi_password != NULL ? lv_textarea_get_text(g_wifi_password)
                                       : "";
    if(g_wifi_pick.secure && (password == NULL || password[0] == '\0')) {
        lv_label_set_text(g_wifi_modal_status, "Nhập mật khẩu Wi-Fi.");
        return;
    }
    rc = vqeaf_wifi_connect(g_wifi_pick.ssid, password);
    if(rc == 0) {
        wifi_modal_close();
        vqeaf_ui_show(VQEAF_PAGE_WIFI);
        return;
    }
    lv_label_set_text(g_wifi_modal_status, vqeaf_wifi_error(rc));
}

/* Modal password sheet: dim layer, card, textarea and LVGL keyboard. The key
 * never leaves this session; the connect call reports the missing supplicant
 * instead of pretending the join worked. */
static void wifi_open_modal(const vqeaf_wifi_net_t *net)
{
    lv_display_t *display = lv_display_get_default();
    int width = (int)lv_display_get_horizontal_resolution(display);
    int height = (int)lv_display_get_vertical_resolution(display);
    int card_w = width * 84 / 100;
    int keyboard_h = height * 38 / 100;

    if(g_wifi_modal != NULL || net == NULL) return;
    g_wifi_pick = *net;

    g_wifi_modal = vqeaf_obj(lv_layer_top());
    lv_obj_set_size(g_wifi_modal, width, height);
    lv_obj_set_style_bg_color(g_wifi_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_wifi_modal, LV_OPA_70, 0);
    lv_obj_add_flag(g_wifi_modal, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *card = vqeaf_obj(g_wifi_modal);
    lv_obj_set_size(card, card_w, keyboard_h > 0 ? 330 : 300);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_set_style_bg_color(card, VQEAF_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 32, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, VQEAF_C_LINE, 0);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 12, 0);

    lv_obj_t *title = vqeaf_label(card, net->ssid, VQEAF_FONT_TITLE,
                                  VQEAF_C_TEXT);
    lv_obj_set_width(title, lv_pct(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);

            char subtext[96];
        snprintf(subtext, sizeof(subtext), "%s · Gateway %s",
                 vqeaf_wifi_enc_name(net->enc),
                 strcmp(wifi_gateway_mode(), "static") == 0 ?
                 "static" : "DHCP");
lv_obj_t *sub = vqeaf_label(card, subtext,
                                VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    lv_obj_set_width(sub, lv_pct(100));

    g_wifi_password = lv_textarea_create(card);
    lv_obj_set_size(g_wifi_password, lv_pct(100), 76);
    lv_textarea_set_one_line(g_wifi_password, true);
    lv_textarea_set_password_mode(g_wifi_password, true);
    lv_textarea_set_placeholder_text(g_wifi_password, "Mật khẩu");
    lv_obj_set_style_bg_color(g_wifi_password, VQEAF_C_SURFACE_3, 0);
    lv_obj_set_style_bg_opa(g_wifi_password, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_wifi_password, VQEAF_C_LINE, 0);
    lv_obj_set_style_border_width(g_wifi_password, 1, 0);
    lv_obj_set_style_radius(g_wifi_password, 18, 0);
    lv_obj_set_style_text_color(g_wifi_password, VQEAF_C_TEXT, 0);
    lv_obj_set_style_text_font(g_wifi_password, VQEAF_FONT_BODY, 0);
    lv_obj_set_style_pad_left(g_wifi_password, 18, 0);
    lv_obj_set_style_pad_right(g_wifi_password, 18, 0);

    g_wifi_modal_status = vqeaf_label(card, "", VQEAF_FONT_CAPTION,
                                      VQEAF_C_WARN);
    lv_obj_set_width(g_wifi_modal_status, lv_pct(100));

    lv_obj_t *buttons = vqeaf_obj(card);
    lv_obj_set_size(buttons, lv_pct(100), 72);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(buttons, 14, 0);
    lv_obj_t *cancel = vqeaf_obj(buttons);
    lv_obj_set_size(cancel, 150, 64);
    lv_obj_set_style_bg_color(cancel, VQEAF_C_SURFACE_3, 0);
    lv_obj_set_style_bg_opa(cancel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cancel, 32, 0);
    lv_obj_set_flex_flow(cancel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cancel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    vqeaf_label(cancel, "Hủy", VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
    vqeaf_on_click(cancel, wifi_modal_close_cb, NULL);

    lv_obj_t *connect = vqeaf_obj(buttons);
    lv_obj_set_size(connect, 190, 64);
    lv_obj_set_style_bg_color(connect, VQEAF_C_ACCENT, 0);
    lv_obj_set_style_bg_opa(connect, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(connect, 32, 0);
    lv_obj_set_flex_flow(connect, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(connect, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    vqeaf_label(connect, "Kết nối", VQEAF_FONT_BODY, VQEAF_C_BG_DEEP);
    vqeaf_on_click(connect, wifi_modal_connect_cb, NULL);

    /* Independent IME window overlays the password sheet (adjustNothing); the
     * applied keyboard theme is honoured by the IME itself. */
    lv_obj_add_event_cb(g_wifi_password, ime_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(g_wifi_password, ime_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
    vqeaf_ime_show(g_wifi_password, NULL);
}

static void wifi_network_cb(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);

    if(index < 0 || index >= g_wifi_count) return;
    if(g_wifi_nets[index].secure) {
        wifi_open_modal(&g_wifi_nets[index]);
    }
    else {
        int rc = vqeaf_wifi_connect(g_wifi_nets[index].ssid, "");
        if(rc != 0) {
            snprintf(g_wifi_scan_note, sizeof(g_wifi_scan_note), "%s",
                     vqeaf_wifi_error(rc));
            vqeaf_ui_show(VQEAF_PAGE_WIFI);
        }
        else vqeaf_ui_show(VQEAF_PAGE_WIFI);
    }
}

static void wifi_rescan_cb(lv_event_t *event)
{
    (void)event;
    wifi_modal_close();
    vqeaf_ui_show(VQEAF_PAGE_WIFI);
}

static void build_wifi(lv_obj_t *content)
{
    char text[96];
    lv_obj_t *root = page_root(content);

    detail_header(root, "Wi-Fi");
    wifi_scan_now();

    lv_obj_t *hero = vqeaf_card(root, lv_pct(100), 186);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hero, 20, 0);
    vqeaf_tile(hero, VQEAF_SYM_WIFI, lv_color_hex(0x1A5A6A),
               vqeaf_state.wifi_if_present ? VQEAF_C_ACCENT :
                                             VQEAF_C_TEXT_MUTE, 92, 28);

    lv_obj_t *head = vqeaf_column(hero);
    lv_obj_set_width(head, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(head, 2, 0);
    vqeaf_label(head, "Wi-Fi", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    vqeaf_label(head, wifi_state_text(), VQEAF_FONT_BODY,
                vqeaf_state.wifi_if_present ? VQEAF_C_TEXT_DIM :
                                              VQEAF_C_TEXT_MUTE);
    vqeaf_label(head,
        vqeaf_state.wifi_if_present ? "Giao diện mạng đã được tạo" :
        "wlan/cfg80211 có nhưng chưa có netdev",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);

    vqeaf_label(root, "Kết nối", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    lv_obj_t *link = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(link, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(link, 18, 0);
    info_row(link, "Giao diện",
             vqeaf_state.wifi_if_present ? vqeaf_state.wifi_ifname :
                                           "Không có");
    info_row(link, "Trạng thái liên kết",
             vqeaf_state.wifi_up > 0 ? "up" :
             vqeaf_state.wifi_up == 0 ? "down" : "không có");
    info_row(link, "Địa chỉ MAC",
             vqeaf_state.wifi_mac[0] != '\0' ? vqeaf_state.wifi_mac :
                                              "Không có");
    snprintf(text, sizeof(text), "%d%%", vqeaf_state.wifi_link);
    info_row(link, "Chất lượng liên kết",
             vqeaf_state.wifi_if_present ? text : "--");
    info_row(link, "SSID", "Chưa có supplicant");
    {
        const char *mode = wifi_gateway_mode();
        lv_obj_t *gw = info_row(link, "Chế độ Gateway",
            strcmp(mode, "static") == 0 ? "Tĩnh (static)" :
                                         "DHCP (chờ udhcpc)");
        lv_obj_add_flag(gw, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(gw, wifi_gateway_cb, LV_EVENT_CLICKED, NULL);
    }

    vqeaf_label(root, "Trình điều khiển & firmware", VQEAF_FONT_TITLE,
                VQEAF_C_TEXT);
    lv_obj_t *driver = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(driver, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(driver, 18, 0);
    info_row(driver, "Module wlan + cfg80211",
             vqeaf_state.wifi_driver_loaded ? "Đã nạp" : "Không có");
    info_row(driver, "Firmware WCN3990",
             vqeaf_state.wifi_firmware_present ? "Có" : "Thiếu");
    info_row(driver, "Thư mục firmware",
             vqeaf_state.wifi_firmware_present ?
             vqeaf_state.wifi_firmware_path : "Không tìm thấy");

    lv_obj_t *scan_head = vqeaf_row(root);
    vqeaf_label(scan_head, "Mạng khả dụng", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    lv_obj_t *rescan = vqeaf_label(scan_head, "Quét lại", VQEAF_FONT_BODY,
                                   VQEAF_C_ACCENT);
    vqeaf_on_click(rescan, wifi_rescan_cb, NULL);

    if(g_wifi_scan_note[0] != '\0') {
        lv_obj_t *box = vqeaf_card(root, lv_pct(100), 110);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(box, 16, 0);
        vqeaf_glyph(box, VQEAF_SYM_SIGNAL_WIFI_OFF, VQEAF_ICON_SM,
                    VQEAF_C_TEXT_MUTE);
        lv_obj_t *msg = vqeaf_label(box, g_wifi_scan_note, VQEAF_FONT_CAPTION,
                                    VQEAF_C_TEXT_DIM);
        lv_obj_set_flex_grow(msg, 1);
    }
    else if(g_wifi_count == 0) {
        lv_obj_t *box = vqeaf_card(root, lv_pct(100), 96);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        vqeaf_label(box, "Không tìm thấy mạng nào.", VQEAF_FONT_CAPTION,
                    VQEAF_C_TEXT_DIM);
    }
    else {
        for(int i = 0; i < g_wifi_count; i++) {
            lv_obj_t *row = vqeaf_card(root, lv_pct(100), 108);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(row, 16, 0);
            vqeaf_tile(row, VQEAF_SYM_WIFI, lv_color_hex(0x1A5A6A),
                       VQEAF_C_ACCENT, 64, 20);

            lv_obj_t *text = vqeaf_column(row);
            lv_obj_set_flex_grow(text, 1);
            lv_obj_set_style_pad_row(text, 0, 0);
            lv_obj_t *name = vqeaf_label(text, g_wifi_nets[i].ssid,
                                          VQEAF_FONT_BODY, VQEAF_C_TEXT);
            lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_width(name, lv_pct(100));
            char sub[48];
            snprintf(sub, sizeof(sub), "%s · %d%%",
                     vqeaf_wifi_enc_name(g_wifi_nets[i].enc),
                     g_wifi_nets[i].signal);
            vqeaf_label(text, sub, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);

            vqeaf_glyph(row,
                        g_wifi_nets[i].secure ? VQEAF_SYM_LOCK :
                                                VQEAF_SYM_LOCK_OPEN,
                        VQEAF_ICON_SM, g_wifi_nets[i].secure ?
                        VQEAF_C_TEXT_DIM : VQEAF_C_GREEN);
            vqeaf_glyph(row, VQEAF_SYM_CHEVRON_RIGHT, VQEAF_ICON_SM,
                        VQEAF_C_TEXT_MUTE);
            vqeaf_on_click(row, wifi_network_cb, (void *)(intptr_t)i);
        }
    }

    lv_obj_t *note = vqeaf_label(root,
        "Firmware không được kèm trong ảnh initramfs: blob WCN3990 phải lấy "
        "từ chính máy. Khi thiếu firmware, driver không tạo wlan0 nên không "
        "thể scan hay kết nối. Tầng driver/firmware và tầng supplicant được "
        "tách riêng, theo cách HakiOS mô tả trong docs/driver-review/"
        "wifi-stack.md.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(note, lv_pct(100));
}

/* ---- drivers and drives -------------------------------------------------- */

static void build_drivers(lv_obj_t *content)
{
    char text[96];
    lv_obj_t *root = page_root(content);

    detail_header(root, "Trình điều khiển");

    vqeaf_label(root, "Phần cứng", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    lv_obj_t *hw = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hw, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(hw, 18, 0);
    info_row(hw, "Màn hình DRM/KMS",
             vqeaf_state.drm_present ? "card0 sẵn sàng" : "Không có");
    snprintf(text, sizeof(text), "%d event", vqeaf_state.touch_events);
    info_row(hw, "Cảm ứng evdev",
             vqeaf_state.touch_events > 0 ? text : "Không có");
    info_row(hw, "Bluetooth",
             vqeaf_state.bt_if_present ? vqeaf_state.bt_ifname :
             vqeaf_state.bt_driver_loaded ? "module, chua co netdev" :
                                            "Khong co");
    info_row(hw, "USB gadget (UDC)",
             vqeaf_state.usb_udc_present ? "Đã bind" : "Không có");
    info_row(hw, "Đèn nền",
             vqeaf_state.backlight_present ? "Có" : "Không có");
    info_row(hw, "Pin",
             vqeaf_state.battery_valid ? "Có provider" : "Không có");
    info_row(hw, "Wi-Fi",
             vqeaf_state.wifi_if_present ? vqeaf_state.wifi_ifname :
             vqeaf_state.wifi_driver_loaded ? "module, chưa có netdev" :
                                              "Không có");

    vqeaf_label(root, "Ổ đĩa", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    lv_obj_t *drives = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(drives, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(drives, 18, 0);
    if(vqeaf_state.storage_valid) {
        char used[24];
        char total[24];
        format_bytes(vqeaf_state.storage_total - vqeaf_state.storage_free,
                     used, sizeof(used));
        format_bytes(vqeaf_state.storage_total, total, sizeof(total));
        snprintf(text, sizeof(text), "%s / %s", used, total);
    }
    else snprintf(text, sizeof(text), "Không đọc được");
    info_row(drives, "Root initramfs", text);
    if(vqeaf_state.usb_lun_present) {
        char size[24];
        format_bytes(vqeaf_state.usb_lun_size, size, sizeof(size));
        snprintf(text, sizeof(text), "%s (LUN)", size);
    }
    else snprintf(text, sizeof(text), "Không có");
    info_row(drives, "QEAFRESCUE", text);
    info_row(drives, "Phân vùng ghi được", "p77/recovery");

    lv_obj_t *note = vqeaf_label(root,
        "Trạng thái đọc trực tiếp từ /sys, /dev và /proc. Không có mục nào "
        "được giả lập; thiết bị thiếu driver/firmware hiện “Không có”.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(note, lv_pct(100));
}

/* ---- file sync with QEAFRESCUE ------------------------------------------- */

static void sync_refresh_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_ui_show(VQEAF_PAGE_SYNC);
}

static void build_sync(lv_obj_t *content)
{
    vqeaf_sync_entry_t files[32];
    int count;
    lv_obj_t *root = page_root(content);

    detail_header(root, "Tệp & đồng bộ");

    lv_obj_t *hero = vqeaf_card(root, lv_pct(100), 168);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hero, 18, 0);
    vqeaf_tile(hero, VQEAF_SYM_FOLDER_OPEN, lv_color_hex(0x3A2E14),
               VQEAF_C_WARN, 84, 26);
    lv_obj_t *text = vqeaf_column(hero);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_style_pad_row(text, 2, 0);
    vqeaf_label(text, "QEAFRESCUE", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    vqeaf_label(text, "Cùng một volume FAT16 với PC",
                VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    lv_obj_t *refresh = vqeaf_obj(hero);
    lv_obj_set_size(refresh, 150, 64);
    lv_obj_set_style_bg_color(refresh, VQEAF_C_ACCENT, 0);
    lv_obj_set_style_bg_opa(refresh, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(refresh, 32, 0);
    lv_obj_set_flex_flow(refresh, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(refresh, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    vqeaf_label(refresh, "Đồng bộ", VQEAF_FONT_CAPTION, VQEAF_C_BG_DEEP);
    vqeaf_on_click(refresh, sync_refresh_cb, NULL);

    count = vqeaf_ui_sync_list("/", files,
                               (int)(sizeof(files) / sizeof(files[0])));
    char subtitle[64];
    snprintf(subtitle, sizeof(subtitle), "%d mục trong thư mục gốc", count);
    vqeaf_label(root, subtitle, VQEAF_FONT_TITLE, VQEAF_C_TEXT);

    if(count <= 0) {
        lv_obj_t *empty = vqeaf_card(root, lv_pct(100), 110);
        lv_obj_set_flex_flow(empty, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(empty, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        vqeaf_label(empty, "Chưa đọc được volume QEAFRESCUE.",
                    VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    }
    else {
        for(int i = 0; i < count; i++) {
            lv_obj_t *row = vqeaf_card(root, lv_pct(100), 92);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(row, 14, 0);
            vqeaf_tile(row,
                       files[i].is_dir ? VQEAF_SYM_FOLDER : VQEAF_SYM_NOTE,
                       lv_color_hex(0x1A2A46), VQEAF_C_CYAN, 56, 18);
            lv_obj_t *name = vqeaf_label(row, files[i].name, VQEAF_FONT_BODY,
                                         VQEAF_C_TEXT);
            lv_obj_set_flex_grow(name, 1);
            lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
            char size[32];
            if(files[i].is_dir)
                snprintf(size, sizeof(size), "thư mục");
            else
                format_bytes(files[i].size, size, sizeof(size));
            lv_obj_t *value = vqeaf_label(row, size, VQEAF_FONT_CAPTION,
                                          VQEAF_C_TEXT_DIM);
            lv_obj_set_width(value, 130);
            lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        }
    }

    lv_obj_t *note = vqeaf_label(root,
        "Tệp do PC thả vào ổ QEAFRESCUE hiện ở đây sau khi chạm Đồng bộ; "
        "tệp do OS ghi (DEVINFO, RESPONSE, SCREEN.RAW) hiện trên PC ngay. "
        "Không có bản sao thứ hai: cả hai bên cùng đọc một ảnh FAT16 trong RAM.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(note, lv_pct(100));
}

/* ---- file manager -------------------------------------------------------- */

static char g_files_path[256] = "/";
static int g_files_tab;             /* 0 = rootfs, 1 = QEAFRESCUE, 2 = volumes */
/* Picker mode: the Files page becomes "choose a .qeapp to install", starting
 * in the device storage tree instead of the rescue root. */
static int g_pick_mode;

void vqeaf_ui_pick_qeapp(void)
{
    g_pick_mode = 1;
    g_files_tab = 1;
    snprintf(g_files_path, sizeof(g_files_path), "/Install/Apps");
    vqeaf_ui_show(VQEAF_PAGE_FILES);
}
static int g_files_count;
static vqeaf_file_t g_files_entries[64];

static void files_reload(void)
{
    if(g_files_tab == 2) {
        vqeaf_mount_t mounts[32];
        int count = vqeaf_sys_mounts(mounts, 32, 0);
        int max = (int)(sizeof(g_files_entries) / sizeof(g_files_entries[0]));
        int n = 0;

        if(count < 0) count = 0;
        /* Only real mount points are listed. An NTFS volume is not implied
         * by a Windows-looking filename, and no entry is fabricated here. */
        for(int i = 0; i < count && n < max; i++) {
            snprintf(g_files_entries[n].name,
                     sizeof(g_files_entries[n].name), "%s", mounts[i].path);
            g_files_entries[n].size = 0;
            g_files_entries[n].is_dir = 1;
            n++;
        }
        g_files_count = n;
        return;
    }
    if(g_files_tab == 0) {
        g_files_count = vqeaf_files_list(g_files_path, g_files_entries,
                                         (int)(sizeof(g_files_entries) /
                                               sizeof(g_files_entries[0])));
        return;
    }

    {
        vqeaf_sync_entry_t sync[64];
        int count = vqeaf_ui_sync_list(g_files_path, sync,
                                       (int)(sizeof(sync) / sizeof(sync[0])));
        int max = (int)(sizeof(g_files_entries) / sizeof(g_files_entries[0]));
        int n = 0;
        if(count < 0) count = 0;
        /* fat16_list_dir omits "." and ".."; surface a parent entry so the
         * rescue tab walks back up the same way the rootfs tab does. */
        if(strcmp(g_files_path, "/") != 0 && n < max) {
            snprintf(g_files_entries[n].name,
                     sizeof(g_files_entries[n].name), "..");
            g_files_entries[n].size = 0;
            g_files_entries[n].is_dir = 1;
            n++;
        }
        for(int i = 0; i < count && n < max; i++) {
            if(strcmp(sync[i].name, "..") == 0 ||
               strcmp(sync[i].name, ".") == 0)
                continue;
            snprintf(g_files_entries[n].name,
                     sizeof(g_files_entries[n].name), "%s", sync[i].name);
            g_files_entries[n].size = sync[i].size;
            g_files_entries[n].is_dir = sync[i].is_dir;
            n++;
        }
        g_files_count = n;
    }
}

static void files_tab_cb(lv_event_t *event)
{
    int tab = (int)(intptr_t)lv_event_get_user_data(event);
    if(tab < 0 || tab > 2) return;
    g_pick_mode = 0;
    g_files_tab = tab;
    snprintf(g_files_path, sizeof(g_files_path), "/");
    vqeaf_ui_show(VQEAF_PAGE_FILES);
}

static void files_refresh_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_ui_show(VQEAF_PAGE_FILES);
}

static void files_back_cb(lv_event_t *event)
{
    (void)event;
    if(strcmp(g_files_path, "/") != 0) {
        char parent[256];
        vqeaf_files_parent(g_files_path, parent, sizeof(parent));
        snprintf(g_files_path, sizeof(g_files_path), "%s", parent);
        vqeaf_ui_show(VQEAF_PAGE_FILES);
        return;
    }
    g_pick_mode = 0;
    vqeaf_ui_show(VQEAF_PAGE_APPS);
}

static void files_entry_cb(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    char next[256];

    if(index < 0 || index >= g_files_count) return;
    if(!g_files_entries[index].is_dir) return;
    if(g_files_tab == 2) {
        snprintf(g_files_path, sizeof(g_files_path), "%s",
                 g_files_entries[index].name);
        g_files_tab = 0;
        vqeaf_ui_show(VQEAF_PAGE_FILES);
        return;
    }
    if(strcmp(g_files_entries[index].name, "..") == 0) {
        vqeaf_files_parent(g_files_path, next, sizeof(next));
        snprintf(g_files_path, sizeof(g_files_path), "%s", next);
        vqeaf_ui_show(VQEAF_PAGE_FILES);
        return;
    }
    if(vqeaf_files_join(g_files_path, g_files_entries[index].name, next,
                        sizeof(next)) == 0) {
        snprintf(g_files_path, sizeof(g_files_path), "%s", next);
        vqeaf_ui_show(VQEAF_PAGE_FILES);
    }
}

static lv_obj_t *g_file_view;
static lv_obj_t *g_gallery_layer;

/* Decoded preview pixels for the file viewer. Kept in a shell-local buffer so
 * the lv_image can reference it directly; freed when the viewer closes or a
 * new file is opened. ARGB8888 here means BGRA byte order in memory. */
static uint8_t *g_view_pixels;
static lv_image_dsc_t g_view_dsc;
static int g_view_w, g_view_h;

static void view_free_pixels(void)
{
    if(g_view_pixels != NULL) {
        lv_free(g_view_pixels);
        g_view_pixels = NULL;
    }
    g_view_w = g_view_h = 0;
}

static void view_set_image(int w, int h)
{
    g_view_w = w;
    g_view_h = h;
    memset(&g_view_dsc, 0, sizeof(g_view_dsc));
    g_view_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_view_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
    g_view_dsc.header.w = w;
    g_view_dsc.header.h = h;
    g_view_dsc.header.stride = w * 4;
    g_view_dsc.data_size = (size_t)w * h * 4;
    g_view_dsc.data = g_view_pixels;
}

static uint16_t view_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t view_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* PNG via the bundled lodepng (RGBA out) -> BGRA preview. */
static int view_decode_png(const uint8_t *data, size_t len)
{
    unsigned w = 0, h = 0;
    unsigned char *rgba = NULL;
    if(lodepng_decode32(&rgba, &w, &h, data, len) != 0 || rgba == NULL) {
        if(rgba != NULL) lv_free(rgba);
        return -1;
    }
    if(w == 0 || h == 0 || w > 1024 || h > 1024) {
        lv_free(rgba);
        return -1;
    }
    g_view_pixels = lv_malloc((size_t)w * h * 4);
    if(g_view_pixels == NULL) {
        lv_free(rgba);
        return -1;
    }
    for(size_t i = 0; i < (size_t)w * h; i++) {
        g_view_pixels[i * 4 + 0] = rgba[i * 4 + 2];
        g_view_pixels[i * 4 + 1] = rgba[i * 4 + 1];
        g_view_pixels[i * 4 + 2] = rgba[i * 4 + 0];
        g_view_pixels[i * 4 + 3] = rgba[i * 4 + 3];
    }
    lv_free(rgba);
    view_set_image((int)w, (int)h);
    return 0;
}

/* Uncompressed 24/32-bit BMP, bottom-up or top-down. Pixels are already BGR. */
static int view_decode_bmp(const uint8_t *d, size_t n)
{
    if(n < 54 || d[0] != 'B' || d[1] != 'M') return -1;
    uint32_t off = view_le32(d + 10);
    int32_t w = (int32_t)view_le32(d + 18);
    int32_t h = (int32_t)view_le32(d + 22);
    uint16_t bpp = view_le16(d + 28);
    uint32_t comp = view_le32(d + 30);
    if(w <= 0 || w > 1024 || h == 0 || comp != 0) return -1;
    int topdown = h < 0;
    if(topdown) h = -h;
    if(h > 1024 || (bpp != 24 && bpp != 32)) return -1;
    size_t stride = ((size_t)w * (bpp / 8) + 3) & ~(size_t)3;
    if(off + stride * (size_t)h > n) return -1;
    g_view_pixels = lv_malloc((size_t)w * h * 4);
    if(g_view_pixels == NULL) return -1;
    for(int y = 0; y < h; y++) {
        int sy = topdown ? y : (h - 1 - y);
        const uint8_t *row = d + off + (size_t)sy * stride;
        for(int x = 0; x < w; x++) {
            const uint8_t *p = row + (size_t)x * (bpp / 8);
            uint8_t *o = g_view_pixels + ((size_t)y * w + x) * 4;
            o[0] = p[0];
            o[1] = p[1];
            o[2] = p[2];
            o[3] = bpp == 32 ? p[3] : 255;
        }
    }
    view_set_image(w, h);
    return 0;
}

/* Full-size image preview from the RAM LUN: reads the whole file (screenshots
 * are several MB), decodes with the same PNG/BMP/JPEG decoders as the file
 * viewer, and shows the real pixels. No placeholder image is ever drawn. */
/* Builds a real thumbnail from the decoded pixels of the actual file. Uses the
 * file viewer's PNG/BMP/JPEG decoders, then nearest-neighbour downsamples into
 * a fixed per-card buffer. No stock photo or placeholder image is drawn. */
static int gallery_make_thumb(const char *path, int idx)
{
    uint8_t *data;
    int got, decoded = -1;
    vqeaf_media_info_t info;
    vqeaf_file_t files[24];
    size_t len = 0;
    const char *name;

    if(path == NULL || idx < 0 || idx >= (int)GALLERY_MAX_THUMBS) return -1;
    name = strrchr(path, '/');
    name = name != NULL ? name + 1 : path;
    int count = vqeaf_files_list("/Pictures", files, 24);
    for(int i = 0; i < count; i++)
        if(strcmp(files[i].name, name) == 0) { len = files[i].size; break; }
    if(len == 0 || len > 24u * 1024u * 1024u) return -1;
    data = lv_malloc(len);
    if(data == NULL) return -1;
    got = vqeaf_files_read(path, data, len);
    if(got > 0 && vqeaf_media_inspect(data, (size_t)got, &info) == 0) {
        if(info.type == VQEAF_MEDIA_PNG) decoded = view_decode_png(data, (size_t)got);
        else if(info.type == VQEAF_MEDIA_BMP) decoded = view_decode_bmp(data, (size_t)got);
        else if(info.type == VQEAF_MEDIA_JPEG) decoded = view_decode_jpg(data, (size_t)got);
    }
    lv_free(data);
    if(decoded != 0) { view_free_pixels(); return -1; }

    for(uint32_t y = 0; y < GALLERY_THUMB_H; y++) {
        uint32_t sy = (y * (uint32_t)g_view_h) / GALLERY_THUMB_H;
        for(uint32_t x = 0; x < GALLERY_THUMB_W; x++) {
            uint32_t sx = (x * (uint32_t)g_view_w) / GALLERY_THUMB_W;
            uint32_t src = (sy * (uint32_t)g_view_w + sx) * 4u;
            uint32_t dst = (y * GALLERY_THUMB_W + x) * 4u;
            memcpy(g_gallery_thumb_px[idx] + dst, g_view_pixels + src, 4);
        }
    }
    view_free_pixels();
    memset(&g_gallery_thumb_dsc[idx], 0, sizeof(g_gallery_thumb_dsc[idx]));
    g_gallery_thumb_dsc[idx].header.magic = LV_IMAGE_HEADER_MAGIC;
    g_gallery_thumb_dsc[idx].header.cf = LV_COLOR_FORMAT_ARGB8888;
    g_gallery_thumb_dsc[idx].header.w = GALLERY_THUMB_W;
    g_gallery_thumb_dsc[idx].header.h = GALLERY_THUMB_H;
    g_gallery_thumb_dsc[idx].header.stride = GALLERY_THUMB_W * 4u;
    g_gallery_thumb_dsc[idx].data_size =
        (size_t)GALLERY_THUMB_W * GALLERY_THUMB_H * 4u;
    g_gallery_thumb_dsc[idx].data = g_gallery_thumb_px[idx];
    g_gallery_thumb_ok[idx] = 1;
    return 0;
}

static void gallery_close_cb(lv_event_t *event)
{
    (void)event;
    if(g_gallery_layer != NULL) {
        lv_obj_delete(g_gallery_layer);
        g_gallery_layer = NULL;
    }
    view_free_pixels();
}

static void gallery_open_image(const char *path)
{
    uint8_t *data;
    int got, decoded = -1;
    vqeaf_media_info_t info;
    vqeaf_file_t files[24];
    size_t len = 0;
    const char *name;

    if(path == NULL) return;
    name = strrchr(path, '/');
    name = name != NULL ? name + 1 : path;
    int count = vqeaf_files_list("/Pictures", files, 24);
    for(int i = 0; i < count; i++)
        if(strcmp(files[i].name, name) == 0) { len = files[i].size; break; }
    if(len == 0 || len > 24u * 1024u * 1024u) return;

    data = lv_malloc(len);
    if(data == NULL) return;
    got = vqeaf_files_read(path, data, len);
    if(got > 0 && vqeaf_media_inspect(data, (size_t)got, &info) == 0) {
        if(info.type == VQEAF_MEDIA_PNG) decoded = view_decode_png(data, (size_t)got);
        else if(info.type == VQEAF_MEDIA_BMP) decoded = view_decode_bmp(data, (size_t)got);
        else if(info.type == VQEAF_MEDIA_JPEG) decoded = view_decode_jpg(data, (size_t)got);
    }
    lv_free(data);
    if(decoded != 0) { view_free_pixels(); return; }

    g_gallery_layer = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_gallery_layer, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(g_gallery_layer, lv_color_hex(0x05060A), 0);
    lv_obj_set_style_pad_all(g_gallery_layer, 12, 0);
    lv_obj_t *img = lv_image_create(g_gallery_layer);
    int avail = lv_obj_get_width(g_gallery_layer) - 24;
    int zoom = 256;
    if(g_view_w > 0 && g_view_w > avail) zoom = avail * 256 / g_view_w;
    lv_image_set_scale(img, (uint32_t)zoom);
    lv_image_set_src(img, &g_view_dsc);
    lv_obj_center(img);
    lv_obj_t *close = lv_button_create(g_gallery_layer);
    lv_obj_add_event_cb(close, gallery_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(close);
    lv_label_set_text(cl, "Đóng");
    lv_obj_center(cl);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -8);
}

/* JPEG via the bundled tjpgd (RGB888 MCU blocks) -> BGRA preview. */
typedef struct {
    const uint8_t *data;
    size_t len, pos;
} view_jpg_src_t;

static size_t view_jpg_in(JDEC *jd, uint8_t *buf, size_t n)
{
    view_jpg_src_t *s = (view_jpg_src_t *)jd->device;
    size_t avail = s->len - s->pos;
    if(n > avail) n = avail;
    if(buf != NULL) {
        memcpy(buf, s->data + s->pos, n);
        s->pos += n;
    }
    else {
        s->pos += n;
    }
    return n;
}

static int view_jpg_out(JDEC *jd, void *bitmap, JRECT *rect)
{
    (void)jd;
    const uint8_t *rgb = (const uint8_t *)bitmap;
    int rw = rect->right - rect->left + 1;
    int rh = rect->bottom - rect->top + 1;
    for(int y = 0; y < rh; y++) {
        int dy = rect->top + y;
        if(dy >= g_view_h) break;
        for(int x = 0; x < rw; x++) {
            int dx = rect->left + x;
            if(dx >= g_view_w) break;
            const uint8_t *s = rgb + ((size_t)y * rw + x) * 3;
            uint8_t *o = g_view_pixels + ((size_t)dy * g_view_w + dx) * 4;
            o[0] = s[2];
            o[1] = s[1];
            o[2] = s[0];
            o[3] = 255;
        }
    }
    return 1;
}

static int view_decode_jpg(const uint8_t *data, size_t len)
{
    JDEC jd;
    uint8_t *pool = lv_malloc(4096);
    if(pool == NULL) return -1;
    view_jpg_src_t src = { data, len, 0 };
    if(jd_prepare(&jd, view_jpg_in, pool, 4096, &src) != JDR_OK) {
        lv_free(pool);
        return -1;
    }
    int w = jd.width, h = jd.height;
    if(w <= 0 || h <= 0 || w > 1024 || h > 1024) {
        lv_free(pool);
        return -1;
    }
    g_view_pixels = lv_malloc((size_t)w * h * 4);
    if(g_view_pixels == NULL) {
        lv_free(pool);
        return -1;
    }
    memset(g_view_pixels, 0, (size_t)w * h * 4);
    g_view_w = w;
    g_view_h = h;
    JRESULT rc = jd_decomp(&jd, view_jpg_out, 0);
    lv_free(pool);
    if(rc != JDR_OK) {
        view_free_pixels();
        return -1;
    }
    view_set_image(w, h);
    return 0;
}

static void file_view_close_cb(lv_event_t *event)
{
    (void)event;
    if(g_file_view != NULL) {
        lv_obj_delete(g_file_view);
        g_file_view = NULL;
    }
    view_free_pixels();
}

static void files_view_cb(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    lv_display_t *display = lv_display_get_default();
    int width = (int)lv_display_get_horizontal_resolution(display);
    int height = (int)lv_display_get_vertical_resolution(display);
    uint8_t *buffer;
    uint8_t *heap = NULL;
    size_t size = 0;
    int read = -1;
    char meta[160];
    vqeaf_file_kind_t kind;

    if(index < 0 || index >= g_files_count) return;
    kind = vqeaf_files_kind(g_files_entries[index].name);
    buffer = malloc(32 * 1024);

    if(buffer != NULL) {
        if(g_files_tab == 0) {
            char path[512];
            if(snprintf(path, sizeof(path), "%s/%s", g_files_path,
                        g_files_entries[index].name) < (int)sizeof(path))
                read = vqeaf_files_read(path, buffer, 32 * 1024);
        }
        else {
            char path[512];
            if(vqeaf_files_join(g_files_path, g_files_entries[index].name,
                                path, sizeof(path)) == 0 &&
               vqeaf_ui_read_file(path, &heap, &size) == 0 && heap != NULL) {
                size_t copy = size < 32 * 1024 ? size : 32 * 1024;
                memcpy(buffer, heap, copy);
                read = (int)copy;
            }
        }
    }

    if(g_file_view != NULL) lv_obj_delete(g_file_view);
    view_free_pixels();
    g_file_view = vqeaf_obj(lv_layer_top());
    lv_obj_set_size(g_file_view, width, height);
    lv_obj_set_style_bg_color(g_file_view, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_file_view, LV_OPA_70, 0);
    lv_obj_add_flag(g_file_view, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *card = vqeaf_obj(g_file_view);
    lv_obj_set_size(card, width * 90 / 100, height * 80 / 100);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, VQEAF_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, VQEAF_C_LINE, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 10, 0);

    vqeaf_label(card, g_files_entries[index].name, VQEAF_FONT_TITLE,
                VQEAF_C_TEXT);
    snprintf(meta, sizeof(meta), "%s · %s · %s",
             vqeaf_files_kind_name(kind), vqeaf_files_kind_key(kind),
             g_files_entries[index].is_dir ? "thư mục" : "tệp");
    vqeaf_label(card, meta, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);

    lv_obj_t *content = vqeaf_obj(card);
    lv_obj_set_size(content, lv_pct(100), lv_pct(70));
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_style_bg_color(content, VQEAF_C_BG, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(content, 16, 0);
    lv_obj_set_style_pad_all(content, 14, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 10, 0);

    if(kind == VQEAF_FILE_TEXT || kind == VQEAF_FILE_JSON ||
       kind == VQEAF_FILE_MARKDOWN || kind == VQEAF_FILE_LOG) {
        if(read > 0) {
            buffer[read < 32 * 1024 ? read : 32 * 1024 - 1] = '\0';
            lv_obj_t *text = vqeaf_label(content, (const char *)buffer,
                                          VQEAF_FONT_CAPTION, VQEAF_C_TEXT);
            lv_obj_set_width(text, lv_pct(100));
            lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_WRAP);
        }
        else {
            vqeaf_label(content, "Không đọc được nội dung tệp.",
                        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
        }
    }
    else if(kind == VQEAF_FILE_RAW) {
        char hex[4096];
        size_t used = 0;
        int limit = read > 0 ? (read < 1500 ? read : 1500) : 0;
        used += (size_t)snprintf(hex + used, sizeof(hex) - used,
                                 "hex · %d bytes\n", read);
        for(int i = 0; i < limit && used + 4 < sizeof(hex); i++)
            used += (size_t)snprintf(hex + used, sizeof(hex) - used, "%02x ",
                                     buffer[i]);
        lv_obj_t *text = vqeaf_label(content, hex, VQEAF_FONT_CAPTION,
                                      VQEAF_C_TEXT);
        lv_obj_set_width(text, lv_pct(100));
        lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_WRAP);
    }
    else {
        vqeaf_media_info_t info;
        int have_info = read > 0 &&
                        vqeaf_media_inspect(buffer, (size_t)read, &info) == 0;
        int decoded = -1;
        if(have_info) {
            if(info.type == VQEAF_MEDIA_PNG)
                decoded = view_decode_png(buffer, (size_t)read);
            else if(info.type == VQEAF_MEDIA_JPEG)
                decoded = view_decode_jpg(buffer, (size_t)read);
            else if(info.type == VQEAF_MEDIA_BMP)
                decoded = view_decode_bmp(buffer, (size_t)read);
        }

        if(decoded == 0) {
            lv_obj_t *img = lv_image_create(content);
            int avail = width * 90 / 100 - 80;
            if(avail < 100) avail = 100;
            int zoom = 256;
            if(g_view_w > avail) zoom = avail * 256 / g_view_w;
            int max_h = height * 45 / 100;
            if(g_view_h * zoom / 256 > max_h && g_view_h > 0)
                zoom = max_h * 256 / g_view_h;
            if(zoom < 16) zoom = 16;
            lv_image_set_scale(img, (uint32_t)zoom);
            lv_image_set_src(img, &g_view_dsc);
            vqeaf_label(content, info.summary, VQEAF_FONT_CAPTION,
                        VQEAF_C_TEXT_DIM);
        }
        else {
            const char *note;
            if(have_info && (info.type == VQEAF_MEDIA_PNG ||
                             info.type == VQEAF_MEDIA_JPEG ||
                             info.type == VQEAF_MEDIA_BMP))
                note = "Ảnh quá lớn hoặc hỏng (chỉ đọc 32 KB đầu); chưa giải mã được.";
            else if(have_info && info.type == VQEAF_MEDIA_GIF)
                note = "Đã đọc metadata GIF; bộ giải mã GIF chưa có trong ảnh.";
            else if(kind == VQEAF_FILE_AUDIO)
                note = "Đã đọc metadata âm thanh; codec phát chưa có trong ảnh initramfs.";
            else if(kind == VQEAF_FILE_VIDEO)
                note = "Đã đọc metadata video; codec phát chưa có trong ảnh initramfs.";
            else if(kind == VQEAF_FILE_PDF)
                note = "Đã đọc metadata PDF; trình kết xuất PDF chưa có trong ảnh.";
            else if(kind == VQEAF_FILE_ARCHIVE)
                note = "Kho lưu trữ ZIP; dùng Tệp để duyệt, chưa giải nén.";
            else if(kind == VQEAF_FILE_QEAPP)
                note = "QEAPP; dùng nút Cài trong danh sách để cài.";
            else
                note = "Định dạng không xác định; chỉ hiển thị metadata.";
            vqeaf_label(content, have_info ? info.summary :
                        "Không nhận diện được định dạng.", VQEAF_FONT_BODY,
                        VQEAF_C_TEXT_DIM);
            vqeaf_label(content, note, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
        }
        if(read >= 0) {
            char sz[48];
            format_bytes((uint64_t)read, sz, sizeof(sz));
            vqeaf_label(content, sz, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
        }
    }

    lv_obj_t *close = vqeaf_obj(card);
    lv_obj_set_size(close, 150, 60);
    lv_obj_set_style_bg_color(close, VQEAF_C_SURFACE_3, 0);
    lv_obj_set_style_bg_opa(close, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(close, 30, 0);
    lv_obj_set_flex_flow(close, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(close, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    vqeaf_label(close, "Đóng", VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    vqeaf_on_click(close, file_view_close_cb, NULL);

    if(heap != NULL) free(heap);
    if(buffer != NULL) free(buffer);
}

static int ext_is(const char *name, size_t length, const char *ext)
{
    size_t n = strlen(ext);
    if(length < n) return 0;
    for(size_t i = 0; i < n; i++) {
        char a = name[length - n + i];
        char b = ext[i];
        if(a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if(b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if(a != b) return 0;
    }
    return 1;
}

/* Windows writes an 8.3 alias for .qeapp names ("HELLO~1.QEA"), and the FAT16
 * layer surfaces that alias for files copied from a PC. Accept both the long
 * extension and the alias so the Cài button exists for either name. */
static int files_is_qeapp(const char *name)
{
    size_t length = name != NULL ? strlen(name) : 0;
    if(length < 5) return 0;
    return (length > 6 && ext_is(name, length, ".qeapp")) ||
           ext_is(name, length, ".qea");
}

/* ---- .qeapp package installer --------------------------------------------
 * A three-state confirmation flow modelled on a package installer: confirm
 * (real package identity + declared permissions) -> installing (indeterminate
 * busy bar while the signed install runs) -> done (open the launcher) or error.
 * Identity comes from vqeaf_qeapp_preview, which verifies and parses without
 * committing; the install itself re-verifies the bytes, so a package swapped
 * after the preview is still caught. */
typedef enum {
    INSTALL_CONFIRM = 0,
    INSTALL_BUSY,
    INSTALL_DONE,
    INSTALL_ERROR
} install_state_t;

static lv_obj_t *g_install_modal;
static int g_install_state;
static vqeaf_qeapp_preview_t g_install_preview;
static lv_image_dsc_t g_install_icon_dsc;
static char g_install_path[512];
static char g_install_error[160];
static lv_timer_t *g_install_timer;

static void install_modal_rebuild(void);
static void installer_refresh_cb(lv_timer_t *timer);

static void install_modal_close(void)
{
    if(g_install_timer != NULL) {
        lv_timer_delete(g_install_timer);
        g_install_timer = NULL;
    }
    if(g_install_modal != NULL) {
        lv_obj_delete(g_install_modal);
        g_install_modal = NULL;
    }
    /* The installer page lists packages and recent installs, so it must be
     * rebuilt after a dialog closes. Deferred: LVGL is still dispatching the
     * tap that closed the dialog. */
    if(vqeaf_ui_page() == VQEAF_PAGE_INSTALLER) {
        lv_timer_t *timer = lv_timer_create(installer_refresh_cb, 30, NULL);
        if(timer != NULL) lv_timer_set_repeat_count(timer, 1);
    }
}

static void install_cancel_cb(lv_event_t *event)
{
    (void)event;
    install_modal_close();
}

static void install_open_cb(lv_event_t *event)
{
    (void)event;
    install_modal_close();
    vqeaf_ui_show(VQEAF_PAGE_QEAPP);
}

/* The signed install runs one tick after the busy card is drawn, so the
 * "installing" state is on screen while the real work happens. The bar is
 * indeterminate: the install is atomic, so there is no honest percentage. */
static void install_run_cb(lv_timer_t *timer)
{
    (void)timer;
    char error[160];

    g_install_timer = NULL;
    error[0] = '\0';
    if(vqeaf_qeapp_install(g_install_path, error, sizeof(error)) == 0) {
        g_install_state = INSTALL_DONE;
    }
    else {
        g_install_state = INSTALL_ERROR;
        snprintf(g_install_error, sizeof(g_install_error), "%s", error);
    }
    install_modal_rebuild();
}

static void install_confirm_cb(lv_event_t *event)
{
    (void)event;
    g_install_state = INSTALL_BUSY;
    install_modal_rebuild();
    g_install_timer = lv_timer_create(install_run_cb, 220, NULL);
    if(g_install_timer != NULL) lv_timer_set_repeat_count(g_install_timer, 1);
}

static lv_obj_t *install_icon(lv_obj_t *parent, int32_t size)
{
    const lv_image_dsc_t *dsc;
    if(g_install_preview.has_icon) {
        memset(&g_install_icon_dsc, 0, sizeof(g_install_icon_dsc));
        g_install_icon_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        g_install_icon_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
        g_install_icon_dsc.header.w = 64;
        g_install_icon_dsc.header.h = 64;
        g_install_icon_dsc.header.stride = 64 * 4;
        g_install_icon_dsc.data_size = 64u * 64u * 4u;
        g_install_icon_dsc.data = g_install_preview.icon;
        dsc = &g_install_icon_dsc;
    }
    else {
        dsc = vqeaf_app_icon(VQEAF_APP_ICON_QEAPP);
    }
    lv_obj_t *img = vqeaf_icon_img(parent, dsc, size);
    if(img == NULL)
        img = vqeaf_tile(parent, VQEAF_SYM_BOLT, lv_color_hex(0x2A2E6E),
                         lv_color_hex(0xB6BCFF), size, size * 28 / 100);
    return img;
}

static void install_head(lv_obj_t *card, const char *subtitle)
{
    lv_obj_t *head = vqeaf_obj(card);
    lv_obj_set_size(head, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 18, 0);
    install_icon(head, 88);
    lv_obj_t *col = vqeaf_obj(head);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 4, 0);
    lv_obj_t *name = vqeaf_label(col, g_install_preview.name,
                                 VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    lv_obj_set_width(name, lv_pct(100));
    lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
    lv_obj_t *sub = vqeaf_label(col, subtitle, VQEAF_FONT_CAPTION,
                                VQEAF_C_TEXT_DIM);
    lv_obj_set_width(sub, lv_pct(100));
    lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
}

static void install_permissions(lv_obj_t *card)
{
    static const struct { uint32_t bit; const char *label; } perms[] = {
        { QEAF_PERM_UI_DISPLAY,        "Hiển thị giao diện" },
        { QEAF_PERM_STORAGE_READ,      "Đọc bộ nhớ" },
        { QEAF_PERM_STORAGE_WRITE_APP, "Ghi dữ liệu ứng dụng" },
        { QEAF_PERM_INPUT_RECEIVE,     "Nhận thao tác nhập" },
        { QEAF_PERM_MEDIA_READ,        "Đọc thư viện media" },
    };
    lv_obj_t *box = vqeaf_obj(card);
    lv_obj_set_size(box, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(box, VQEAF_C_SURFACE_2, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 18, 0);
    lv_obj_set_style_pad_all(box, 16, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 8, 0);
    vqeaf_label(box, "Quyền yêu cầu", VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    int any = 0;
    for(size_t i = 0; i < sizeof(perms) / sizeof(perms[0]); i++) {
        if((g_install_preview.permissions & perms[i].bit) == 0) continue;
        lv_obj_t *row = vqeaf_obj(box);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 10, 0);
        vqeaf_glyph(row, VQEAF_SYM_CHECK, VQEAF_ICON_SM, VQEAF_C_ACCENT);
        vqeaf_label(row, perms[i].label, VQEAF_FONT_CAPTION, VQEAF_C_TEXT);
        any = 1;
    }
    if(!any)
        vqeaf_label(box, "Không yêu cầu quyền đặc biệt.",
                    VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
}

static lv_obj_t *install_buttons(lv_obj_t *card)
{
    lv_obj_t *row = vqeaf_weighted_row(card);
    lv_obj_set_height(row, 64);
    lv_obj_set_style_pad_column(row, 12, 0);
    return row;
}

static lv_obj_t *install_button(lv_obj_t *parent, const char *text,
                                int primary, lv_event_cb_t cb)
{
    lv_obj_t *b = vqeaf_obj(parent);
    lv_obj_set_height(b, 64);
    vqeaf_weight(b, 1);
    lv_obj_set_style_bg_color(b, primary ? VQEAF_C_ACCENT : VQEAF_C_SURFACE_3,
                              0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 32, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    vqeaf_label(b, text, VQEAF_FONT_CAPTION,
                primary ? VQEAF_C_BG_DEEP : VQEAF_C_TEXT_DIM);
    vqeaf_on_click(b, cb, NULL);
    return b;
}

static void install_busy_bar(lv_obj_t *card)
{
    /* The install is atomic, so there is no honest percentage: M3
     * indeterminate linear progress (sweeping indicator), not a fake %. */
    vqeaf_m3_progress_indeterminate(card);
}

static void install_modal_rebuild(void)
{
    lv_display_t *display = lv_display_get_default();
    int width = (int)lv_display_get_horizontal_resolution(display);
    int height = (int)lv_display_get_vertical_resolution(display);
    char meta[160];

    if(g_install_modal != NULL) {
        lv_obj_delete(g_install_modal);
        g_install_modal = NULL;
    }

    g_install_modal = vqeaf_obj(lv_layer_top());
    lv_obj_set_size(g_install_modal, width, height);
    lv_obj_set_style_bg_color(g_install_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_install_modal, LV_OPA_70, 0);
    lv_obj_add_flag(g_install_modal, LV_OBJ_FLAG_CLICKABLE);

    int card_w = width * 86 / 100;
    if(card_w > 560) card_w = 560;
    lv_obj_t *card = vqeaf_obj(g_install_modal);
    lv_obj_set_width(card, card_w);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, VQEAF_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 30, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, VQEAF_C_LINE, 0);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 16, 0);

    if(g_install_state == INSTALL_CONFIRM) {
        snprintf(meta, sizeof(meta), "v%s · %s", g_install_preview.version,
                 g_install_preview.id);
        install_head(card, meta);
        install_permissions(card);
        lv_obj_t *q = vqeaf_label(card,
            "Bạn có muốn cài đặt ứng dụng này không?",
            VQEAF_FONT_BODY, VQEAF_C_TEXT);
        lv_obj_set_width(q, lv_pct(100));
        lv_label_set_long_mode(q, LV_LABEL_LONG_WRAP);
        lv_obj_t *buttons = install_buttons(card);
        install_button(buttons, "HỦY", 0, install_cancel_cb);
        install_button(buttons, "CÀI ĐẶT", 1, install_confirm_cb);
    }
    else if(g_install_state == INSTALL_BUSY) {
        install_head(card, "Đang cài đặt...");
        install_busy_bar(card);
        lv_obj_t *buttons = install_buttons(card);
        install_button(buttons, "HỦY", 0, install_cancel_cb);
    }
    else if(g_install_state == INSTALL_DONE) {
        lv_obj_t *glyph = vqeaf_glyph(card, VQEAF_SYM_CHECK_CIRCLE,
                                      VQEAF_ICON, VQEAF_C_ACCENT);
        lv_obj_set_width(glyph, lv_pct(100));
        lv_obj_set_style_text_align(glyph, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *t = vqeaf_label(card, "Ứng dụng đã được cài đặt.",
                                  VQEAF_FONT_TITLE, VQEAF_C_ACCENT);
        lv_obj_set_width(t, lv_pct(100));
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        snprintf(meta, sizeof(meta), "%s · v%s", g_install_preview.name,
                 g_install_preview.version);
        lv_obj_t *m = vqeaf_label(card, meta, VQEAF_FONT_CAPTION,
                                  VQEAF_C_TEXT_DIM);
        lv_obj_set_width(m, lv_pct(100));
        lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *buttons = install_buttons(card);
        install_button(buttons, "XONG", 0, install_cancel_cb);
        install_button(buttons, "MỞ", 1, install_open_cb);
    }
    else {
        lv_obj_t *glyph = vqeaf_glyph(card, VQEAF_SYM_CLOSE, VQEAF_ICON,
                                      VQEAF_C_DANGER);
        lv_obj_set_width(glyph, lv_pct(100));
        lv_obj_set_style_text_align(glyph, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *t = vqeaf_label(card, "Không cài được", VQEAF_FONT_TITLE,
                                  VQEAF_C_DANGER);
        lv_obj_set_width(t, lv_pct(100));
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *m = vqeaf_label(card, g_install_error, VQEAF_FONT_CAPTION,
                                  VQEAF_C_TEXT_DIM);
        lv_obj_set_width(m, lv_pct(100));
        lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
        lv_obj_t *buttons = install_buttons(card);
        install_button(buttons, "ĐÓNG", 0, install_cancel_cb);
    }
}

/* Shared by the file manager and the installer app: verify the package and
 * open the confirmation dialog with its real identity, so both entry points
 * show the same name/version/permissions before anything is written. */
static void install_prompt_path(const char *path)
{
    char error[160];

    if(path == NULL || path[0] == '\0') return;
    snprintf(g_install_path, sizeof(g_install_path), "%s", path);
    error[0] = '\0';
    if(vqeaf_qeapp_preview(path, &g_install_preview, error,
                           sizeof(error)) == 0) {
        g_install_state = INSTALL_CONFIRM;
        g_install_error[0] = '\0';
    }
    else {
        g_install_state = INSTALL_ERROR;
        snprintf(g_install_error, sizeof(g_install_error), "%s", error);
    }
    install_modal_rebuild();
}

static void files_install_cb(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    char path[512];

    if(index < 0 || index >= g_files_count) return;
    if(vqeaf_files_join(g_files_path, g_files_entries[index].name,
                        path, sizeof(path)) != 0)
        return;
    install_prompt_path(path);
}

/* ---- installer app --------------------------------------------------------
 * A launcher entry that scans the device storage for .qeapp packages and
 * lists the installs made most recently. Installing goes through the same
 * confirmation dialog as every other entry point. */

#define INSTALLER_MAX 16

static char g_installer_paths[INSTALLER_MAX][256];
static int g_installer_count;

static void installer_scan(const char *path, int depth)
{
    vqeaf_sync_entry_t entries[24];
    int count = vqeaf_ui_sync_list(path, entries,
                                   (int)(sizeof(entries) / sizeof(entries[0])));
    if(count <= 0) return;
    for(int i = 0; i < count && g_installer_count < INSTALLER_MAX; i++) {
        char next[256];
        if(strcmp(entries[i].name, ".") == 0 ||
           strcmp(entries[i].name, "..") == 0)
            continue;
        if(vqeaf_files_join(path, entries[i].name, next, sizeof(next)) != 0)
            continue;
        if(entries[i].is_dir) {
            if(depth < 3) installer_scan(next, depth + 1);
        }
        else if(files_is_qeapp(entries[i].name)) {
            snprintf(g_installer_paths[g_installer_count],
                     sizeof(g_installer_paths[0]), "%s", next);
            g_installer_count++;
        }
    }
}

static void installer_row_cb(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    if(index < 0 || index >= g_installer_count) return;
    install_prompt_path(g_installer_paths[index]);
}

/* Rebuild after a dialog closes, once LVGL has finished dispatching the tap. */
static void installer_refresh_cb(lv_timer_t *timer)
{
    (void)timer;
    if(vqeaf_ui_page() == VQEAF_PAGE_INSTALLER)
        vqeaf_ui_show(VQEAF_PAGE_INSTALLER);
}

static void build_installer(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);

    page_title(root, "Trình cài đặt");
    g_installer_count = 0;
    installer_scan("/", 0);

    vqeaf_label(root, g_installer_count > 0 ? "Gói .qeapp tìm thấy" :
                                            "Chưa tìm thấy gói .qeapp",
                VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    if(g_installer_count == 0) {
        vqeaf_label(root,
            "Chép tệp .qeapp vào ổ QEAFRESCUE (hoặc Install/Apps) rồi mở "
            "lại trang này. Tệp ở mọi thư mục con đều được quét.",
            VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    }
    for(int i = 0; i < g_installer_count; i++) {
        const char *path = g_installer_paths[i];
        const char *slash = strrchr(path, '/');
        const char *leaf = slash ? slash + 1 : path;
        char dir[256];
        size_t dir_len = slash ? (size_t)(slash - path) : 1;
        lv_obj_t *row = vqeaf_card(root, lv_pct(100), 96);
        lv_obj_t *text;

        if(dir_len >= sizeof(dir)) dir_len = sizeof(dir) - 1;
        if(slash == NULL || dir_len == 0) {
            snprintf(dir, sizeof(dir), "/");
        }
        else {
            memcpy(dir, path, dir_len);
            dir[dir_len] = '\0';
        }
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 14, 0);
        vqeaf_tile(row, VQEAF_SYM_NOTE, lv_color_hex(0x1A2A46),
                   VQEAF_C_CYAN, 54, 17);
        text = vqeaf_obj(row);
        lv_obj_set_flex_grow(text, 1);
        lv_obj_set_height(text, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(text, 2, 0);
        {
            lv_obj_t *name = vqeaf_label(text, leaf, VQEAF_FONT_BODY,
                                         VQEAF_C_TEXT);
            lv_obj_set_width(name, lv_pct(100));
            lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
            vqeaf_label(text, dir, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
        }
        vqeaf_label(row, "Cài", VQEAF_FONT_CAPTION, VQEAF_C_ACCENT);
        vqeaf_on_click(row, installer_row_cb, (void *)(intptr_t)i);
    }

    vqeaf_label(root, "Lần cài gần đây", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    {
        int installed = vqeaf_qeapp_count();
        int shown = 0;
        if(installed <= 0)
            vqeaf_label(root, "Chưa cài ứng dụng nào.", VQEAF_FONT_CAPTION,
                        VQEAF_C_TEXT_DIM);
        for(int i = installed - 1; i >= 0 && shown < 5; i--, shown++) {
            const vqeaf_qeapp_t *app = vqeaf_qeapp_at(i);
            char meta[160];
            lv_obj_t *row;
            lv_obj_t *text;
            if(app == NULL) continue;
            row = vqeaf_card(root, lv_pct(100), 92);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(row, 14, 0);
            vqeaf_tile(row, VQEAF_SYM_APPS, lv_color_hex(0x1A3A5A),
                       lv_color_hex(0x6FA8FF), 54, 17);
            text = vqeaf_obj(row);
            lv_obj_set_flex_grow(text, 1);
            lv_obj_set_height(text, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(text, 2, 0);
            vqeaf_label(text, app->name, VQEAF_FONT_BODY, VQEAF_C_TEXT);
            snprintf(meta, sizeof(meta), "v%s · %s",
                     app->version[0] ? app->version : "?", app->id);
            vqeaf_label(text, meta, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
            vqeaf_label(row, "Mở", VQEAF_FONT_CAPTION, VQEAF_C_ACCENT);
            vqeaf_on_click(row, qeapp_open_cb, (void *)(intptr_t)i);
        }
    }
}

#ifndef __linux__
/* Host render harness only: drive the installer overlay into a given state
 * with the seeded demo package so the confirm/busy/done/error layouts can be
 * inspected without a phone. Never compiled into the device image. */
void vqeaf_install_render(int state)
{
    char error[160];

    vqeaf_qeapp_preview("demo.qeapp", &g_install_preview, error, sizeof(error));
    snprintf(g_install_path, sizeof(g_install_path), "demo.qeapp");
    g_install_state = state;
    if(state == INSTALL_ERROR)
        snprintf(g_install_error, sizeof(g_install_error),
                 "Chữ ký không hợp lệ hoặc tệp đã bị sửa.");
    else
        g_install_error[0] = '\0';
    install_modal_rebuild();
}

void vqeaf_install_render_close(void)
{
    install_modal_close();
}
#endif

static void files_tab_pill(lv_obj_t *parent, const char *text, int tab)
{
    int active = g_files_tab == tab;
    lv_obj_t *pill = vqeaf_obj(parent);
    lv_obj_set_height(pill, 60);
    lv_obj_set_style_bg_color(pill, active ? VQEAF_C_SURFACE_3 :
                                              VQEAF_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pill, 30, 0);
    lv_obj_set_style_border_width(pill, active ? 2 : 1, 0);
    lv_obj_set_style_border_color(pill,
        active ? VQEAF_C_ACCENT : VQEAF_C_LINE, 0);
    lv_obj_set_style_pad_hor(pill, 22, 0);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    vqeaf_label(pill, text, VQEAF_FONT_CAPTION,
                active ? VQEAF_C_ACCENT : VQEAF_C_TEXT_DIM);
    vqeaf_on_click(pill, files_tab_cb, (void *)(intptr_t)tab);
}

static void build_files(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);

    lv_obj_t *header = vqeaf_row(root);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 18, 0);
    lv_obj_set_style_pad_top(header, 4, 0);
    lv_obj_t *back = vqeaf_tile(header, VQEAF_SYM_ARROW_BACK,
                                VQEAF_C_SURFACE, VQEAF_C_TEXT, 72, 24);
    vqeaf_on_click(back, files_back_cb, NULL);
    vqeaf_label(header, g_pick_mode ? "Chọn gói QEAPP" : "Tệp",
                VQEAF_FONT_HEADLINE, VQEAF_C_TEXT);
    lv_obj_t *spacer = vqeaf_obj(header);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_t *refresh_icon = vqeaf_glyph(header, VQEAF_SYM_REFRESH,
                                         VQEAF_ICON_SM, VQEAF_C_TEXT_DIM);
    vqeaf_on_click(refresh_icon, files_refresh_cb, NULL);

    if(!g_pick_mode) {
        lv_obj_t *tabs = vqeaf_obj(root);
        lv_obj_set_size(tabs, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(tabs, 12, 0);
        files_tab_pill(tabs, "Hệ thống", 0);
        files_tab_pill(tabs, "QEAFRESCUE", 1);
        files_tab_pill(tabs, "Ổ đĩa", 2);
    }
    else {
        vqeaf_label(root,
            "Duyệt bộ nhớ trong rồi chạm một gói .qeapp để cài. "
            "Thư mục gợi ý: Install/Apps, UserData/Downloads.",
            VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    }

    lv_obj_t *path_row = vqeaf_row(root);
    char path_label[300];
    if(g_files_tab == 0)
        snprintf(path_label, sizeof(path_label), "%s", g_files_path);
    else if(g_files_tab == 2)
        snprintf(path_label, sizeof(path_label), "Các phân vùng đã mount");
    else
        snprintf(path_label, sizeof(path_label),
                 g_pick_mode ? "Bộ nhớ trong %s" : "QEAFRESCUE %s",
                 g_files_path);
    vqeaf_label(path_row, path_label, VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
    lv_obj_t *refresh = vqeaf_label(path_row,
        g_files_tab == 0 ? "Làm mới" : "Đồng bộ",
        VQEAF_FONT_CAPTION, VQEAF_C_ACCENT);
    vqeaf_on_click(refresh, files_refresh_cb, NULL);

    files_reload();
    if(g_files_count < 0) {
        lv_obj_t *box = vqeaf_card(root, lv_pct(100), 110);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(box, 16, 0);
        vqeaf_glyph(box, VQEAF_SYM_FOLDER, VQEAF_ICON_SM, VQEAF_C_TEXT_MUTE);
        lv_obj_t *msg = vqeaf_label(box, "Không mở được thư mục.",
                                    VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
        lv_obj_set_flex_grow(msg, 1);
    }
    else if(g_files_count == 0) {
        lv_obj_t *box = vqeaf_card(root, lv_pct(100), 100);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        vqeaf_label(box,
                    g_pick_mode ? "Thư mục này không có gói .qeapp. "
                                  "Dùng ← để lên thư mục." :
                    g_files_tab == 0 ? "Thư mục trống." :
                    g_files_tab == 2 ?
                    "Không có phân vùng nào được mount để duyệt." :
                    "QEAFRESCUE chưa có tệp. PC thả tệp vào ổ rồi Đồng bộ.",
                    VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    }
    else {
        for(int i = 0; i < g_files_count; i++) {
            /* The picker only offers packages, but still walks folders. */
            if(g_pick_mode && !g_files_entries[i].is_dir &&
               !files_is_qeapp(g_files_entries[i].name))
                continue;
            lv_obj_t *row = vqeaf_card(root, lv_pct(100), 88);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(row, 14, 0);
            vqeaf_tile(row,
                g_files_entries[i].is_dir ? VQEAF_SYM_FOLDER : VQEAF_SYM_NOTE,
                g_files_entries[i].is_dir ? lv_color_hex(0x3A2E14) :
                                            lv_color_hex(0x1A2A46),
                g_files_entries[i].is_dir ? VQEAF_C_WARN : VQEAF_C_CYAN,
                54, 17);
            lv_obj_t *name = vqeaf_label(row, g_files_entries[i].name,
                                          VQEAF_FONT_BODY, VQEAF_C_TEXT);
            lv_obj_set_flex_grow(name, 1);
            lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);

            int installable = g_files_tab == 1 &&
                              !g_files_entries[i].is_dir &&
                              files_is_qeapp(g_files_entries[i].name);
            if(installable) {
                lv_obj_t *install = vqeaf_label(row, "Cài",
                                                VQEAF_FONT_CAPTION,
                                                VQEAF_C_ACCENT);
                lv_obj_set_style_text_align(install, LV_TEXT_ALIGN_RIGHT, 0);
                vqeaf_on_click(row, files_install_cb, (void *)(intptr_t)i);
            }
            else {
                char size[32];
                if(g_files_entries[i].is_dir)
                    snprintf(size, sizeof(size), "thư mục");
                else
                    format_bytes(g_files_entries[i].size, size, sizeof(size));
                lv_obj_t *value = vqeaf_label(row, size, VQEAF_FONT_CAPTION,
                                              VQEAF_C_TEXT_DIM);
                lv_obj_set_width(value, 130);
                lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);

                if(g_files_entries[i].is_dir) {
                    vqeaf_glyph(row, VQEAF_SYM_CHEVRON_RIGHT, VQEAF_ICON_SM,
                                VQEAF_C_TEXT_MUTE);
                    vqeaf_on_click(row, files_entry_cb, (void *)(intptr_t)i);
                }
                else {
                    vqeaf_glyph(row, VQEAF_SYM_CHEVRON_RIGHT, VQEAF_ICON_SM,
                                VQEAF_C_TEXT_MUTE);
                    vqeaf_on_click(row, files_view_cb, (void *)(intptr_t)i);
                }
            }
        }
    }

    lv_obj_t *note = vqeaf_label(root,
        g_pick_mode ?
        "Đọc thẳng từ kho lưu trữ của thiết bị; gói vẫn được xác minh chữ ký "
        "và lưu vào kho p77 để còn sau reboot." :
        g_files_tab == 0 ?
        "Chỉ đọc: duyệt rootfs của OS, không xoá hay sửa tệp hệ thống." :
        g_files_tab == 2 ?
        "Danh sách lấy từ /proc/mounts. Chạm một phân vùng để duyệt thư mục "
        "gốc của nó; NTFS chỉ hiện khi đã được mount." :
        "Chạm một tệp .qeapp để mở trình cài đặt (xem tên, phiên bản và quyền "
        "trước khi cài). Cùng volume FAT16 mà PC thấy: tệp PC thả vào hiện sau "
        "khi chạm Đồng bộ; tệp OS ghi hiện trên PC.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(note, lv_pct(100));
}

/* ---- lock screen theme editor -------------------------------------------- */

static lv_obj_t *g_lock_preview_clock;

static void lock_font_cb(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    if(index < 0 || index >= (int)(sizeof(lock_fonts) / sizeof(lock_fonts[0])))
        return;
    g_lock_font = index;
    vqeaf_ui_show(VQEAF_PAGE_LOCK_THEME);
}

static void lock_color_cb(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    if(index < 0 || index >= (int)(sizeof(lock_colors) / sizeof(lock_colors[0])))
        return;
    g_lock_color = lock_colors[index];
    vqeaf_ui_show(VQEAF_PAGE_LOCK_THEME);
}

static void lock_weight_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);
    int value = lv_slider_get_value(slider);
    g_lock_weight = 40 + value * 60 / 100;
    if(g_lock_preview_clock != NULL)
        lv_obj_set_style_opa(g_lock_preview_clock,
                             (lv_opa_t)(g_lock_weight * 255 / 100), 0);
}

static void lock_done_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_ui_show(VQEAF_PAGE_LOCK);
}

static void build_lock_theme(lv_obj_t *content)
{
    lv_display_t *display = lv_obj_get_display(content);
    int width = (int)lv_display_get_horizontal_resolution(display);
    int height = (int)lv_display_get_vertical_resolution(display);
    int landscape = height < width;
    int preview_h = landscape ? 300 : 480;
    lv_obj_t *root = page_root(content);

    g_lock_preview_clock = NULL;

    lv_obj_t *header = vqeaf_row(root);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 18, 0);
    lv_obj_t *back = vqeaf_tile(header, VQEAF_SYM_ARROW_BACK,
                                VQEAF_C_SURFACE, VQEAF_C_TEXT, 72, 24);
    vqeaf_on_click(back, nav_cb, (void *)(intptr_t)VQEAF_PAGE_SETTINGS);
    vqeaf_label(header, "Màn hình khóa", VQEAF_FONT_HEADLINE, VQEAF_C_TEXT);
    lv_obj_t *spacer = vqeaf_obj(header);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_t *done = vqeaf_obj(header);
    lv_obj_set_size(done, 150, 66);
    lv_obj_set_style_bg_color(done, VQEAF_C_ACCENT, 0);
    lv_obj_set_style_bg_opa(done, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(done, 33, 0);
    lv_obj_set_flex_flow(done, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(done, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    vqeaf_label(done, "Xong", VQEAF_FONT_BODY, VQEAF_C_BG_DEEP);
    vqeaf_on_click(done, lock_done_cb, NULL);

    lv_obj_t *preview = vqeaf_card(root, lv_pct(100), preview_h);
    lv_obj_set_style_bg_color(preview,
        vqeaf_theme_get() == VQEAF_THEME_LIGHT ? VQEAF_C_BG :
                                                 lv_color_hex(0x0A0F12), 0);
    lv_obj_set_style_pad_all(preview, 18, 0);

    const lv_font_t *preview_font = lock_fonts[g_lock_font].font;
    if(landscape && g_lock_font == 0) preview_font = lock_fonts[1].font;
    lv_obj_t *clock = vqeaf_label(preview, "01:49", preview_font,
                                  lv_color_hex(lock_clock_rgb()));
    lv_obj_align(clock, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_set_style_opa(clock, (lv_opa_t)(g_lock_weight * 255 / 100), 0);
    g_lock_preview_clock = clock;
    lv_obj_t *date = vqeaf_label(preview, "Thứ 3, 10 Tháng 11",
                                  VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    lv_obj_align(date, LV_ALIGN_TOP_MID, 0,
                 landscape ? 120 : 174);

    for(int i = 0; i < (landscape ? 0 : 2); i++) {
        lv_obj_t *pill = vqeaf_obj(preview);
        lv_obj_set_size(pill, lv_pct(80), 52);
        lv_obj_align(pill, LV_ALIGN_BOTTOM_MID, 0, -22 - i * 62);
        lv_obj_set_style_bg_color(pill, VQEAF_C_SURFACE, 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_80, 0);
        lv_obj_set_style_radius(pill, 18, 0);
        lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(pill, 12, 0);
        lv_obj_set_style_pad_left(pill, 14, 0);
        lv_obj_t *dot = vqeaf_obj(pill);
        lv_obj_set_size(dot, 18, 18);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, i == 0 ? VQEAF_C_GREEN :
                                               VQEAF_C_CYAN, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        vqeaf_label(pill, i == 0 ? "Tin nhắn · bây giờ" :
                                   "Lịch · 15 phút nữa",
                    VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    }

    vqeaf_label(root, "Font và màu", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    lv_obj_t *chips = vqeaf_obj(root);
    lv_obj_set_width(chips, lv_pct(100));
    lv_obj_set_height(chips, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(chips, 14, 0);
    lv_obj_set_style_pad_row(chips, 14, 0);
    for(size_t i = 0; i < sizeof(lock_fonts) / sizeof(lock_fonts[0]); i++) {
        int selected = (int)i == g_lock_font;
        lv_obj_t *chip = vqeaf_obj(chips);
        lv_obj_set_size(chip, lv_pct(47), 150);
        lv_obj_set_style_bg_color(chip, VQEAF_C_SURFACE, 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(chip, 24, 0);
        lv_obj_set_style_border_width(chip, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(chip,
            selected ? VQEAF_C_ACCENT : VQEAF_C_LINE, 0);
        lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *sample = vqeaf_label(chip, "12", lock_fonts[i].font,
                                       VQEAF_C_TEXT);
        lv_obj_set_style_opa(sample, 220, 0);
        vqeaf_label(chip, lock_fonts[i].name, VQEAF_FONT_CAPTION,
                    selected ? VQEAF_C_ACCENT : VQEAF_C_TEXT_DIM);
        vqeaf_on_click(chip, lock_font_cb, (void *)(intptr_t)i);
    }

    vqeaf_label(root, "Độ đậm", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    lv_obj_t *slider = lv_slider_create(root);
    lv_obj_set_width(slider, lv_pct(100));
    lv_obj_set_height(slider, 18);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, (g_lock_weight - 40) * 100 / 60, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, VQEAF_C_SURFACE_3, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, VQEAF_C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, VQEAF_C_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_width(slider, 30, LV_PART_KNOB);
    lv_obj_set_style_height(slider, 30, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, lock_weight_cb, LV_EVENT_VALUE_CHANGED, NULL);

    vqeaf_label(root, "Màu đồng hồ", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    lv_obj_t *colors = vqeaf_obj(root);
    lv_obj_set_width(colors, lv_pct(100));
    lv_obj_set_height(colors, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(colors, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(colors, 14, 0);
    lv_obj_set_style_pad_row(colors, 14, 0);
    for(size_t i = 0; i < sizeof(lock_colors) / sizeof(lock_colors[0]); i++) {
        int selected = lock_colors[i] == g_lock_color;
        lv_obj_t *swatch = vqeaf_obj(colors);
        lv_obj_set_size(swatch, 70, 70);
        lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(swatch, lv_color_hex(lock_colors[i]), 0);
        lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(swatch, selected ? 3 : 1, 0);
        lv_obj_set_style_border_color(swatch,
            selected ? VQEAF_C_ACCENT : VQEAF_C_LINE, 0);
        vqeaf_on_click(swatch, lock_color_cb, (void *)(intptr_t)i);
    }

    lv_obj_t *note = vqeaf_label(root,
        "Thay đổi áp dụng cho màn hình khóa ngay trong phiên này; chưa ghi "
        "vào prefs vì violet chưa có phân vùng lưu cấu hình.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(note, lv_pct(100));
}

/* ---- PIN entry + Security ------------------------------------------------ */

/* PIN entry is a real numeric keypad. The same page serves three flows:
 * 0 = unlock the phone, 1 = choose a new PIN, 2 = confirm the new PIN. */
static char g_pin_entry[16];
static char g_pin_pending[16];
static int  g_pin_mode;
static lv_obj_t *g_pin_dots;
static lv_obj_t *g_pin_prompt;

static void pin_update_dots(void)
{
    char dots[24];
    size_t i, n = strlen(g_pin_entry);
    size_t expect = g_pin_mode == 0 ? pin_length() : 4;

    if(expect == 0) expect = 4;
    for(i = 0; i < expect && i + 1 < sizeof(dots); i++)
        dots[i] = i < n ? VQEAF_SYM_LOCK[0] : '.';
    dots[i < sizeof(dots) ? i : sizeof(dots) - 1] = '\0';
    /* A simple ASCII dot string reads more clearly than a glyph repeated. */
    for(i = 0; i < expect && i + 1 < sizeof(dots); i++)
        dots[i] = i < n ? '*' : '-';
    dots[i] = '\0';
    if(g_pin_dots != NULL) lv_label_set_text(g_pin_dots, dots);
}

static void pin_flow_done(int ok)
{
    if(g_pin_mode == 0) {
        if(ok) {
            g_pin_entry[0] = '\0';
            vqeaf_ui_show(VQEAF_PAGE_HOME);
        } else {
            g_pin_entry[0] = '\0';
            pin_update_dots();
            if(g_pin_prompt != NULL)
                lv_label_set_text(g_pin_prompt, "PIN sai. Thử lại.");
        }
        return;
    }
    if(g_pin_mode == 1) {
        if(ok) {
            strncpy(g_pin_pending, g_pin_entry, sizeof(g_pin_pending) - 1);
            g_pin_pending[sizeof(g_pin_pending) - 1] = '\0';
            g_pin_mode = 2;
            g_pin_entry[0] = '\0';
            pin_update_dots();
            if(g_pin_prompt != NULL)
                lv_label_set_text(g_pin_prompt, "Xác nhận PIN mới");
        }
        return;
    }
    /* mode 2: confirm */
    if(ok && strcmp(g_pin_pending, g_pin_entry) == 0) {
        pin_store(g_pin_pending);
        show_toast("Đã đặt PIN cho màn hình khóa");
        g_pin_mode = 0;
        g_pin_entry[0] = '\0';
        vqeaf_ui_show(VQEAF_PAGE_SECURITY);
    } else {
        g_pin_mode = 1;
        g_pin_entry[0] = '\0';
        pin_update_dots();
        if(g_pin_prompt != NULL)
            lv_label_set_text(g_pin_prompt, "Không khớp. Nhập lại PIN mới.");
    }
}

static void pin_key_cb(lv_event_t *event)
{
    const char *key = (const char *)lv_event_get_user_data(event);
    size_t expect = g_pin_mode == 0 ? pin_length() : 4;
    size_t n;

    if(key == NULL) return;
    if(expect == 0) expect = 4;
    if(strcmp(key, "del") == 0) {
        n = strlen(g_pin_entry);
        if(n > 0) g_pin_entry[n - 1] = '\0';
    } else if(strcmp(key, "ok") == 0) {
        pin_flow_done(1);
        return;
    } else if(strlen(g_pin_entry) < expect) {
        strcat(g_pin_entry, key);
    }
    pin_update_dots();
    if(strlen(g_pin_entry) == expect) {
        if(g_pin_mode == 0)
            pin_flow_done(pin_matches(g_pin_entry));
        else
            pin_flow_done(1);
    }
}

static void pin_cancel_cb(lv_event_t *event)
{
    (void)event;
    g_pin_entry[0] = '\0';
    if(g_pin_mode != 0) {
        g_pin_mode = 0;
        vqeaf_ui_show(VQEAF_PAGE_SECURITY);
    } else {
        vqeaf_ui_show(VQEAF_PAGE_LOCK);
    }
}

static void pin_set_from_security(lv_event_t *event)
{
    (void)event;
    g_pin_mode = 1;
    g_pin_entry[0] = '\0';
    vqeaf_ui_show(VQEAF_PAGE_PIN);
}

static void pin_clear_cb(lv_event_t *event)
{
    (void)event;
    pin_store("");
    g_pin_mode = 0;
    g_pin_entry[0] = '\0';
    show_toast("Đã xóa PIN");
    vqeaf_ui_show(VQEAF_PAGE_SECURITY);
}

static void build_pin(lv_obj_t *content)
{
    static const char *const keys[12] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "del", "0", "ok"
    };
    lv_obj_t *root = page_root(content);
    lv_obj_t *card;

    page_title(root, "Nhập PIN");

    if(g_pin_prompt == NULL || lv_obj_get_parent(g_pin_prompt) != root) {
        g_pin_prompt = vqeaf_label(root,
            g_pin_mode == 0 ? "Nhập PIN để mở khóa" :
            g_pin_mode == 1 ? "Nhập PIN mới (4 số)" : "Xác nhận PIN mới",
            VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
    } else {
        lv_label_set_text(g_pin_prompt,
            g_pin_mode == 0 ? "Nhập PIN để mở khóa" :
            g_pin_mode == 1 ? "Nhập PIN mới (4 số)" : "Xác nhận PIN mới");
    }

    card = vqeaf_card(root, lv_pct(100), 120);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    g_pin_dots = vqeaf_label(card, "", VQEAF_FONT_CLOCK, VQEAF_C_ACCENT);
    pin_update_dots();

    /* 3x4 keypad */
    lv_obj_t *grid = vqeaf_obj(root);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(grid, 8, 0);
    lv_obj_set_style_pad_row(grid, 14, 0);
    lv_obj_set_style_pad_column(grid, 14, 0);
    for(int i = 0; i < 12; i++) {
        lv_obj_t *btn = vqeaf_card(grid, 160, 110);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        const char *label = strcmp(keys[i], "del") == 0 ? "Xóa" :
                            strcmp(keys[i], "ok") == 0 ? "OK" : keys[i];
        vqeaf_label(btn, label, VQEAF_FONT_HEADLINE_BOLD, VQEAF_C_TEXT);
        lv_obj_add_event_cb(btn, pin_key_cb, LV_EVENT_CLICKED,
                            (void *)keys[i]);
    }

    lv_obj_t *cancel = vqeaf_label(root, "Hủy", VQEAF_FONT_BODY,
                                   VQEAF_C_TEXT_DIM);
    lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cancel, pin_cancel_cb, LV_EVENT_CLICKED, NULL);
}

static void security_bool_row_cb(lv_event_t *event)
{
    (void)event;
    show_toast("Tính năng này cần phần cứng/driver mà thiết bị không có.");
}

static void build_security(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);
    char pin_status[64];
    lv_obj_t *row;

    page_title(root, "Bảo mật");

    snprintf(pin_status, sizeof(pin_status), "%s",
             pin_enabled() ? "Đã đặt PIN" : "Chưa đặt PIN");
    row = vqeaf_card(root, lv_pct(100), 96);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 14, 0);
    vqeaf_tile(row, VQEAF_SYM_LOCK, lv_color_hex(0x1A5A3A),
               VQEAF_C_ACCENT, 54, 16);
    lv_obj_t *txt = vqeaf_column(row);
    lv_obj_set_flex_grow(txt, 1);
    vqeaf_label(txt, "PIN màn hình khóa", VQEAF_FONT_BODY, VQEAF_C_TEXT);
    vqeaf_label(txt, pin_status, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, pin_set_from_security, LV_EVENT_CLICKED, NULL);

    if(pin_enabled()) {
        lv_obj_t *clear = vqeaf_label(root, "Xóa PIN", VQEAF_FONT_BODY,
                                      VQEAF_C_TEXT_DIM);
        lv_obj_add_flag(clear, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(clear, pin_clear_cb, LV_EVENT_CLICKED, NULL);
    }

    /* Hardware-gated features are reported honestly, never faked. */
    static const struct { const char *sym; const char *name;
                          const char *detail; uint32_t color; } hw[] = {
        { VQEAF_SYM_LOCK, "Mở khóa bằng vân tay",
          "Không có driver/driver vân tay trên kernel này", 0x1A5A6A },
        { VQEAF_SYM_SECURITY, "Khóa chạm trong túi",
          "Không có cảm biến tiệm cận được xuất ra", 0x3A2A5A },
        { VQEAF_SYM_VOLUME_UP, "Rung khi chạm",
          "Không có driver rung được xuất ra", 0x5A1A2A },
    };
    for(size_t i = 0; i < sizeof(hw) / sizeof(hw[0]); i++) {
        row = vqeaf_card(root, lv_pct(100), 92);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 14, 0);
        vqeaf_tile(row, hw[i].sym, lv_color_hex(hw[i].color),
                   VQEAF_C_ACCENT, 50, 14);
        txt = vqeaf_column(row);
        lv_obj_set_flex_grow(txt, 1);
        vqeaf_label(txt, hw[i].name, VQEAF_FONT_BODY, VQEAF_C_TEXT);
        vqeaf_label(txt, hw[i].detail, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, security_bool_row_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *note = vqeaf_label(root,
        "PIN chỉ tồn tại trong phiên này; violet không có phân vùng lưu "
        "cấu hình nên khởi động lại sẽ xóa PIN.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(note, lv_pct(100));
}

/* ---- Default apps ---------------------------------------------------------
 * A generic "open" action (browser link, media file, document, launcher) is
 * routed to the app the user chose. Values live in the Settings service so
 * the chooser and the dispatcher share one source of truth. violet has no
 * prefs partition, so a reboot restores the built-in fallback. */
static const char *const default_keys[VQEAF_DEFAULT_COUNT] = {
    "default.browser", "default.music", "default.gallery",
    "default.files", "default.home"
};
static const char *const default_names[VQEAF_DEFAULT_COUNT] = {
    "Trình duyệt", "Trình phát nhạc", "Xem ảnh",
    "Quản lý tệp", "Trình khởi chạy"
};
static const char *const default_fallback[VQEAF_DEFAULT_COUNT] = {
    "app:7", "app:10", "app:9", "app:12", "app:0"
};

const char *vqeaf_default_role_name(int role)
{
    if(role < 0 || role >= VQEAF_DEFAULT_COUNT) return "Không xác định";
    return default_names[role];
}

const char *vqeaf_default_selector(int role)
{
    if(role < 0 || role >= VQEAF_DEFAULT_COUNT) return "";
    return vqeaf_settings_get_str(default_keys[role], default_fallback[role]);
}

int vqeaf_defaults_set(int role, const char *selector)
{
    if(role < 0 || role >= VQEAF_DEFAULT_COUNT || selector == NULL ||
       selector[0] == '\0') return 0;
    return vqeaf_settings_set_str(default_keys[role], selector) == 0 ? 1 : 0;
}

/* Resolve a stored selector to a human-readable current choice. */
static void default_label(const char *selector, char *out, size_t cap)
{
    int id, index;
    const vqeaf_app_t *app;
    const vqeaf_qeapp_t *qa;

    if(sscanf(selector, "app:%d", &id) == 1) {
        app = vqeaf_app_by_id(id);
        snprintf(out, cap, "%s", app != NULL ? app->name : "Ứng dụng hệ thống");
        return;
    }
    if(sscanf(selector, "qeapp:%d", &index) == 1) {
        qa = vqeaf_qeapp_at(index);
        snprintf(out, cap, "%s", qa != NULL ? qa->name : "QEAPP");
        return;
    }
    snprintf(out, cap, "%s", selector);
}

int vqeaf_defaults_open(int role)
{
    const char *selector = vqeaf_default_selector(role);
    int id = -1, index = -1;
    const vqeaf_app_t *app;

    if(sscanf(selector, "app:%d", &id) == 1) {
        app = vqeaf_app_by_id(id);
        if(app == NULL || app->page == VQEAF_PAGE_COUNT) return 0;
        vqeaf_ui_show(app->page);
        return 1;
    }
    if(sscanf(selector, "qeapp:%d", &index) == 1 &&
       vqeaf_qeapp_at(index) != NULL) {
        vqeaf_qeapp_select(index);
        vqeaf_ui_show(VQEAF_PAGE_QEAPP);
        return 1;
    }
    return 0;
}

/* Which built-in app ids can handle a role. Kept explicit so a role never
 * points at a tile with VQEAF_PAGE_COUNT (no real backend). */
static int default_app_ids(int role, const int **out)
{
    static const int browser[] = { 7 };
    static const int music[] = { 10, 11 };
    static const int gallery[] = { 9, 11 };
    static const int files[] = { 12 };
    static const int home[] = { 0 };
    switch(role) {
    case VQEAF_DEFAULT_BROWSER: *out = browser; return 1;
    case VQEAF_DEFAULT_MUSIC:   *out = music;   return 2;
    case VQEAF_DEFAULT_GALLERY: *out = gallery; return 2;
    case VQEAF_DEFAULT_FILES:   *out = files;   return 1;
    case VQEAF_DEFAULT_HOME:    *out = home;    return 1;
    default: return 0;
    }
}

static int default_role_for_app(int app_id)
{
    for(int role = 0; role < VQEAF_DEFAULT_COUNT; role++) {
        const int *ids = NULL;
        int n = default_app_ids(role, &ids);
        for(int i = 0; i < n; i++)
            if(ids[i] == app_id) return role;
    }
    return -1;
}

/* Chooser card: every real handler plus installed QEAPPs. Each button stores
 * a selector through the Settings service; no fake handler is offered. */
static lv_obj_t *g_default_card;
static char g_default_selectors[24][64];
static int  g_default_roles[24];

static void default_chooser_close_cb(lv_event_t *event)
{
    (void)event;
    if(g_default_card != NULL) {
        lv_obj_delete(g_default_card);
        g_default_card = NULL;
    }
}

static void default_choose_cb(lv_event_t *event)
{
    intptr_t slot = (intptr_t)lv_event_get_user_data(event);
    if(slot < 0 || slot >= 24) return;
    int role = g_default_roles[slot];
    if(role >= 0 && role < VQEAF_DEFAULT_COUNT)
        vqeaf_defaults_set(role, g_default_selectors[slot]);
    default_chooser_close_cb(event);
    vqeaf_ui_show(VQEAF_PAGE_DEFAULT_APPS);
}

static void default_chooser_cb(lv_event_t *event);

static void default_chooser(int role)
{
    lv_display_t *d = lv_display_get_default();
    int width = (int)lv_display_get_horizontal_resolution(d);
    int height = (int)lv_display_get_vertical_resolution(d);
    char buf[96];
    const int *ids = NULL;
    int n, qcount, used = 0;

    if(g_default_card != NULL) default_chooser_close_cb(NULL);
    lv_obj_t *scrim = vqeaf_obj(lv_layer_top());
    lv_obj_set_size(scrim, width, height);
    lv_obj_set_style_bg_color(scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_70, 0);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scrim, default_chooser_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = vqeaf_obj(lv_layer_top());
    g_default_card = card;
    lv_obj_set_size(card, width * 84 / 100, height * 64 / 100);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, VQEAF_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 30, 0);
    lv_obj_set_style_pad_all(card, 22, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    snprintf(buf, sizeof(buf), "%s — chọn ứng dụng",
             vqeaf_default_role_name(role));
    vqeaf_label(card, buf, VQEAF_FONT_TITLE, VQEAF_C_TEXT);

    n = default_app_ids(role, &ids);
    for(int i = 0; i < n && used < 20; i++) {
        const vqeaf_app_t *app = vqeaf_app_by_id(ids[i]);
        if(app == NULL) continue;
        snprintf(g_default_selectors[used], sizeof(g_default_selectors[used]),
                 "app:%d", ids[i]);
        g_default_roles[used] = role;
        lv_obj_t *row = vqeaf_card(card, lv_pct(100), 76);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 14, 0);
        vqeaf_tile(row, app->symbol, lv_color_hex(app->bg),
                   lv_color_hex(app->fg), 52, 16);
        vqeaf_label(row, app->name, VQEAF_FONT_BODY, VQEAF_C_TEXT);
        lv_obj_add_event_cb(row, default_choose_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)used++);
    }

    qcount = vqeaf_qeapp_count();
    for(int i = 0; i < qcount && used < 20; i++) {
        const vqeaf_qeapp_t *qa = vqeaf_qeapp_at(i);
        if(qa == NULL) continue;
        snprintf(g_default_selectors[used], sizeof(g_default_selectors[used]),
                 "qeapp:%d", i);
        g_default_roles[used] = role;
        lv_obj_t *row = vqeaf_card(card, lv_pct(100), 76);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        vqeaf_tile(row, VQEAF_SYM_STOREFRONT, lv_color_hex(0x2A2E6E),
                   VQEAF_C_ACCENT, 52, 16);
        vqeaf_label(row, qa->name, VQEAF_FONT_BODY, VQEAF_C_TEXT);
        lv_obj_add_event_cb(row, default_choose_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)used++);
    }
}

static void default_chooser_cb(lv_event_t *event)
{
    default_chooser((int)(intptr_t)lv_event_get_user_data(event));
}

static void build_default_apps(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);
    page_title(root, "Ứng dụng mặc định");
    vqeaf_label(root,
        "Chọn ứng dụng xử lý tác vụ mở liên kết, nhạc, ảnh, tệp và màn hình "
        "chủ. Lưu trong Settings Service; violet khởi động lại về mặc định.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);

    for(int role = 0; role < VQEAF_DEFAULT_COUNT; role++) {
        char current[64];
        lv_obj_t *row = vqeaf_card(root, lv_pct(100), 104);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 14, 0);
        vqeaf_tile(row, VQEAF_SYM_APPS, lv_color_hex(0x1A3A5A),
                   VQEAF_C_ACCENT, 54, 16);
        lv_obj_t *col = vqeaf_column(row);
        lv_obj_set_flex_grow(col, 1);
        vqeaf_label(col, vqeaf_default_role_name(role),
                    VQEAF_FONT_BODY, VQEAF_C_TEXT);
        default_label(vqeaf_default_selector(role), current, sizeof(current));
        vqeaf_label(col, current, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, default_chooser_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)role);
    }
}

/* ---- System Terminal (Termux-style) ---------------------------------------
 * A dark, scrollable console with a live prompt, command history and a touch
 * keybar (TAB / UP / DOWN / CTRL+C / ENTER). Commands read real framework
 * state, the VFS and live sysfs; no shell process and no fake data.
 */
#define TERM_BUF  8192
#define TERM_HIST 32
static char g_term_out[TERM_BUF];
static char g_term_hist[TERM_HIST][96];
static int  g_term_hist_n;
static int  g_term_hist_pos;
static char g_term_cwd[128] = "/data/qeaf";   /* virtual session cwd */
static lv_obj_t *g_term_view;
static lv_obj_t *g_term_scroll;
static lv_obj_t *g_term_input;
static lv_obj_t *g_term_cwd_label;

static const char *const term_commands[] = {
    "help", "status", "date", "uptime", "battery", "storage", "wifi",
    "apps", "defaults", "default", "theme", "page", "echo", "clear",
    "scan", "gateway", "connect", "rec",
    "pwd", "ls", "cat", "cd", "whoami", "uname", "env", "which",
    "history"
};

static void term_print(const char *line)
{
    size_t used = strlen(g_term_out);
    size_t room = sizeof(g_term_out) - used - 1;
    if(room == 0) return;
    if(used > 0 && g_term_out[used - 1] != '\n') {
        g_term_out[used++] = '\n';
        room--;
    }
    strncat(g_term_out, line, room);
}

static void term_printf(const char *fmt, ...)
{
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    term_print(buf);
}

static void term_sync(void)
{
    if(g_term_view != NULL) lv_label_set_text(g_term_view, g_term_out);
    if(g_term_scroll != NULL)
        lv_obj_scroll_to_y(g_term_scroll,
                           lv_obj_get_scroll_bottom(g_term_scroll), LV_ANIM_OFF);
    if(g_term_cwd_label != NULL) lv_label_set_text(g_term_cwd_label, g_term_cwd);
}

static void term_help(void)
{
    term_print("Available commands:");
    term_print("  help status date uptime battery storage wifi apps");
    term_print("  defaults default theme page echo clear");
    term_print("  pwd ls cat cd whoami uname env which history");
    term_print("Keys: TAB complete  UP/DOWN history  CTRL+C cancel  ENTER run");
}

static void term_status(void)
{
    char up[32];
    format_uptime(vqeaf_state.uptime_s, up, sizeof(up));
    term_printf("device   violet / QEAFIVELS OS (stock 4.14 kernel)");
    term_printf("clock    %s  %s", vqeaf_state.clock_text, vqeaf_state.date_text);
    term_printf("uptime   %s", up);
    if(vqeaf_state.battery_valid)
        term_printf("battery  %d%% %s", vqeaf_state.battery_pct,
                    vqeaf_state.charging ? "charging" : "discharging");
    else term_print("battery  unavailable");
    if(vqeaf_state.storage_valid)
        term_printf("storage  %d%% used", vqeaf_state.storage_pct);
    else term_print("storage  unavailable");
    term_printf("wifi     driver=%d fw=%d if=%s up=%d",
                vqeaf_state.wifi_driver_loaded,
                vqeaf_state.wifi_firmware_present,
                vqeaf_state.wifi_ifname[0] ? vqeaf_state.wifi_ifname : "-",
                vqeaf_state.wifi_up);
}

static void term_ls(const char *arg)
{
    char path[160];
    DIR *d;
    struct dirent *e;
    int n = 0;

    if(arg == NULL || arg[0] == '\0') snprintf(path, sizeof(path), "%s", g_term_cwd);
    else if(arg[0] == '/') snprintf(path, sizeof(path), "%s", arg);
    else snprintf(path, sizeof(path), "%s/%s", g_term_cwd, arg);

    d = opendir(path);
    if(d == NULL) { term_printf("ls: %s: No such file or directory", path); return; }
    while((e = readdir(d)) != NULL && n < 48) {
        if(e->d_name[0] == '.') continue;
        term_printf("  %s", e->d_name);
        n++;
    }
    closedir(d);
    if(n == 0) term_print("  (empty)");
}

static void term_cat(const char *arg)
{
    char path[160];
    FILE *f;
    char line[160];
    int n = 0;

    if(arg == NULL || arg[0] == '\0') { term_print("cat: missing file"); return; }
    if(arg[0] == '/') snprintf(path, sizeof(path), "%s", arg);
    else snprintf(path, sizeof(path), "%s/%s", g_term_cwd, arg);
    f = fopen(path, "r");
    if(f == NULL) { term_printf("cat: %s: No such file", path); return; }
    while(n < 24 && fgets(line, sizeof(line), f) != NULL) {
        size_t len = strlen(line);
        while(len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        term_print(line);
        n++;
    }
    fclose(f);
}

static void term_which(const char *arg)
{
    if(arg == NULL || arg[0] == '\0') { term_print("which: missing name"); return; }
    for(int i = 0; i < vqeaf_app_count(); i++) {
        const vqeaf_app_t *app = vqeaf_app_by_id(i);
        if(app != NULL && strcasecmp(app->name, arg) == 0) {
            term_printf("%s -> built-in page %d", arg, (int)app->page);
            return;
        }
    }
    term_printf("%s: not found", arg);
}

static void term_run(const char *line)
{
    char cmd[32], arg[128];
    int argc = sscanf(line, "%31s %127[^\\n]", cmd, arg);

    if(argc <= 0) return;
    if(g_term_hist_n < TERM_HIST && line[0] != '\0') {
        snprintf(g_term_hist[g_term_hist_n++], sizeof(g_term_hist[0]), "%s", line);
    } else if(line[0] != '\0') {
        for(int i = 0; i < TERM_HIST - 1; i++)
            snprintf(g_term_hist[i], sizeof(g_term_hist[0]), "%s",
                     g_term_hist[i + 1]);
        snprintf(g_term_hist[TERM_HIST - 1], sizeof(g_term_hist[0]), "%s", line);
    }
    g_term_hist_pos = g_term_hist_n;

    term_printf("> vqeaf@violet:%s$ %s", g_term_cwd, line);
    if(strcmp(cmd, "help") == 0) {
        term_help();
    } else if(strcmp(cmd, "status") == 0) {
        term_status();
    } else if(strcmp(cmd, "date") == 0) {
        term_printf("%s %s", vqeaf_state.clock_text, vqeaf_state.date_text);
    } else if(strcmp(cmd, "uptime") == 0) {
        char up[32];
        format_uptime(vqeaf_state.uptime_s, up, sizeof(up));
        term_printf("up %s", up);
    } else if(strcmp(cmd, "battery") == 0) {
        if(!vqeaf_state.battery_valid) term_print("battery: unavailable");
        else term_printf("battery: %d%% %s", vqeaf_state.battery_pct,
                         vqeaf_state.charging ? "charging" : "discharging");
    } else if(strcmp(cmd, "storage") == 0) {
        if(!vqeaf_state.storage_valid) term_print("storage: unavailable");
        else term_printf("storage: %d%% used", vqeaf_state.storage_pct);
    } else if(strcmp(cmd, "wifi") == 0) {
        term_printf("wifi: driver=%d firmware=%d interface=%s state=%d link=%d",
                    vqeaf_state.wifi_driver_loaded,
                    vqeaf_state.wifi_firmware_present,
                    vqeaf_state.wifi_ifname[0] ? vqeaf_state.wifi_ifname : "-",
                    vqeaf_state.wifi_up, vqeaf_state.wifi_link);
        if(vqeaf_state.wifi_up <= 0)
            term_print("wifi: KHONG co ket noi that (khong bao gia gia)");
    } else if(strcmp(cmd, "scan") == 0 && argc == 1) {
        vqeaf_wifi_net_t nets[VQEAF_WIFI_MAX];
        int n = vqeaf_wifi_scan(nets, VQEAF_WIFI_MAX);
        if(n < 0) {
            term_printf("scan: %s", vqeaf_wifi_error(n));
        } else {
            term_printf("scan: %d mang", n);
            for(int i = 0; i < n; i++)
                term_printf("%2d  %-32s  %-14s  %d%%", i + 1,
                            nets[i].ssid,
                            vqeaf_wifi_enc_name(nets[i].enc),
                            nets[i].signal);
        }
    } else if(strcmp(cmd, "gateway") == 0) {
        if(argc == 2 && strcmp(arg, "static") == 0) {
            vqeaf_settings_set_str("wifi.gateway", "static");
            term_print("gateway: static");
        } else if(argc == 2 && strcmp(arg, "dhcp") == 0) {
            vqeaf_settings_set_str("wifi.gateway", "dhcp");
            term_print("gateway: dhcp (can udhcpc)");
        } else {
            term_printf("gateway: %s (tap de doi trong Settings)",
                        wifi_gateway_mode());
        }
    } else if(strcmp(cmd, "connect") == 0 && argc >= 2) {
        /* Secured networks must be joined from Settings so the password is
         * entered through the IME and never appears in terminal history. */
        int rc = vqeaf_wifi_connect(arg, "");
        term_printf("connect %s: %s", arg,
                    rc == 0 ? "OK" : vqeaf_wifi_error(rc));
    } else if(strcmp(cmd, "rec") == 0) {
        if(argc == 2 && strcmp(arg, "start") == 0) {
            term_printf("rec: %s", vio_rec_start(8, 10) == 0 ?
                        "started (REC0001.PNG...)" : "refused");
        } else if(argc == 2 && strcmp(arg, "stop") == 0) {
            term_printf("rec: %s", vio_rec_stop() == 0 ?
                        "stop requested" : "not recording");
        } else {
            term_printf("rec: %s", vio_rec_status());
            term_print("usage: rec start | rec stop | rec status");
        }
    } else if(strcmp(cmd, "apps") == 0) {
        term_printf("apps: %d dang ky (built-in + QEAPP)", vqeaf_app_count());
    } else if(strcmp(cmd, "defaults") == 0) {
        for(int role = 0; role < VQEAF_DEFAULT_COUNT; role++)
            term_printf("%-14s -> %s", vqeaf_default_role_name(role),
                        vqeaf_default_selector(role));
    } else if(strcmp(cmd, "default") == 0 && argc == 2) {
        int role = atoi(arg);
        term_printf("default: %s -> %s", vqeaf_default_role_name(role),
                    vqeaf_default_selector(role));
        if(!vqeaf_defaults_open(role)) term_print("default: khong co handler that");
    } else if(strcmp(cmd, "theme") == 0 && argc == 2) {
        if(strcmp(arg, "dark") == 0) {
            vqeaf_ui_set_theme(VQEAF_THEME_DARK); term_print("theme: dark");
        } else if(strcmp(arg, "light") == 0) {
            vqeaf_ui_set_theme(VQEAF_THEME_LIGHT); term_print("theme: light");
        } else term_print("theme: dung dark hoac light");
    } else if(strcmp(cmd, "page") == 0 && argc == 2) {
        vqeaf_page_t p = VQEAF_PAGE_COUNT;
        if(strcmp(arg, "home") == 0) p = VQEAF_PAGE_HOME;
        else if(strcmp(arg, "apps") == 0) p = VQEAF_PAGE_APPS;
        else if(strcmp(arg, "settings") == 0) p = VQEAF_PAGE_SETTINGS;
        else if(strcmp(arg, "files") == 0) p = VQEAF_PAGE_FILES;
        else if(strcmp(arg, "terminal") == 0) p = VQEAF_PAGE_TERMINAL;
        else if(strcmp(arg, "ntfs") == 0) p = VQEAF_PAGE_NTFS_SEARCH;
        else if(strcmp(arg, "tasks") == 0) p = VQEAF_PAGE_TASK_MANAGER;
        else if(strcmp(arg, "cleanup") == 0) p = VQEAF_PAGE_DISK_CLEANUP;
        if(p != VQEAF_PAGE_COUNT) { term_printf("page: %s", arg); vqeaf_ui_show(p); }
        else term_print("page: ten khong hop le");
    } else if(strcmp(cmd, "echo") == 0 && argc == 2) {
        term_print(arg);
    } else if(strcmp(cmd, "clear") == 0) {
        g_term_out[0] = '\0'; term_sync(); return;
    } else if(strcmp(cmd, "pwd") == 0) {
        term_print(g_term_cwd);
    } else if(strcmp(cmd, "ls") == 0) {
        term_ls(argc == 2 ? arg : NULL);
    } else if(strcmp(cmd, "cat") == 0) {
        term_cat(argc == 2 ? arg : NULL);
    } else if(strcmp(cmd, "cd") == 0 && argc == 2) {
        if(strcmp(arg, "..") == 0) {
            char *slash = strrchr(g_term_cwd, '/');
            if(slash != NULL && slash != g_term_cwd) *slash = '\0';
        } else snprintf(g_term_cwd, sizeof(g_term_cwd), "%s/%s", g_term_cwd, arg);
        term_sync();
    } else if(strcmp(cmd, "whoami") == 0) {
        term_print("vqeaf");
    } else if(strcmp(cmd, "uname") == 0) {
#if defined(__linux__)
        struct utsname u;
        if(uname(&u) == 0) term_printf("%s %s %s", u.sysname, u.release, u.machine);
        else term_print("uname: failed");
#else
        /* The host render has no kernel to query; never invent one. */
        term_print("uname: host render (no device kernel)");
#endif
    } else if(strcmp(cmd, "env") == 0) {
        term_print("USER=vqeaf");
        term_print("HOME=/data/qeaf");
        term_print("TERM=qeaf-256color");
        term_print("SHELL=/system/bin/qeaf-term (in-process)");
    } else if(strcmp(cmd, "which") == 0) {
        term_which(argc == 2 ? arg : NULL);
    } else if(strcmp(cmd, "history") == 0) {
        for(int i = 0; i < g_term_hist_n; i++)
            term_printf("%3d  %s", i + 1, g_term_hist[i]);
    } else {
        term_printf("%s: command not found (go 'help')", cmd);
    }
    term_sync();
}

void vqeaf_terminal_run(const char *line) { term_run(line); }

static void term_submit(void)
{
    if(g_term_input == NULL) return;
    char line[128];
    snprintf(line, sizeof(line), "%s", lv_textarea_get_text(g_term_input));
    lv_textarea_set_text(g_term_input, "");
    vqeaf_ime_hide();
    term_run(line);
}

static void term_key_cb(lv_event_t *event)
{
    int id = (int)(intptr_t)lv_event_get_user_data(event);
    if(g_term_input == NULL) return;
    if(id == 0) {                       /* TAB: first matching command */
        const char *cur = lv_textarea_get_text(g_term_input);
        for(unsigned i = 0; i < sizeof(term_commands)/sizeof(term_commands[0]); i++) {
            if(strncmp(cur, term_commands[i], strlen(cur)) == 0) {
                lv_textarea_set_text(g_term_input, term_commands[i]);
                break;
            }
        }
    } else if(id == 1) {                /* UP: older history */
        if(g_term_hist_pos > 0) {
            g_term_hist_pos--;
            lv_textarea_set_text(g_term_input, g_term_hist[g_term_hist_pos]);
        }
    } else if(id == 2) {                /* DOWN: newer history */
        if(g_term_hist_pos < g_term_hist_n) {
            g_term_hist_pos++;
            lv_textarea_set_text(g_term_input,
                g_term_hist_pos == g_term_hist_n ? "" :
                g_term_hist[g_term_hist_pos]);
        }
    } else if(id == 3) {                /* CTRL+C */
        term_print("^C");
        lv_textarea_set_text(g_term_input, "");
        term_sync();
    } else if(id == 4) {                /* ENTER */
        term_submit();
    }
}

static void term_ready_cb(lv_event_t *event)
{
    (void)event;
    term_submit();
}

static lv_obj_t *term_key(lv_obj_t *parent, const char *label, int id)
{
    lv_obj_t *btn = vqeaf_obj(parent);
    lv_obj_set_size(btn, 120, 68);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1C2B2A), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x2E5F55), 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *l = vqeaf_label(btn, label, VQEAF_FONT_CAPTION,
                              lv_color_hex(0x8ED3));
    lv_obj_center(l);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, term_key_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)id);
    return btn;
}

static void build_terminal(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);
    lv_obj_set_height(root, lv_pct(100));   /* full console height, not content-sized */
    lv_obj_set_style_pad_all(root, 16, 0);
    lv_obj_set_style_pad_row(root, 10, 0);

    /* Termux-like full dark console, independent of the OS wallpaper/theme. */
    lv_obj_set_style_bg_color(root, lv_color_hex(0x0B1514), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    lv_obj_t *head = vqeaf_row(root);
    vqeaf_label(head, "termux@violet", VQEAF_FONT_TITLE,
                lv_color_hex(0x8ED3));
    vqeaf_label(head, "  ~ /data/qeaf", VQEAF_FONT_CAPTION,
                lv_color_hex(0x5E8F86));

    g_term_scroll = vqeaf_obj(root);
    lv_obj_set_width(g_term_scroll, lv_pct(100));
    lv_obj_set_flex_grow(g_term_scroll, 1);
    lv_obj_add_flag(g_term_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(g_term_scroll, LV_DIR_VER);
    lv_obj_set_style_bg_color(g_term_scroll, lv_color_hex(0x0B1514), 0);
    lv_obj_set_style_bg_opa(g_term_scroll, LV_OPA_COVER, 0);

    g_term_view = vqeaf_label(g_term_scroll, "", VQEAF_FONT_CAPTION,
                              lv_color_hex(0xEAF7F5));
    lv_obj_set_width(g_term_view, lv_pct(100));
    lv_label_set_long_mode(g_term_view, LV_LABEL_LONG_WRAP);

    g_term_cwd_label = vqeaf_label(root, g_term_cwd, VQEAF_FONT_CAPTION,
                                   lv_color_hex(0x5E8F86));

    g_term_input = lv_textarea_create(root);
    lv_obj_remove_style_all(g_term_input);
    lv_obj_set_width(g_term_input, lv_pct(100));
    lv_obj_set_height(g_term_input, 84);
    lv_obj_set_style_pad_all(g_term_input, 12, 0);
    lv_obj_set_style_bg_color(g_term_input, lv_color_hex(0x12211F), 0);
    lv_obj_set_style_bg_opa(g_term_input, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(g_term_input, VQEAF_FONT_BODY, 0);
    lv_obj_set_style_text_color(g_term_input, lv_color_hex(0x8ED3), 0);
    lv_obj_set_style_border_width(g_term_input, 1, 0);
    lv_obj_set_style_border_color(g_term_input, lv_color_hex(0x2E5F55), 0);
    lv_obj_set_style_radius(g_term_input, 10, 0);
    lv_textarea_set_placeholder_text(g_term_input, "command...");
    lv_textarea_set_one_line(g_term_input, true);
    lv_obj_add_event_cb(g_term_input, ime_focus_cb, LV_EVENT_FOCUSED, content);
    lv_obj_add_event_cb(g_term_input, term_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(g_term_input, ime_defocus_cb, LV_EVENT_DEFOCUSED, NULL);

    lv_obj_t *keys = vqeaf_obj(root);
    lv_obj_set_width(keys, lv_pct(100));
    lv_obj_set_flex_flow(keys, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(keys, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(keys, 8, 0);
    term_key(keys, "TAB", 0);
    term_key(keys, "UP", 1);
    term_key(keys, "DOWN", 2);
    term_key(keys, "CTRL+C", 3);
    term_key(keys, "ENTER", 4);

    if(g_term_out[0] == '\0')
        snprintf(g_term_out, sizeof(g_term_out),
                 "QEAFIVELS OS Terminal - live framework state only.\n"
                 "Type 'help' and press ENTER. TAB completes, UP/DOWN recall.");
    term_sync();
}

/* ---- entry --------------------------------------------------------------- */

void vqeaf_pages_build(vqeaf_page_t page, lv_obj_t *content)
{
    /* Dismiss any open IME before the page is rebuilt so its window never
     * outlives the field that requested it. */
    vqeaf_ime_hide();
    page_clock = NULL;
    page_date = NULL;
    live_uptime = NULL;
    live_mem_pair = NULL;
    live_storage_pct = NULL;
    live_storage_pair = NULL;
    live_storage_bar = NULL;
    live_battery = NULL;
    live_battery_pct = NULL;
    live_usb = NULL;
    live_wifi = NULL;
    live_brightness = NULL;

    switch(page) {
    case VQEAF_PAGE_LOCK:     build_lock(content); break;
    case VQEAF_PAGE_HOME:     build_home(content); break;
    case VQEAF_PAGE_APPS:     build_apps(content); break;
    case VQEAF_PAGE_CONTROL:  build_control(content); break;
    case VQEAF_PAGE_SETTINGS: build_settings(content); break;
    case VQEAF_PAGE_STORAGE:  build_storage(content); break;
    case VQEAF_PAGE_ABOUT:    build_about(content); break;
    case VQEAF_PAGE_KEYBOARD: build_keyboard(content); break;
    case VQEAF_PAGE_KEYBOARD_FEEDBACK: build_keyboard_feedback(content); break;
    case VQEAF_PAGE_KEYBOARD_THEMES: build_keyboard_themes(content); break;
    case VQEAF_PAGE_WIFI:     build_wifi(content); break;
    case VQEAF_PAGE_DRIVERS:  build_drivers(content); break;
    case VQEAF_PAGE_SYNC:     build_sync(content); break;
    case VQEAF_PAGE_LOCK_THEME: build_lock_theme(content); break;
    case VQEAF_PAGE_M3:      build_m3_layout(content); break;
    case VQEAF_PAGE_APPS_EDIT: build_apps_edit(content); break;
    case VQEAF_PAGE_QEAPP_STORE: build_qeapp_store(content); break;
    case VQEAF_PAGE_SECURITY: build_security(content); break;
    case VQEAF_PAGE_PIN:     build_pin(content); break;
    case VQEAF_PAGE_DEFAULT_APPS: build_default_apps(content); break;
    case VQEAF_PAGE_TERMINAL: build_terminal(content); break;
    case VQEAF_PAGE_NTFS_SEARCH: vqeaf_tools_build_ntfs_search(content); break;
    case VQEAF_PAGE_TASK_MANAGER: vqeaf_tools_build_task_manager(content); break;
    case VQEAF_PAGE_DISK_CLEANUP: vqeaf_tools_build_disk_cleanup(content); break;
    case VQEAF_PAGE_FILES:    build_files(content); break;
    case VQEAF_PAGE_QEAPP:    build_qeapp(content); break;
    case VQEAF_PAGE_QEAPP_NATIVE: build_qeapp_native(content); break;
    case VQEAF_PAGE_BROWSER:  build_browser(content); break;
    case VQEAF_PAGE_NOTES:    build_notes(content); break;
    case VQEAF_PAGE_MUSIC:    build_music(content); break;
    case VQEAF_PAGE_VIDEO:    build_video(content); break;
    case VQEAF_PAGE_GALLERY:  build_gallery(content); break;
    case VQEAF_PAGE_INSTALLER: build_installer(content); break;
    case VQEAF_PAGE_RECENTS:  build_recents(content); break;
    default: break;
    }
    refresh_live_fields();
}


