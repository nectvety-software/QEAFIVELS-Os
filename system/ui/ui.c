#include "include/vqeaf/ui.h"
#include "include/vqeaf/gfx.h"
#include "include/vqeaf/screen_common.h"
#include "include/vqeaf/recovery_screen.h"
#include "include/vqeaf/update_screen.h"
#include "include/vqeaf/phone_shell.h"

typedef enum { UI_SCREEN_PHONE = 0, UI_SCREEN_FASTBOOT } UiScreen;

static VqeafFramebuffer ui_fb;
static uint64_t ui_free_pages;
static uint32_t ui_task_count;
static int ui_ready;
static int ui_blanked;
static UiScreen ui_screen;
static uint32_t ui_fb_selected;
static uint32_t ui_pmenu_selected;
static int ui_pmenu_open;
static VqeafUpdateView ui_update;
static int ui_update_active;
static VqeafPhoneState ui_phone;
static VqeafPhoneModel ui_model;
static char ui_storage_path[16];
static char ui_storage_file[24];
static char ui_storage_summary[48];
static char ui_platform_device[32] = "VQEAF Reference";
static char ui_platform_model[32] = "x86_64 UEFI";
static char ui_platform_detail[32] = "QEMU x86_64";
static char ui_platform_build[32] = "reference";
static char ui_platform_footer[48];

static void append(char *dst, uint32_t *n, uint32_t cap, const char *src) {
    while (*src && *n + 1u < cap) dst[(*n)++] = *src++;
    dst[*n] = 0;
}
static void copy_clean(char *dst, uint32_t cap, const char *src) {
    uint32_t n = 0;
    if (!src) src = "";
    for (uint32_t i = 0; src[i] && n + 1u < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\r' || c == '\n' || c == '\t') {
            if (n && dst[n - 1u] != ' ') dst[n++] = ' ';
        } else if (c >= 32u && c < 127u) {
            dst[n++] = (char)c;
        }
    }
    while (n && dst[n - 1u] == ' ') n--;
    dst[n] = 0;
}

void vqeaf_ui_set_platform(const char *device, const char *model,
                           const char *detail, const char *build,
                           const char *footer) {
    copy_clean(ui_platform_device, sizeof ui_platform_device, device);
    copy_clean(ui_platform_model, sizeof ui_platform_model, model);
    copy_clean(ui_platform_detail, sizeof ui_platform_detail, detail);
    copy_clean(ui_platform_build, sizeof ui_platform_build, build);
    copy_clean(ui_platform_footer, sizeof ui_platform_footer, footer);
}

int vqeaf_ui_init(uint64_t framebuffer_base, uint32_t width, uint32_t height,
                  uint32_t stride, uint32_t format, uint64_t free_pages,
                  uint32_t task_count) {
    if (vqeaf_gfx_setup(&ui_fb, framebuffer_base, width, height, stride, format) != 0) {
        ui_ready = 0;
        return -1;
    }
    ui_free_pages = free_pages;
    ui_task_count = task_count;
    ui_ready = 1;
    ui_blanked = 0;
    ui_screen = UI_SCREEN_PHONE;
    ui_update_active = 0;
    ui_pmenu_open = 0;
    VqeafScreenFrame f = vqeaf_screen_frame(&ui_fb);
    vqeaf_phone_init(&ui_phone, f.w >= 600u ? 4u : f.w >= 360u ? 3u : 2u);
    ui_model.timeout_index = 1u;
    vqeaf_ui_render(0);
    return 0;
}
int vqeaf_ui_ready(void) { return ui_ready; }
void vqeaf_ui_set_blanked(int blanked) { ui_blanked = blanked ? 1 : 0; }
int vqeaf_ui_update_active(void) { return ui_update_active; }

void vqeaf_ui_set_clock(int valid, uint32_t year, uint32_t month, uint32_t day,
                        uint32_t hour, uint32_t minute, uint32_t second) {
    if (!valid || year < 2000u || year > 2100u || month < 1u || month > 12u ||
        day < 1u || day > 31u || hour > 23u || minute > 59u || second > 59u) {
        ui_model.clock_valid = 0;
        return;
    }
    ui_model.clock_valid = 1;
    ui_model.year = year;
    ui_model.month = month;
    ui_model.day = day;
    ui_model.hour = hour;
    ui_model.minute = minute;
    ui_model.second = second;
}

void vqeaf_ui_set_power_status(uint32_t timeout_index, int keep_awake,
                               int battery_present, uint32_t permille,
                               int charging) {
    ui_model.timeout_index = timeout_index < 5u ? timeout_index : 4u;
    ui_model.keep_awake = keep_awake ? 1 : 0;
    ui_model.battery_present = battery_present ? 1 : 0;
    ui_model.battery_permille = permille > 1000u ? 1000u : permille;
    ui_model.charging = charging ? 1 : 0;
}

void vqeaf_ui_set_storage_status(int mounted, uint64_t sectors, const char *path,
                                 const char *file, const char *summary) {
    ui_model.storage_mounted = mounted ? 1 : 0;
    ui_model.storage_sectors = sectors;
    copy_clean(ui_storage_path, sizeof ui_storage_path, path);
    copy_clean(ui_storage_file, sizeof ui_storage_file, file);
    copy_clean(ui_storage_summary, sizeof ui_storage_summary, summary);
    ui_model.storage_path = ui_storage_path;
    ui_model.storage_file = ui_storage_file;
    ui_model.storage_summary = ui_storage_summary;
}

void vqeaf_ui_update_show_view(const VqeafUpdateView *view) {
    if (!view) return;
    ui_update = *view;
    if (ui_update.percent > 100u) ui_update.percent = 100u;
    if (ui_update.stage > VQEAF_UPDATE_STAGE_COUNT)
        ui_update.stage = VQEAF_UPDATE_STAGE_COUNT;
    ui_update_active = 1;
    ui_blanked = 0;
}
void vqeaf_ui_update_show(uint32_t percent, const char *stage, int failed) {
    VqeafUpdateView view;
    view.percent = percent;
    view.stage = percent >= 100u ? VQEAF_UPDATE_STAGE_COUNT : 0u;
    view.detail = stage;
    view.failed = failed ? 1 : 0;
    view.available = 1;
    vqeaf_ui_update_show_view(&view);
}
void vqeaf_ui_update_hide(void) { ui_update_active = 0; }

void vqeaf_ui_show_home(void) {
    ui_screen = UI_SCREEN_PHONE;
    ui_phone.screen = VQEAF_PHONE_HOME;
    ui_phone.sel = 0;
    ui_update_active = 0;
    ui_pmenu_open = 0;
    ui_blanked = 0;
}
void vqeaf_ui_show_fastboot(void) {
    ui_screen = UI_SCREEN_FASTBOOT;
    ui_update_active = 0;
    ui_pmenu_open = 0;
    ui_blanked = 0;
    ui_fb_selected = 0;
}
int vqeaf_ui_fastboot_active(void) {
    return ui_screen == UI_SCREEN_FASTBOOT && !ui_update_active && !ui_blanked;
}
void vqeaf_ui_fastboot_nav(int delta) {
    int sel = (int)ui_fb_selected + delta;
    if (sel < 0) sel = 0;
    if (sel >= (int)VQEAF_FASTBOOT_ITEM_COUNT) sel = (int)VQEAF_FASTBOOT_ITEM_COUNT - 1;
    ui_fb_selected = (uint32_t)sel;
}
uint32_t vqeaf_ui_fastboot_selected(void) { return ui_fb_selected; }
void vqeaf_ui_set_power_menu(int open, uint32_t selected) {
    ui_pmenu_open = open ? 1 : 0;
    ui_pmenu_selected = selected;
}

int vqeaf_ui_phone_key(VqeafPhoneKey key) {
    if (!ui_ready) return -1;
    if (ui_update_active) {
        if (key == VQEAF_PHONE_KEY_ENTER || key == VQEAF_PHONE_KEY_BACK) {
            ui_update_active = 0;
            return VQEAF_PHONE_ACTION_UPDATE_CLOSE;
        }
        return VQEAF_PHONE_ACTION_NONE;
    }
    if (ui_screen != UI_SCREEN_PHONE) return VQEAF_PHONE_ACTION_NONE;
    VqeafPhoneAction action = vqeaf_phone_key(&ui_phone, key);
    if (action == VQEAF_PHONE_ACTION_FASTBOOT) vqeaf_ui_show_fastboot();
    return action;
}

int vqeaf_ui_phone_screen(void) {
    if (!ui_ready || ui_screen != UI_SCREEN_PHONE) return -1;
    if (ui_update_active || ui_blanked) return -1;
    return (int)ui_phone.screen;
}

int vqeaf_ui_tap(uint32_t x, uint32_t y) {
    if (!ui_ready) return -1;
    if (ui_blanked) return VQEAF_PHONE_ACTION_NONE;
    if (ui_update_active) {
        ui_update_active = 0;
        return VQEAF_PHONE_ACTION_UPDATE_CLOSE;
    }
    if (ui_pmenu_open) {
        /* Same geometry as power_menu_overlay: a tap on a row activates it,
         * a tap outside the card dismisses the menu. */
        VqeafScreenFrame f = vqeaf_screen_frame(&ui_fb);
        uint32_t s = f.scale;
        uint32_t w = 210u * s;
        if (w > f.w - 24u * s) w = f.w - 24u * s;
        uint32_t row = 34u * s;
        uint32_t h = 32u * s + 4u * (row + 4u * s) + 10u * s;
        if (h > ui_fb.height) h = ui_fb.height;
        uint32_t mx = f.x + (f.w - w) / 2u;
        uint32_t my = ui_fb.height > h ? (ui_fb.height - h) / 2u : 0u;
        if (x < mx || x >= mx + w || y < my || y >= my + h) {
            ui_pmenu_open = 0;
            return VQEAF_PHONE_ACTION_NONE;
        }
        uint32_t ry = my + 32u * s;
        for (uint32_t i = 0; i < 4u; i++) {
            if (y >= ry && y < ry + row) {
                ui_pmenu_open = 0;
                ui_pmenu_selected = i;
                switch (i) {
                case 0: return VQEAF_PHONE_ACTION_POWER_OFF;
                case 1: return VQEAF_PHONE_ACTION_REBOOT;
                case 2: return VQEAF_PHONE_ACTION_FASTBOOT;
                default: return VQEAF_PHONE_ACTION_SLEEP;
                }
            }
            ry += row + 4u * s;
        }
        return VQEAF_PHONE_ACTION_NONE;
    }
    if (ui_screen == UI_SCREEN_FASTBOOT) {
        /* The fastboot view is informational on a touch-only port: none of
         * its items have a working backend here, so any tap returns home.
         * Rebooting to the bootloader stays an explicit power-menu action. */
        vqeaf_ui_show_home();
        return VQEAF_PHONE_ACTION_NONE;
    }
    VqeafPhoneAction action = vqeaf_phone_tap(&ui_phone, ui_fb.width, ui_fb.height,
                                              x, y);
    if (action == VQEAF_PHONE_ACTION_FASTBOOT) vqeaf_ui_show_fastboot();
    return action;
}

static void power_menu_overlay(const VqeafFramebuffer *fb, const VqeafPalette *p) {
    static const char *const labels[4] = {
        "T\u1EAET NGU\u1ED3N", "KH\u1EDEI \u0110\u1ED8NG L\u1EA0I", "FASTBOOT",
        "NG\u1EE6"
    };
    static const VqeafIcon icons[4] = {VQEAF_ICON_POWER, VQEAF_ICON_REBOOT,
                                      VQEAF_ICON_BOLT, VQEAF_ICON_SLEEP};
    VqeafScreenFrame f = vqeaf_screen_frame(fb);
    uint32_t s = f.scale;
    uint32_t w = 210u * s;
    if (w > f.w - 24u * s) w = f.w - 24u * s;
    uint32_t row = 34u * s;
    uint32_t h = 32u * s + 4u * (row + 4u * s) + 10u * s;
    if (h > fb->height) h = fb->height;
    uint32_t x = f.x + (f.w - w) / 2u;
    uint32_t y = fb->height > h ? (fb->height - h) / 2u : 0u;
    vqeaf_gfx_rect(fb, x, y, w, h, p->panel_hi);
    vqeaf_gfx_border(fb, x, y, w, h, 2u, p->accent);
    vqeaf_gfx_text(fb, x + 14u * s, y + 8u * s, "NGU\u1ED2N", 1u, p->accent);
    uint32_t ry = y + 32u * s;
    for (uint32_t i = 0; i < 4u; i++) {
        int sel = (int)i == (int)ui_pmenu_selected;
        vqeaf_gfx_rect(fb, x + 8u * s, ry, w - 16u * s, row, sel ? p->panel : p->bar);
        if (sel) vqeaf_gfx_border(fb, x + 8u * s, ry, w - 16u * s, row, 2u, p->accent);
        vqeaf_screen_icon(fb, x + 16u * s, ry + (row - 20u * s) / 2u, 20u * s,
                          icons[i], sel ? p->accent : p->dim);
        vqeaf_gfx_text(fb, x + 40u * s, ry + (row - 16u) / 2u, labels[i], 1u,
                       sel ? p->text : p->dim);
        ry += row + 4u * s;
    }
}

void vqeaf_ui_render(uint64_t pit_ticks) {
    if (!ui_ready) return;
    const VqeafFramebuffer *fb = &ui_fb;
    if (ui_blanked) {
        vqeaf_gfx_clear(fb, vqeaf_gfx_rgb(fb, 0x000000));
        return;
    }
    if (ui_update_active) {
        vqeaf_update_screen_render_view(fb, &ui_update);
        return;
    }
    if (ui_screen == UI_SCREEN_FASTBOOT) {
        VqeafDeviceInfo info;
        static char version[24];
        uint32_t n = 0;
        version[0] = 0;
        append(version, &n, sizeof version, "v0.3.0");
        info.device = ui_platform_device;
        info.model = ui_platform_model;
        info.version = version;
        info.build = ui_platform_build;
        info.detail = ui_platform_detail;
        info.footer = ui_platform_footer[0] ? ui_platform_footer : 0;
        vqeaf_fastboot_screen_render(fb, &info, ui_fb_selected, 0);
        return;
    }
    ui_model.uptime_ticks = pit_ticks;
    ui_model.free_pages = ui_free_pages;
    ui_model.task_count = ui_task_count;
    ui_model.fb_width = fb->width;
    ui_model.fb_height = fb->height;
    vqeaf_phone_render(fb, &ui_phone, &ui_model);
    if (ui_pmenu_open) {
        VqeafPalette p = vqeaf_screen_palette(fb);
        power_menu_overlay(fb, &p);
    }
}
