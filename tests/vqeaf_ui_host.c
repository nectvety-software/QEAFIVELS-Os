/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_ui_host.c - render the real VQEAF phone shell on the desktop.
 *
 * This harness compiles the same vqeaf_ui / vqeaf_pages / vqeaf_theme sources
 * as the violet initramfs, but replaces vqeaf_state.c with a fixed provider
 * set so layout can be inspected without a phone. Every page is drawn into an
 * XRGB8888 buffer and dumped as PPM; scripts/render_phone_ui.py builds it,
 * converts the PPMs to PNG and gates on non-blank, per-page-distinct output.
 *
 * It is not part of the initramfs and never reads host hardware state.
 * Platform helpers (backlight, monotonic clock, logging) come from
 * vqeaf_host_platform.h so the shell can enter real sleep on the host too.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "vqeaf_state.h"
#include "vqeaf_ui.h"
#include "vqeaf_theme.h"
#include "vqeaf_ime.h"
#include "vqeaf_pages.h"
#include "vqeaf_components.h"
#include "vqeaf_qeapp_device.h"
#include "vqeaf_host_platform.h"

vqeaf_state_t vqeaf_state = {
    .clock_text = "--:--",
    .date_text = "Chưa đặt thời gian",
    .wifi_up = -1,
    .brightness = 85,
};

/* The harness owns every value; nothing is read from the host. */
void vqeaf_state_refresh(void)
{
}

int vqeaf_state_set_brightness(int percent)
{
    if(percent < 5) percent = 5;
    if(percent > 100) percent = 100;
    vqeaf_state.brightness = percent;
    return 0;
}

/* The desktop harness has no vibrator; the IME asks through these and must
 * see an honest "unavailable" instead of a fabricated pulse. */
int vio_haptics_available(void) { return 0; }
int vio_haptic_pulse(unsigned duration_ms) { (void)duration_ms; return 0; }

/* ---- capture and camera stubs (Linux-only device code) --------------------
 *
 * The shell offers screenshot, screen recording and the camera census through
 * vio_* and vqeaf_camera_*, which encode DRM scanout and talk to V4L2. The
 * host renderer has neither, so every call reports "not available" and the
 * pages draw their honest empty state instead of a fabricated capture.
 */
int vio_rec_start(int fps, int seconds)
{
    (void)fps;
    (void)seconds;
    return -1;
}

int vio_rec_stop(void)
{
    return -1;
}

int vio_rec_active(void)
{
    return 0;
}

const char *vio_rec_status(void)
{
    return "host renderer: no DRM scanout";
}

int vio_screenshot_now(void)
{
    return -1;
}

int vqeaf_camera_query_caps(char *buf, int cap)
{
    if(buf != NULL && cap > 0)
        snprintf(buf, (size_t)cap, "host renderer: no V4L2 nodes");
    return -1;
}

int vqeaf_camera_create_session(int32_t *session_handle, char *err, int err_cap)
{
    if(session_handle != NULL) *session_handle = -1;
    if(err != NULL && err_cap > 0)
        snprintf(err, (size_t)err_cap, "host renderer: no cam-req-mgr");
    return -1;
}

int vqeaf_camera_probe(char *buf, int cap)
{
    if(buf != NULL && cap > 0)
        snprintf(buf, (size_t)cap, "host renderer: no V4L2 nodes");
    return -1;
}

/* ---- vqeaf_qeapp_device.c stubs (Linux-only device runtime) ----------------
 *
 * The real runtime forks a child and maps native modules; that cannot exist
 * on the host render harness. The stubs report "never ran", so the QEAPP
 * native page renders its honest empty state instead of a fabricated frame.
 */
#include "vqeaf_qeapp_device.h"

int vqeaf_qeapp_device_session_start(const uint8_t *package, size_t size,
                                     uint32_t effective_permissions,
                                     char *error, size_t error_cap)
{
    (void)package; (void)size; (void)effective_permissions;
    if(error != NULL && error_cap > 0) error[0] = '\0';
    return 1;
}

int vqeaf_qeapp_device_session_send(const vqeaf_qeapp_device_input_t *event)
{
    (void)event;
    return -1;
}

int vqeaf_qeapp_device_session_poll(char *error, size_t error_cap)
{
    if(error != NULL && error_cap > 0) error[0] = '\0';
    return 0;
}

void vqeaf_qeapp_device_session_stop(void) {}
int vqeaf_qeapp_device_session_active(void) { return 0; }
uint32_t vqeaf_qeapp_device_session_frame_seq(void) { return 0u; }

int vqeaf_qeapp_device_launch(const uint8_t *package, size_t size,
                              uint32_t effective_permissions,
                              char *report, size_t report_cap,
                              char *error, size_t error_cap)
{
    (void)package;
    (void)size;
    (void)effective_permissions;
    if(report != NULL && report_cap > 0) report[0] = '\0';
    if(error != NULL && error_cap > 0)
        snprintf(error, error_cap, "native execution requires the device.");
    return -1;
}

/* A stand-in for the canvas a native module draws: a blank top strip (so the
 * shell's status bar may overlay it, as on a phone) and a filled body. The
 * device supplies the real frame; this only exists so the QEAPP_NATIVE page
 * can be laid out and looked at on the desktop. */
const uint32_t *vqeaf_qeapp_device_frame(void)
{
    static uint32_t canvas[VQEAF_QEAPP_DEVICE_W * VQEAF_QEAPP_DEVICE_HEIGHT];
    static int built;
    if(!built) {
        int x, y;
        for(y = 0; y < VQEAF_QEAPP_DEVICE_HEIGHT; y++) {
            for(x = 0; x < VQEAF_QEAPP_DEVICE_W; x++) {
                uint32_t pixel = 0x00000000u;
                if(y > 260 && y < 1240 && x > 60 && x < 660) {
                    /* Text-like lines on a shallow gradient, so the render has
                     * the colour spread of a real app frame. */
                    int dy = y - 260;
                    int line = (dy % 60) < 7 && x > 100 && x < 620;
                    pixel = line ? 0x00DDE9E6u
                                 : (0x00102422u + (uint32_t)((dy / 4) & 63) *
                                                     0x00010101u);
                }
                canvas[y * VQEAF_QEAPP_DEVICE_W + x] = pixel;
            }
        }
        built = 1;
    }
    return canvas;
}

const char *vqeaf_qeapp_device_lifecycle(void)
{
    return "";
}

const char *vqeaf_qeapp_device_process(void)
{
    return "";
}

uint32_t vqeaf_qeapp_device_hash(void)
{
    return 0u;
}

int vqeaf_qeapp_device_page_pending(void)
{
    return 0;
}

void vqeaf_qeapp_device_page_ack(void)
{
}


static uint32_t *framebuffer;
static lv_display_t *display;
static uint32_t host_tick;

static void flush_cb(lv_display_t *disp, const lv_area_t *area,
                     uint8_t *px_map)
{
    (void)area;
    (void)px_map; /* DIRECT mode draws straight into framebuffer. */
    lv_display_flush_ready(disp);
}

static uint32_t tick_cb(void)
{
    return host_tick;
}

/* Advance the LVGL clock so animations (the IME slide-up) reach their end
 * state before a frame is captured. */
static void host_step(int ms)
{
    int t;
    for(t = 0; t < ms; t += 10) { host_tick += 10; lv_timer_handler(); }
}

/* First textarea in the tree, so the IME can bind the real Notes field. */
static lv_obj_t *find_textarea(lv_obj_t *obj)
{
    uint32_t i, n;
    if(obj == NULL) return NULL;
    if(lv_obj_check_type(obj, &lv_textarea_class)) return obj;
    n = lv_obj_get_child_count(obj);
    for(i = 0; i < n; i++) {
        lv_obj_t *r = find_textarea(lv_obj_get_child(obj, i));
        if(r != NULL) return r;
    }
    return NULL;
}

static lv_obj_t *find_label(lv_obj_t *obj, const char *text)
{
    uint32_t i, n;
    if(obj == NULL) return NULL;
    if(lv_obj_check_type(obj, &lv_label_class) &&
       strcmp(lv_label_get_text(obj), text) == 0)
        return obj;
    n = lv_obj_get_child_count(obj);
    for(i = 0; i < n; i++) {
        lv_obj_t *hit = find_label(lv_obj_get_child(obj, i), text);
        if(hit != NULL) return hit;
    }
    return NULL;
}

static void scroll_to_top(lv_obj_t *obj)
{
    lv_obj_t *scroller = lv_obj_get_parent(obj);
    lv_area_t area, viewport;

    lv_obj_scroll_to_view_recursive(obj, LV_ANIM_OFF);
    while(scroller != NULL && !lv_obj_has_flag(scroller, LV_OBJ_FLAG_SCROLLABLE))
        scroller = lv_obj_get_parent(scroller);
    if(scroller == NULL) return;
    lv_obj_update_layout(lv_screen_active());
    lv_obj_get_coords(obj, &area);
    lv_obj_get_coords(scroller, &viewport);
    lv_obj_scroll_by(scroller, 0,
                     viewport.y1 + lv_obj_get_style_pad_top(scroller, 0) + 8 - area.y1,
                     LV_ANIM_OFF);
}

/* Two profiles: a handset whose sysfs providers are absent, and one that
 * reports every value the shell can show. */
static void configure_providers(int with_hardware)
{
    memset(&vqeaf_state, 0, sizeof(vqeaf_state));
    vqeaf_state.brightness = 85;
    vqeaf_state.storage_valid = 1;
    vqeaf_state.storage_total = 27ull * 1024ull * 1024ull * 1024ull;
    vqeaf_state.storage_free = 15ull * 1024ull * 1024ull * 1024ull;
    vqeaf_state.storage_pct = 44;
    vqeaf_state.mem_valid = 1;
    vqeaf_state.mem_total_kb = 5u * 1024u * 1024u;
    vqeaf_state.mem_available_kb = 2u * 1024u * 1024u;
    vqeaf_state.uptime_s = 3u * 3600u + 12u * 60u;
    vqeaf_state.drm_present = 1;
    vqeaf_state.touch_events = 2;
    vqeaf_state.usb_udc_present = 1;
    vqeaf_state.backlight_present = 1;
    vqeaf_state.usb_lun_present = 1;
    vqeaf_state.usb_lun_size = 32ull * 1024ull * 1024ull;
    vqeaf_state.wifi_driver_loaded = 1;

    if(!with_hardware) {
        strcpy(vqeaf_state.clock_text, "--:--");
        strcpy(vqeaf_state.date_text, "Chưa đặt thời gian");
        vqeaf_state.wifi_up = -1;
        vqeaf_state.battery_valid = 0;
        return;
    }
    strcpy(vqeaf_state.clock_text, "09:41");
    strcpy(vqeaf_state.date_text, "Thứ 3, 10 Tháng 11, 2026");
    vqeaf_state.time_valid = 1;
    vqeaf_state.hour = 9;
    vqeaf_state.minute = 41;
    vqeaf_state.wifi_up = 1;
    vqeaf_state.battery_valid = 1;
    vqeaf_state.battery_pct = 76;
    vqeaf_state.charging = 1;
    vqeaf_state.wifi_if_present = 1;
    strcpy(vqeaf_state.wifi_ifname, "wlan0");
    strcpy(vqeaf_state.wifi_mac, "9c:5c:f9:12:34:56");
    vqeaf_state.wifi_up = 1;
    vqeaf_state.wifi_link = 62;
    vqeaf_state.wifi_firmware_present = 1;
    strcpy(vqeaf_state.wifi_firmware_path, "/vendor/firmware");
}

#include "vqeaf_host_sync.h"

static int write_ppm(const char *path, int width, int height)
{
    FILE *file = fopen(path, "wb");
    int x, y;

    if(file == NULL) return -1;
    fprintf(file, "P6\n%d %d\n255\n", width, height);
    for(y = 0; y < height; y++) {
        const uint32_t *row = framebuffer + (size_t)y * (size_t)width;
        for(x = 0; x < width; x++) {
            uint32_t pixel = row[x];
            unsigned char rgb[3];
            rgb[0] = (unsigned char)((pixel >> 16) & 0xFFu);
            rgb[1] = (unsigned char)((pixel >> 8) & 0xFFu);
            rgb[2] = (unsigned char)(pixel & 0xFFu);
            if(fwrite(rgb, 1, sizeof(rgb), file) != sizeof(rgb)) {
                fclose(file);
                return -1;
            }
        }
    }
    return fclose(file) == 0 ? 0 : -1;
}

/* Host-only installer overlay hooks (vqeaf_pages.c, #ifndef __linux__). */
void vqeaf_install_render(int state);
void vqeaf_install_render_close(void);

int main(int argc, char **argv)
{
    static const struct {
        vqeaf_page_t page;
        const char *name;
    } pages[] = {
        { VQEAF_PAGE_LOCK, "lock" },
        { VQEAF_PAGE_HOME, "home" },
        { VQEAF_PAGE_HOME, "home-page-2" },
        { VQEAF_PAGE_APPS, "apps" },
        { VQEAF_PAGE_APPS_EDIT, "apps-edit" },
        { VQEAF_PAGE_APPS_EDIT, "apps-edit-page-2" },
        { VQEAF_PAGE_CONTROL, "control" },
        { VQEAF_PAGE_SETTINGS, "settings" },
        { VQEAF_PAGE_STORAGE, "storage" },
        { VQEAF_PAGE_ABOUT, "about" },
        { VQEAF_PAGE_KEYBOARD, "keyboard" },
        { VQEAF_PAGE_KEYBOARD_THEMES, "keyboard-themes" },
        { VQEAF_PAGE_WIFI, "wifi" },
        { VQEAF_PAGE_DRIVERS, "drivers" },
        { VQEAF_PAGE_SYNC, "sync" },
        { VQEAF_PAGE_LOCK_THEME, "lock-theme" },
        { VQEAF_PAGE_FILES, "files" },
        { VQEAF_PAGE_INSTALLER, "installer" },
        { VQEAF_PAGE_QEAPP, "qeapp" },
        { VQEAF_PAGE_QEAPP_NATIVE, "qeapp-native" },
        { VQEAF_PAGE_VIDEO, "video" },
        { VQEAF_PAGE_MUSIC, "music" },
        { VQEAF_PAGE_NTFS_SEARCH, "ntfs-search" },
        { VQEAF_PAGE_TASK_MANAGER, "task-manager" },
        { VQEAF_PAGE_DISK_CLEANUP, "disk-cleanup" },
        { VQEAF_PAGE_TERMINAL, "terminal" },
        { VQEAF_PAGE_SECURITY, "security" },
        { VQEAF_PAGE_WIFI_AUDIT, "wifi-audit" },
        { VQEAF_PAGE_ADVISORIES, "advisories" },
        { VQEAF_PAGE_M3, "m3" },
    };
    const char *out_dir = argc > 1 ? argv[1] : "build/screens/phone-ui";
    const char *profile = "unknown";
    int width = 720;
    int height = 1560;
    int with_hardware = 0;
    size_t i;
    int rc = 0;

    for(i = 2; i + 1 < (size_t)argc; i += 2) {
        if(strcmp(argv[i], "--width") == 0)
            width = atoi(argv[i + 1]);
        else if(strcmp(argv[i], "--height") == 0)
            height = atoi(argv[i + 1]);
        else if(strcmp(argv[i], "--profile") == 0) {
            profile = argv[i + 1];
            with_hardware = strcmp(profile, "providers") == 0;
        }
    }
    if(width <= 0 || height <= 0) {
        fprintf(stderr, "vqeaf-ui-host: bad canvas %dx%d\n", width, height);
        return 2;
    }

    framebuffer = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
    if(framebuffer == NULL) return 2;

    configure_providers(with_hardware);
    lv_init();
    lv_tick_set_cb(tick_cb);
    display = lv_display_create(width, height);
    if(display == NULL) {
        fprintf(stderr, "vqeaf-ui-host: LVGL display failed\n");
        free(framebuffer);
        return 2;
    }
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display, framebuffer, NULL,
                           (uint32_t)(sizeof(uint32_t) * (size_t)width *
                                      (size_t)height),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, flush_cb);
    lv_display_set_default(display);

    {
        const char *glass = getenv("VQEAF_LIQUID_GLASS");
        if(glass != NULL && glass[0] != '\0' && strcmp(glass, "0") != 0)
            vqeaf_theme_set_liquid_glass(1);
    }

    if(vqeaf_setup_complete() < 0) {
        fprintf(stderr, "vqeaf-ui-host: cannot complete setup for normal pages\n");
        free(framebuffer);
        return 2;
    }
    vqeaf_ui_create(display);
    vqeaf_ui_set_sync_list(host_sync_list);
    vqeaf_ui_set_sync_read(host_sync_read);

    for(i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
        char path[512];
        size_t bytes = (size_t)width * (size_t)height * sizeof(uint32_t);

        memset(framebuffer, 0, bytes);
        vqeaf_pages_home_page_set(strstr(pages[i].name, "page-2") != NULL);
        vqeaf_ui_show(pages[i].page);
        host_step(600);
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);

        snprintf(path, sizeof(path), "%s/%s-%dx%d-%s.ppm", out_dir,
                 pages[i].name, width, height, profile);
        if(write_ppm(path, width, height) != 0) {
            fprintf(stderr, "vqeaf-ui-host: cannot write %s\n", path);
            rc = 1;
            break;
        }
        printf("%s\n", path);
    }

    vqeaf_pages_home_page_set(0);

    {
        char path[512];
        size_t bytes = (size_t)width * (size_t)height * sizeof(uint32_t);
        lv_obj_t *label;
        lv_obj_t *select;

        memset(framebuffer, 0, bytes);
        vqeaf_ui_show(VQEAF_PAGE_M3);
        host_step(600);
        label = find_label(lv_screen_active(),
                           "Liquid Glass · nút, thẻ và chất liệu");
        if(label == NULL) {
            fprintf(stderr, "vqeaf-ui-host: Liquid Glass showcase not found\n");
            return 1;
        }
        scroll_to_top(label);
        host_step(200);
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);
        snprintf(path, sizeof(path), "%s/m3-liquid-%dx%d-%s.ppm",
                 out_dir, width, height, profile);
        if(write_ppm(path, width, height) != 0) rc = 1;
        else printf("%s\n", path);

        label = find_label(lv_screen_active(), "Select · một bề mặt lỏng");
        if(label == NULL) {
            fprintf(stderr, "vqeaf-ui-host: Liquid Glass select section not found\n");
            return 1;
        }
        scroll_to_top(label);
        host_step(200);
        label = find_label(lv_screen_active(), "Tự động");
        select = label != NULL ? lv_obj_get_parent(label) : NULL;
        if(select == NULL) {
            fprintf(stderr, "vqeaf-ui-host: Liquid Glass select not found\n");
            return 1;
        }
        lv_obj_send_event(select, LV_EVENT_CLICKED, NULL);
        host_step(300);
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);
        snprintf(path, sizeof(path), "%s/m3-liquid-overlay-%dx%d-%s.ppm",
                 out_dir, width, height, profile);
        if(write_ppm(path, width, height) != 0) rc = 1;
        else printf("%s\n", path);
        vqeaf_lg_overlay_close(select);
        host_step(20);

        label = find_label(lv_screen_active(),
                           "Button Group · nhiều nút, một thân kính");
        if(label == NULL) {
            fprintf(stderr, "vqeaf-ui-host: Liquid Glass actions not found\n");
            return 1;
        }
        scroll_to_top(label);
        host_step(200);
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);
        snprintf(path, sizeof(path), "%s/m3-liquid-actions-%dx%d-%s.ppm",
                 out_dir, width, height, profile);
        if(write_ppm(path, width, height) != 0) rc = 1;
        else printf("%s\n", path);
    }

    /* Search and open a real advisory through the card callback, rather than
     * exposing a test-only selected-record setter. */
    {
        char path[512];
        size_t bytes = (size_t)width * (size_t)height * sizeof(uint32_t);
        lv_obj_t *label;
        lv_obj_t *card;

        memset(framebuffer, 0, bytes);
        vqeaf_pages_advisory_query_set("eternalblue");
        vqeaf_ui_show(VQEAF_PAGE_ADVISORIES);
        host_step(600);
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);
        snprintf(path, sizeof(path), "%s/advisories-search-%dx%d-%s.ppm",
                 out_dir, width, height, profile);
        if(write_ppm(path, width, height) != 0) rc = 1;
        else printf("%s\n", path);

        label = find_label(lv_screen_active(), "CVE-2017-0144");
        card = label != NULL ? lv_obj_get_parent(lv_obj_get_parent(label)) : NULL;
        if(card == NULL) {
            fprintf(stderr, "vqeaf-ui-host: EternalBlue advisory not found\n");
            rc = 1;
        }
        else {
            lv_obj_send_event(card, LV_EVENT_CLICKED, NULL);
            host_step(600);
            if(vqeaf_ui_page() != VQEAF_PAGE_ADVISORY_DETAIL) {
                fprintf(stderr, "vqeaf-ui-host: advisory card did not open\n");
                rc = 1;
            }
        }
        memset(framebuffer, 0, bytes);
        host_step(20);
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);
        snprintf(path, sizeof(path), "%s/advisory-detail-%dx%d-%s.ppm",
                 out_dir, width, height, profile);
        if(write_ppm(path, width, height) != 0) rc = 1;
        else printf("%s\n", path);
        vqeaf_pages_advisory_query_set("");
    }

    /* Control Center media visibility (host render only): inject the same
     * session contract a real playback backend uses, then clear it on stop. */
    {
        char path[512];
        size_t bytes = (size_t)width * (size_t)height * sizeof(uint32_t);
        lv_obj_t *label;
        lv_obj_t *card;

        memset(framebuffer, 0, bytes);
        vqeaf_ui_media_session_stop();
        vqeaf_ui_show(VQEAF_PAGE_CONTROL);
        host_step(600);
        vqeaf_ui_media_session_start(VQEAF_UI_MEDIA_AUDIO,
            "Bài hát kiểm thử", "WAV · 44,1 kHz");
        host_step(600);
        label = find_label(lv_screen_active(), "Bài hát kiểm thử");
        card = label != NULL ? lv_obj_get_parent(lv_obj_get_parent(label)) : NULL;
        if(card != NULL)
            lv_obj_scroll_to_view_recursive(card, LV_ANIM_OFF);
        host_step(600);
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);

        snprintf(path, sizeof(path), "%s/control-media-audio-%dx%d-%s.ppm",
                 out_dir, width, height, profile);
        if(write_ppm(path, width, height) != 0) {
            fprintf(stderr, "vqeaf-ui-host: cannot write %s\n", path);
            rc = 1;
        }
        else printf("%s\n", path);
        vqeaf_ui_media_session_stop();
        host_step(20);
    }

    /* Windows 11 (Fluent) skin: the pages the skin changes, rendered with the
     * skin on, so the Start layout, acrylic surfaces and taskbar can be
     * inspected on the desktop next to their stock counterparts. */
    {
        static const struct {
            vqeaf_page_t page;
            const char *name;
        } fluent_states[] = {
            { VQEAF_PAGE_LOCK, "lock-fluent" },
            { VQEAF_PAGE_HOME, "home-fluent" },
            { VQEAF_PAGE_APPS, "apps-fluent" },
            { VQEAF_PAGE_SETTINGS, "settings-fluent" },
        };

        vqeaf_theme_set_fluent(1);
        for(i = 0; i < sizeof(fluent_states) / sizeof(fluent_states[0]); i++) {
            char path[512];
            size_t bytes = (size_t)width * (size_t)height * sizeof(uint32_t);

            memset(framebuffer, 0, bytes);
            vqeaf_ui_show(fluent_states[i].page);
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(display);

            snprintf(path, sizeof(path), "%s/%s-%dx%d-%s.ppm", out_dir,
                     fluent_states[i].name, width, height, profile);
            if(write_ppm(path, width, height) != 0) {
                fprintf(stderr, "vqeaf-ui-host: cannot write %s\n", path);
                rc = 1;
                break;
            }
            printf("%s\n", path);
        }
        vqeaf_theme_set_fluent(0);
    }

    /* Installer overlay states (host render only). The FILES page sits behind
     * the dimmed modal so confirm/busy/durable/session-only/error cards are
     * seen in context, exactly as they appear over the file picker. */
    {
        static const struct { int state; const char *name; } install_states[] = {
            { 0, "install-confirm" },
            { 1, "install-busy" },
            { 2, "install-done" },
            { 3, "install-error" },
            { 4, "install-session" },
        };
        for(i = 0; i < sizeof(install_states) / sizeof(install_states[0]); i++) {
            char path[512];
            size_t bytes = (size_t)width * (size_t)height * sizeof(uint32_t);

            memset(framebuffer, 0, bytes);
            vqeaf_ui_show(VQEAF_PAGE_FILES);
            vqeaf_install_render(install_states[i].state);
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(display);

            snprintf(path, sizeof(path), "%s/%s-%dx%d-%s.ppm", out_dir,
                     install_states[i].name, width, height, profile);
            if(write_ppm(path, width, height) != 0) {
                fprintf(stderr, "vqeaf-ui-host: cannot write %s\n", path);
                rc = 1;
                break;
            }
            printf("%s\n", path);
            vqeaf_install_render_close();
        }
    }

    /* Bold-text accessibility mode (host render only): the same pages with
     * every VQEAF_FONT_* resolved to the bold face, so the weight system is
     * visible and gateable without a device. */
    {
        static const struct { vqeaf_page_t page; const char *name; } bold_states[] = {
            { VQEAF_PAGE_SETTINGS, "settings-bold" },
            { VQEAF_PAGE_HOME, "home-bold" },
        };
        for(i = 0; i < sizeof(bold_states) / sizeof(bold_states[0]); i++) {
            char path[512];
            size_t bytes = (size_t)width * (size_t)height * sizeof(uint32_t);

            memset(framebuffer, 0, bytes);
            vqeaf_type_set_bold(1);
            vqeaf_ui_show(bold_states[i].page);
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(display);

            snprintf(path, sizeof(path), "%s/%s-%dx%d-%s.ppm", out_dir,
                     bold_states[i].name, width, height, profile);
            if(write_ppm(path, width, height) != 0) {
                fprintf(stderr, "vqeaf-ui-host: cannot write %s\n", path);
                rc = 1;
                break;
            }
            printf("%s\n", path);
            vqeaf_type_set_bold(0);
        }
    }

    /* Independent IME window (host render only): the real Notes page sits
     * behind while the IME shows in each adaptive mode. The slide-up anim is
     * stepped to completion so the window is captured at its final inset. */
    {
        static const struct {
            vqeaf_ime_mode_t mode;
            const char *name;
            int emoji;
        } ime_states[] = {
            { VQEAF_IME_MODE_NORMAL,         "ime-normal",     0 },
            { VQEAF_IME_MODE_SPLIT,          "ime-split",      0 },
            { VQEAF_IME_MODE_ONE_HAND_RIGHT, "ime-one-hand",   0 },
            { VQEAF_IME_MODE_FLOATING,       "ime-floating",   0 },
            { VQEAF_IME_MODE_EXTRACT,        "ime-extract",    0 },
            { VQEAF_IME_MODE_NORMAL,         "ime-emoji",      1 },
        };
        for(i = 0; i < sizeof(ime_states) / sizeof(ime_states[0]); i++) {
            char path[512];
            size_t bytes = (size_t)width * (size_t)height * sizeof(uint32_t);
            lv_obj_t *ta;

            memset(framebuffer, 0, bytes);
            vqeaf_ui_show(VQEAF_PAGE_NOTES);
            ta = find_textarea(lv_screen_active());
            vqeaf_ime_set_mode(ime_states[i].mode);
            vqeaf_ime_show(ta, NULL);
            host_step(400);
            if(ime_states[i].emoji) {
                lv_obj_t *grid = vqeaf_ime_grid();
                lv_obj_t *toggle = lv_obj_get_child(lv_obj_get_child(grid, 3), 1);
                lv_obj_send_event(toggle, LV_EVENT_CLICKED, NULL);
                host_step(50);
                grid = vqeaf_ime_grid();
                lv_obj_t *emoji = lv_obj_get_child(lv_obj_get_child(grid, 0), 0);
                lv_obj_send_event(emoji, LV_EVENT_CLICKED, NULL);
                host_step(50);
            }
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(display);

            snprintf(path, sizeof(path), "%s/%s-%dx%d-%s.ppm", out_dir,
                     ime_states[i].name, width, height, profile);
            if(write_ppm(path, width, height) != 0) {
                fprintf(stderr, "vqeaf-ui-host: cannot write %s\n", path);
                rc = 1;
                break;
            }
            printf("%s\n", path);
            vqeaf_ime_hide();
            host_step(400);
        }
    }

    /* Music app surfaces (host render only): the canvas lyric overlay and the
     * queue, alongside the plain player rendered by the page map above. */
    {
        static const struct { int view; const char *name; } music_states[] = {
            { 1, "music-canvas" },
            { 2, "music-queue" },
        };
        for(i = 0; i < sizeof(music_states) / sizeof(music_states[0]); i++) {
            char path[512];
            size_t bytes = (size_t)width * (size_t)height * sizeof(uint32_t);

            memset(framebuffer, 0, bytes);
            vqeaf_pages_music_view_set(music_states[i].view);
            vqeaf_ui_show(VQEAF_PAGE_MUSIC);
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(display);

            snprintf(path, sizeof(path), "%s/%s-%dx%d-%s.ppm", out_dir,
                     music_states[i].name, width, height, profile);
            if(write_ppm(path, width, height) != 0) {
                fprintf(stderr, "vqeaf-ui-host: cannot write %s\n", path);
                rc = 1;
                break;
            }
            printf("%s\n", path);
            vqeaf_pages_music_view_set(0);
        }
    }

    {
        static const char *const setup_states[] = {
            "setup-welcome", "setup-security", "setup-network",
            "setup-appearance", "setup-ready"
        };
        vqeaf_setup_reset();
        for(i = 0; i < sizeof(setup_states) / sizeof(setup_states[0]); i++) {
            char path[512];
            size_t bytes = (size_t)width * (size_t)height * sizeof(uint32_t);

            memset(framebuffer, 0, bytes);
            vqeaf_setup_show_step((int)i);
            host_step(600);
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(display);
            snprintf(path, sizeof(path), "%s/%s-%dx%d-%s.ppm", out_dir,
                     setup_states[i], width, height, profile);
            if(write_ppm(path, width, height) != 0) {
                fprintf(stderr, "vqeaf-ui-host: cannot write %s\n", path);
                rc = 1;
                break;
            }
            printf("%s\n", path);
        }
        vqeaf_setup_complete();
    }

    free(framebuffer);
    return rc;
}
