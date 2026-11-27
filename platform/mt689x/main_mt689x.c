/* SPDX-License-Identifier: MIT */
/* platform/mt689x/main_mt689x.c - QEAF PID 1 on MT6891/MT6893 (chopin/ares).
 *
 * One process owns mounts, /dev, the rescue LUN, DRM scanout and the shell,
 * exactly like platform/violet/main_violet.c. Two things differ:
 *
 *  - the hardware layer is MediaTek (UFS, MT6893 connectivity, SCP sensors,
 *    1080x2400 panel), so mt689x_bringup.c does the mounts/modules and
 *    mt689x_caps.c publishes the honest census;
 *  - a Denial Wayland compositor can replace the QEAF shell when the image
 *    ships one and /etc/qeaf/session selects it. When it is absent or exits,
 *    the QEAF shell is started, so there is always a UI.
 *
 * PID 1 must not exit: faults are recovered at a setjmp point and the rescue
 * volume keeps answering QEAF.CMD.
 */
#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libs/lodepng/lodepng.h>

#include "lvgl.h"
#include "drm_kms.h"
#include "panel_scale.h"
#include "splash.h"
#include "vio_display.h"
#include "usb_lun.h"
#include "qeaf_version.h"
#include "vqeaf_ui.h"
#include "vqeaf_state.h"
#include "vqeaf_shutdown.h"
#include "violet_input.h"
#include "violet_power.h"
#include "violet.h"
#include "violet_drivers.h"
#include "qvf_encode.h"
#include "volume_lock.h"

#include "hal/include/display.h"
#include "hal/include/usb_gadget.h"
#include "fs/rescue.h"

#include "mt689x.h"
#include "mt689x_bringup.h"
#include "mt689x_caps.h"
#include "mt689x_session.h"

static uint32_t ui_buffer[MTK_UI_PORTRAIT_W * MTK_UI_PORTRAIT_H];
static int ui_w = MTK_UI_PORTRAIT_W;
static int ui_h = MTK_UI_PORTRAIT_H;
static int ui_orientation = -1;
static lv_display_t *ui_display;
static struct vio_touch touch;
static struct vio_power power_btn;
static vio_panel_t panel;
static struct vio_drm drm;
static struct vio_lun lun;
static int drm_ready;
static volatile sig_atomic_t ui_ready;
static VioDriverEvidence driver_evidence;

static pthread_t rescue_thread;
static volatile sig_atomic_t rescue_thread_up;
static const char *volatile boot_stage = "start";
static MtkBringup bringup;

static sigjmp_buf main_recovery;
static sigjmp_buf ui_recovery;
static volatile sig_atomic_t in_ui;
static pthread_t main_thread;
static int usb_cable_seen;

static struct {
    uint16_t pixels[RESCUE_SCREEN_W * RESCUE_SCREEN_H];
    char trailer[RESCUE_SCREEN_TRAILER];
} snap_image;
static unsigned snap_frame;

static char boot_stage_code(void)
{
    const char *stage = (const char *)boot_stage;

    if(stage == NULL) return '?';
    if(strcmp(stage, "start") == 0) return '0';
    if(strcmp(stage, "lun") == 0) return 'l';
    if(strcmp(stage, "dri-wait") == 0) return 'w';
    if(strcmp(stage, "backlight") == 0) return 'b';
    if(strcmp(stage, "drm-init") == 0) return 'd';
    if(strcmp(stage, "splash") == 0) return 's';
    if(strcmp(stage, "phone-ui") == 0) return 'p';
    if(strcmp(stage, "loop") == 0) return 'o';
    return '?';
}

static uint32_t snap_fnv1a(const uint8_t *data, size_t len)
{
    uint32_t hash = 2166136261u;
    for(size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static int snapshot_scanout(struct fat16_volume *vol)
{
    int x, y;

    if(!drm_ready || drm.map == NULL) return -1;
    for(y = 0; y < RESCUE_SCREEN_H; y++) {
        int sy = (int)(((int64_t)y * drm.height) / RESCUE_SCREEN_H);
        const uint32_t *row = (const uint32_t *)(drm.map +
                                                 (size_t)sy * drm.pitch);
        for(x = 0; x < RESCUE_SCREEN_W; x++) {
            int sx = (int)(((int64_t)x * drm.width) / RESCUE_SCREEN_W);
            uint32_t p = row[sx];
            uint16_t r = (uint16_t)((p >> 19) & 0x1F);
            uint16_t g = (uint16_t)((p >> 10) & 0x3F);
            uint16_t b = (uint16_t)((p >> 3) & 0x1F);
            snap_image.pixels[y * RESCUE_SCREEN_W + x] =
                (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
    snap_frame++;
    {
        char text[RESCUE_SCREEN_TRAILER];
        int used = snprintf(text, sizeof(text), "frame=%u fnv=%08x st=%c\n",
                            (unsigned)snap_frame,
                            (unsigned)snap_fnv1a(
                                (const uint8_t *)snap_image.pixels,
                                sizeof(snap_image.pixels)),
                            boot_stage_code());
        memset(snap_image.trailer, ' ', sizeof(snap_image.trailer));
        if(used > 0) {
            if(used > (int)sizeof(snap_image.trailer))
                used = (int)sizeof(snap_image.trailer);
            memcpy(snap_image.trailer, text, (size_t)used);
        }
    }
    if(fat16_write_file(vol, "SCREEN.RAW", (const uint8_t *)&snap_image,
                        sizeof(snap_image)) < 0)
        return -2;
    vio_log("snap: wrote SCREEN.RAW (%dx%d RGB565, frame %u)",
            RESCUE_SCREEN_W, RESCUE_SCREEN_H, (unsigned)snap_frame);
    vqeaf_ui_notify_event("Cứu hộ", "Đã ghi ảnh màn hình vào SCREEN.RAW",
                          VQEAF_SYM_IMAGE, 0x35E0C8,
                          VQEAF_SOUND_CAPTURE);
    return (int)sizeof(snap_image);
}

static unsigned shot_frame;
static char g_last_shot[64];

static int capture_write(struct fat16_volume *vol, const char *dir,
                         const char *name, const unsigned char *data,
                         size_t len)
{
    char path[64];

    snprintf(path, sizeof(path), "%s%s", dir, name);
    return fat16_write_path(vol, path, data, len);
}

static int screenshot_png(struct fat16_volume *vol, char *name, size_t cap)
{
    unsigned char *rgba;
    unsigned char *png = NULL;
    size_t png_len = 0;
    unsigned w, h, x, y;
    unsigned lode_err;
    int rc = -1;

    if(vol == NULL || name == NULL || cap == 0 ||
       !drm_ready || drm.map == NULL) return -1;
    w = drm.width;
    h = drm.height;
    if(w == 0 || h == 0 || (size_t)w > SIZE_MAX / 4u / (size_t)h) return -3;
    rgba = malloc((size_t)w * (size_t)h * 4u);
    if(rgba == NULL) return -2;

    for(y = 0; y < h; y++) {
        const uint32_t *row = (const uint32_t *)(drm.map +
                                                (size_t)y * drm.pitch);
        unsigned char *out = rgba + (size_t)y * w * 4u;
        for(x = 0; x < w; x++) {
            uint32_t p = row[x];
            out[x * 4u + 0u] = (unsigned char)((p >> 16) & 0xff);
            out[x * 4u + 1u] = (unsigned char)((p >> 8) & 0xff);
            out[x * 4u + 2u] = (unsigned char)(p & 0xff);
            out[x * 4u + 3u] = 255;
        }
    }

    lode_err = lodepng_encode32(&png, &png_len, rgba, w, h);
    if(lode_err != 0 || png == NULL || png_len == 0) {
        size_t stride = ((size_t)w * 3u + 3u) & ~(size_t)3u;
        size_t bmp_len;
        unsigned char *bmp;
        unsigned char *pixels;

        free(rgba);
        if(stride > SIZE_MAX - 54u || (size_t)h > (SIZE_MAX - 54u) / stride)
            return -3;
        bmp_len = 54u + stride * (size_t)h;
        bmp = malloc(bmp_len);
        if(bmp == NULL) return -3;
        memset(bmp, 0, 54);
        bmp[0] = 'B'; bmp[1] = 'M';
        bmp[2] = (unsigned char)(bmp_len & 0xff);
        bmp[3] = (unsigned char)((bmp_len >> 8) & 0xff);
        bmp[4] = (unsigned char)((bmp_len >> 16) & 0xff);
        bmp[5] = (unsigned char)((bmp_len >> 24) & 0xff);
        bmp[10] = 54;
        bmp[14] = 40;
        bmp[18] = (unsigned char)(w & 0xff);
        bmp[19] = (unsigned char)((w >> 8) & 0xff);
        bmp[22] = (unsigned char)(h & 0xff);
        bmp[23] = (unsigned char)((h >> 8) & 0xff);
        bmp[26] = 1; bmp[28] = 24;
        bmp[34] = (unsigned char)(bmp_len & 0xff);
        bmp[35] = (unsigned char)((bmp_len >> 8) & 0xff);
        bmp[36] = (unsigned char)((bmp_len >> 16) & 0xff);
        bmp[37] = (unsigned char)((bmp_len >> 24) & 0xff);
        pixels = bmp + 54;
        for(y = 0; y < h; y++) {
            const uint32_t *row = (const uint32_t *)(drm.map +
                (size_t)(h - 1u - y) * drm.pitch);
            unsigned char *out = pixels + (size_t)y * stride;
            for(x = 0; x < w; x++) {
                uint32_t c = row[x];
                out[x * 3u + 0u] = (unsigned char)(c & 0xff);
                out[x * 3u + 1u] = (unsigned char)((c >> 8) & 0xff);
                out[x * 3u + 2u] = (unsigned char)((c >> 16) & 0xff);
            }
        }
        snprintf(name, cap, "SHOT%04u.BMP", ++shot_frame);
        snprintf(g_last_shot, sizeof(g_last_shot), "%s", name);
        rc = capture_write(vol, "UserData/Pictures/", name, bmp, bmp_len) < 0 ?
             -4 : (int)bmp_len;
        free(bmp);
        if(rc > 0) {
            vio_log("shot: PNG encoder error %u; wrote BMP %s (%d bytes)",
                    lode_err, name, rc);
            vqeaf_ui_notify_event("Ảnh màn hình", name, VQEAF_SYM_IMAGE, 0x35E0C8,
                                  VQEAF_SOUND_CAPTURE);
            vqeaf_ui_show_capture_thumb(name);
        }
        return rc;
    }
    free(rgba);

    snprintf(name, cap, "SHOT%04u.PNG", ++shot_frame);
    snprintf(g_last_shot, sizeof(g_last_shot), "%s", name);
    if(capture_write(vol, "UserData/Pictures/", name, png, png_len) < 0)
        rc = -4;
    else
        rc = (int)png_len;
    free(png);

    if(rc > 0) {
        vio_log("shot: %s (%ux%u PNG, %d bytes)", name, w, h, rc);
        vqeaf_ui_notify_event("Ảnh màn hình", name, VQEAF_SYM_IMAGE, 0x35E0C8,
                                  VQEAF_SOUND_CAPTURE);
        vqeaf_ui_show_capture_thumb(name);
    }
    return rc;
}

const char *vio_last_screenshot(void) { return g_last_shot; }

int vio_screenshot_now(void)
{
    char name[64];
    int rc;

    if(!drm_ready || drm.map == NULL) return -1;
    volume_lock_acquire();
    rc = lun.ready ? screenshot_png(&lun.volume, name, sizeof(name)) : -1;
    if(rc > 0) {
        snprintf(g_last_shot, sizeof(g_last_shot), "%s", name);
        rescue_flush_log(&lun.volume);
    }
    volume_lock_release();
    if(rc <= 0)
        vqeaf_ui_notify_event("Ảnh màn hình", "Chụp không thành công",
                              VQEAF_SYM_DELETE, 0xFF8A7A,
                              VQEAF_SOUND_WARNING);
    return rc;
}

#define REC_W 240u
#define REC_H 416u
#define REC_BUDGET (6u * 1024u * 1024u)
#define REC_MAX_SECONDS 60
#define REC_DIR "UserData/Videos/"

static uint8_t *g_rec_buf;
static size_t g_rec_cap, g_rec_used;
static uint32_t *g_rec_xrgb;
static unsigned g_rec_frames, g_rec_limit;
static unsigned g_rec_interval_ms, g_rec_next_ms;
static int g_rec_active, g_rec_fps;
static unsigned g_rec_file;
static char g_rec_status[128] = "idle";
static char g_rec_name[24];

static size_t rec_budget(void)
{
    size_t free_bytes = 0, budget = REC_BUDGET;

    volume_lock_acquire();
    free_bytes = lun.ready ? fat16_free_bytes(&lun.volume) : 0;
    volume_lock_release();
    if(free_bytes / 2u < budget) budget = free_bytes / 2u;
    if(budget < (size_t)REC_W * REC_H * 2u) return 0;
    return budget;
}

static int rec_capture_frame(void)
{
    unsigned x, y;

    if(!drm_ready || drm.map == NULL) return -1;
    if(g_rec_buf == NULL || g_rec_xrgb == NULL) return -1;
    for(y = 0; y < REC_H; y++) {
        unsigned sy = (y * (unsigned)drm.height) / REC_H;
        const uint32_t *row = (const uint32_t *)(drm.map +
                                                 (size_t)sy * drm.pitch);
        for(x = 0; x < REC_W; x++) {
            unsigned sx = (x * (unsigned)drm.width) / REC_W;
            g_rec_xrgb[y * REC_W + x] = row[sx];
        }
    }
    return vio_qvf_append(g_rec_buf, g_rec_cap, &g_rec_used, g_rec_xrgb,
                          REC_W, REC_H);
}

static void rec_save(const char *reason)
{
    char path[64];
    int rc = -1;

    if(g_rec_frames == 0 || g_rec_buf == NULL || g_rec_used <= VIO_QVF_HEADER) {
        snprintf(g_rec_status, sizeof(g_rec_status),
                 "stopped: no frame captured (%s)", reason);
        vio_log("rec: %s", g_rec_status);
        return;
    }
    vio_qvf_set_frames(g_rec_buf, g_rec_used, g_rec_frames);
    snprintf(g_rec_name, sizeof(g_rec_name), "VID%04u.QVF", ++g_rec_file);
    snprintf(path, sizeof(path), "%s%s", REC_DIR, g_rec_name);
    volume_lock_acquire();
    if(lun.ready)
        rc = fat16_write_path(&lun.volume, path, g_rec_buf, g_rec_used);
    if(rc == 0) rescue_flush_log(&lun.volume);
    volume_lock_release();

    if(rc == 0) {
        snprintf(g_rec_status, sizeof(g_rec_status),
                 "saved %s (%u frames, %u bytes, %s)", g_rec_name,
                 g_rec_frames, (unsigned)g_rec_used, reason);
        vio_log("rec: %s", g_rec_status);
        vqeaf_ui_notify_event("Quay màn hình", g_rec_name, VQEAF_SYM_MOVIE,
                              0xFF7A7A, VQEAF_SOUND_RECORD);
    }
    else {
        snprintf(g_rec_status, sizeof(g_rec_status),
                 "save failed after %u frames (%s)", g_rec_frames, reason);
        vio_log("rec: %s", g_rec_status);
        vqeaf_ui_notify_event("Quay màn hình", "Không ghi được tệp video",
                              VQEAF_SYM_MOVIE, 0xFF8A7A,
                              VQEAF_SOUND_WARNING);
    }
}

static void rec_tick(void)
{
    unsigned now;

    if(!g_rec_active) return;
    now = (unsigned)vio_monotonic_ms();
    if(g_rec_next_ms != 0u && now < g_rec_next_ms) return;
    g_rec_next_ms = now + g_rec_interval_ms;

    if(rec_capture_frame() != 0) {
        g_rec_active = 0;
        rec_save("clip budget reached");
        return;
    }
    g_rec_frames++;
    if(g_rec_frames >= g_rec_limit) {
        g_rec_active = 0;
        rec_save("time limit reached");
        return;
    }
    snprintf(g_rec_status, sizeof(g_rec_status),
             "recording %u/%u frames (%ux%u @%d fps)",
             g_rec_frames, g_rec_limit, REC_W, REC_H, g_rec_fps);
}

int vio_rec_start(int fps, int seconds)
{
    size_t budget;

    if(g_rec_active) return -16;
    if(!drm_ready || drm.map == NULL) return -19;
    if(fps < 1 || fps > 15) fps = 8;
    if(seconds < 1 || seconds > REC_MAX_SECONDS) seconds = 10;

    budget = rec_budget();
    if(budget == 0) {
        snprintf(g_rec_status, sizeof(g_rec_status),
                 "refused: no room on the rescue volume");
        vio_log("rec: %s", g_rec_status);
        return -28;
    }
    if(g_rec_buf == NULL) g_rec_buf = malloc(REC_BUDGET);
    if(g_rec_xrgb == NULL)
        g_rec_xrgb = malloc((size_t)REC_W * REC_H * sizeof(uint32_t));
    if(g_rec_buf == NULL || g_rec_xrgb == NULL) return -12;

    g_rec_cap = budget;
    g_rec_used = vio_qvf_header(g_rec_buf, g_rec_cap, REC_W, REC_H,
                                (uint16_t)fps);
    if(g_rec_used == 0) return -22;
    g_rec_frames = 0;
    g_rec_fps = fps;
    g_rec_limit = (unsigned)fps * (unsigned)seconds;
    g_rec_interval_ms = 1000u / (unsigned)fps;
    g_rec_next_ms = 0;
    g_rec_active = 1;
    snprintf(g_rec_status, sizeof(g_rec_status),
             "recording 0/%u frames (%ux%u @%d fps)", g_rec_limit,
             REC_W, REC_H, fps);
    vio_log("rec: started, %ux%u @%d fps, %u frame(s), %u-byte budget",
            REC_W, REC_H, fps, g_rec_limit, (unsigned)g_rec_cap);
    vqeaf_sound_play(VQEAF_SOUND_RECORD);
    return 0;
}

int vio_rec_stop(void)
{
    if(!g_rec_active) return -1;
    g_rec_active = 0;
    rec_save("stopped by user");
    return 0;
}

int vio_rec_active(void) { return g_rec_active; }

const char *vio_rec_status(void) { return g_rec_status; }

static void status_fields(char *resp, int cap)
{
    char line[192];
    int n = (int)strlen(resp);
    int k;
    vqeaf_shutdown_action_t action = vqeaf_shutdown_action();
    const char *power = action == VQEAF_SHUTDOWN_RESTART ? "restart" :
                        action == VQEAF_SHUTDOWN_POWER_OFF ? "poweroff" : "on";

    k = snprintf(line, sizeof(line),
                 "; stage=%s; ui=%d; page=%d; drm=%d; power=%s; uptime=%us; "
                 "board=%s; fw=%d; mods=%d/%d; denial=%d",
                 (const char *)boot_stage, ui_ready ? 1 : 0,
                 (int)vqeaf_ui_page(), drm_ready, power,
                 (unsigned)(vio_monotonic_ms() / 1000u), mtk_board_name(),
                 bringup.firmware_ready, bringup.modules_loaded,
                 bringup.modules_loaded + bringup.modules_failed,
                 bringup.denial_present);
    if(k < 0 || n + k >= cap) return;
    memcpy(resp + n, line, (size_t)k);
    resp[n + k] = '\0';
}

static void set_stage(const char *stage)
{
    boot_stage = stage;
}

static void *rescue_worker(void *arg)
{
    char logged[24] = "";

    (void)arg;
    rescue_thread_up = 1;
    for(;;) {
        const char *stage = (const char *)boot_stage;

        volume_lock_acquire();
        if(strcmp(stage, logged) != 0) {
            char line[64];
            snprintf(line, sizeof(line),
                     "[    -    ] N qeaf: boot stage %s", stage);
            rescue_note(line);
            rescue_flush_log(&lun.volume);
            snprintf(logged, sizeof(logged), "%s", stage);
        }
        rescue_poll(&lun.volume);
        vio_lun_poll(&lun);
        volume_lock_release();
        vio_sleep_ms(200);
    }
    return NULL;
}

static const char *const splash_lines[] = {
    "[  OK  ] ufs: block device scan",
    "[  OK  ] drm: 1080x2400 DSI panel",
    "[  OK  ] lun: QEAFRESCUE bound",
    NULL,
};

static void splash_present(void)
{
    vio_ui_canvas_size(display_orientation(), &ui_w, &ui_h);
    if(ui_orientation != (int)display_orientation()) {
        ui_orientation = (int)display_orientation();
        vio_panel_set_ui_size(&panel, ui_w, ui_h);
        vio_display_bind(&panel);
        vio_panel_set_rotation(&panel,
                               display_orientation_degrees(display_orientation()));
    }
    vio_splash_render(ui_buffer, ui_w, ui_h,
                      "QEAFIVELS OS", QEAF_OS_VERSION " mt689x", splash_lines);
    if(drm_ready && drm.map) {
        vio_panel_render(&panel, ui_buffer, (uint32_t *)drm.map,
                         vio_panel_full_rect(&panel));
        vio_drm_flush(&drm, 0, 0, drm.width - 1, drm.height - 1);
    }
    vio_log("ui: splash %dx%d (%s)", ui_w, ui_h,
            display_orientation_name(display_orientation()));
}

static uint32_t lv_tick_cb(void)
{
    return (uint32_t)vio_monotonic_ms();
}

static void lv_flush_cb(lv_display_t *display, const lv_area_t *area,
                        uint8_t *pixels)
{
    (void)pixels;
    if(drm_ready && drm.map) {
        if(vio_display_take_redraw()) {
            vio_panel_render(&panel, ui_buffer, (uint32_t *)drm.map,
                             vio_panel_full_rect(&panel));
            vio_drm_flush(&drm, 0, 0, drm.width - 1, drm.height - 1);
        }
        else {
            vio_rect_t dirty = { area->x1, area->y1, area->x2, area->y2 };
            vio_rect_t physical = vio_panel_map_ui_rect(&panel, dirty);
            if(physical.x1 <= physical.x2 && physical.y1 <= physical.y2) {
                vio_panel_render(&panel, ui_buffer, (uint32_t *)drm.map,
                                 physical);
                vio_drm_flush(&drm, physical.x1, physical.y1,
                              physical.x2, physical.y2);
            }
        }
    }
    lv_display_flush_ready(display);
}

static void lv_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if(vqeaf_shutdown_active()) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->continue_reading = 0;
        return;
    }
    vio_touch_poll(&touch);
    if(touch.pressed) vqeaf_ui_activity();
    data->point.x = touch.x;
    data->point.y = touch.y;
    data->state = touch.pressed ? LV_INDEV_STATE_PRESSED :
                                  LV_INDEV_STATE_RELEASED;
    data->continue_reading = 0;
}

static int start_phone_ui(void)
{
    lv_indev_t *indev;
    char path[320];
    const char *found;

    lv_init();
    lv_tick_set_cb(lv_tick_cb);
    ui_display = lv_display_create(ui_w, ui_h);
    if(ui_display == NULL) {
        vio_log("ui: LVGL display creation failed");
        return -1;
    }
    lv_display_set_color_format(ui_display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(ui_display, ui_buffer, NULL,
                           (uint32_t)sizeof(ui_buffer),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(ui_display, lv_flush_cb);
    lv_display_set_default(ui_display);
    vio_display_bind(&panel);

    memset(&touch, 0, sizeof(touch));
    touch.fd = -1;
    found = vio_find_touch_device(path, sizeof(path));
    if(found && vio_touch_open(&touch, found, &panel) == 0) {
        driver_evidence.touch_open = 1;
        vio_log("ui: LVGL touch input bound to %s", path);
    }
    else {
        driver_evidence.touch_open = 0;
        vio_log("ui: no touch device; shell accepts rescue taps only");
    }
    {
        char pbuf[320];
        for(int i = 0; i < 16; i++) {
            snprintf(pbuf, sizeof(pbuf), "/dev/input/event%d", i);
            struct stat st;
            if(stat(pbuf, &st) != 0) continue;
            if(power_btn.nfd == 0) {
                if(vio_power_open(&power_btn, pbuf) == 0)
                    vio_log("power: opened %s", pbuf);
            }
            else if(vio_power_open_more(&power_btn, pbuf) == 0) {
                vio_log("power: added %s", pbuf);
            }
        }
    }
    driver_evidence.power_input_open = power_btn.nfd > 0;
    vio_drivers_set_evidence(&driver_evidence);

    indev = lv_indev_create();
    if(indev) {
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, lv_touch_read_cb);
        lv_indev_set_display(indev, ui_display);
        lv_indev_set_long_press_time(indev, 900);
    }

    vqeaf_state_refresh();
    vqeaf_ui_create(ui_display);
    lv_refr_now(ui_display);
    lv_refr_now(ui_display);
    lv_timer_handler();
    vio_log("ui: LVGL phone shell active at %dx%d", ui_w, ui_h);
    return 0;
}

static int ui_tap_hook(int x, int y)
{
    if(!ui_ready || ui_display == NULL) return -1;
    if(x < 0 || y < 0 || x >= ui_w || y >= ui_h) return -1;
    return vio_touch_inject(&touch, x, y) == 0 ? 1 : -1;
}

static int ui_hold_hook(int x, int y, int ms)
{
    if(!ui_ready || ui_display == NULL) return -1;
    if(x < 0 || y < 0 || x >= ui_w || y >= ui_h) return -1;
    return vio_touch_hold(&touch, x, y, ms) == 0 ? 1 : -1;
}

static int ui_swipe_hook(int x0, int y0, int x1, int y1, int steps)
{
    if(!ui_ready || ui_display == NULL) return -1;
    if(x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0 ||
       x0 >= ui_w || y0 >= ui_h || x1 >= ui_w || y1 >= ui_h) return -1;
    return vio_touch_swipe(&touch, x0, y0, x1, y1, steps) == 0 ? 1 : -1;
}

static volatile int ui_page_request = -1;
static volatile int ui_power_request;
static volatile sig_atomic_t ui_fault_request;
static volatile int ui_unlock_request;

static int ui_page_hook(const char *name)
{
    vqeaf_page_t page;
    if(!ui_ready || ui_display == NULL || name == NULL) return -1;
    if(strcmp(name, "files") == 0) page = VQEAF_PAGE_FILES;
    else if(strcmp(name, "apps") == 0) page = VQEAF_PAGE_APPS;
    else if(strcmp(name, "home") == 0) page = VQEAF_PAGE_HOME;
    else if(strcmp(name, "settings") == 0) page = VQEAF_PAGE_SETTINGS;
    else return -1;
    ui_page_request = (int)page;
    return 1;
}

static int ui_power_hook(void)
{
    if(!ui_ready || ui_display == NULL) return -1;
    ui_power_request = 1;
    return 1;
}

static int ui_fault_hook(void)
{
    if(!ui_ready || ui_display == NULL) return -1;
    ui_fault_request = 1;
    return 1;
}

static int ui_unlock_hook(void)
{
    if(!ui_ready || ui_display == NULL) return -1;
    ui_unlock_request = 1;
    return 1;
}

static void sync_phone_ui_orientation(void)
{
    int width, height;
    enum display_orientation orientation = display_orientation();
    if(!ui_display || ui_orientation == (int)orientation) return;

    vio_ui_canvas_size(orientation, &width, &height);
    if(width != ui_w || height != ui_h) {
        ui_w = width;
        ui_h = height;
        lv_display_set_resolution(ui_display, ui_w, ui_h);
        lv_display_set_buffers(ui_display, ui_buffer, NULL,
                               (uint32_t)sizeof(ui_buffer),
                               LV_DISPLAY_RENDER_MODE_DIRECT);
        vio_panel_set_ui_size(&panel, ui_w, ui_h);
    }
    ui_orientation = (int)orientation;
    vio_display_bind(&panel);
    vio_panel_set_rotation(&panel, display_orientation_degrees(orientation));
    vqeaf_ui_show(vqeaf_ui_page());
    lv_refr_now(ui_display);
    vio_log("ui: LVGL relayout %dx%d (%s)", ui_w, ui_h,
            display_orientation_name(orientation));
}

static void ui_fault(int sig, siginfo_t *info, void *context)
{
    (void)info;
    (void)context;
    /* Keep PID 1 alive; the main loop's setjmp point repaints or restarts
     * the shell. The real signal number reaches the log, not a guess. */
    vio_log("fault: signal %d on the UI thread", sig);
    volume_lock_drop_on_fault();
    if(in_ui) siglongjmp(ui_recovery, 1);
    if(main_thread == pthread_self()) siglongjmp(main_recovery, 1);
    for(;;) pause();
}

static void install_ui_fault_handlers(void)
{
    static const int faults[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE };
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_sigaction = ui_fault;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    for(unsigned i = 0; i < sizeof(faults) / sizeof(faults[0]); i++)
        sigaction(faults[i], &action, NULL);
}

static int usb_mode_for_ui(void)
{
    if(!lun.ready) return -1;
    return usb_gadget_mode() == USB_MODE_MTP ? VQEAF_USB_FILE
                                            : VQEAF_USB_CHARGE;
}

static void shutdown_stage(vqeaf_shutdown_action_t action, const char *stage)
{
    (void)action;
    vio_log("shutdown: %s", stage);
}

static int shutdown_final(vqeaf_shutdown_action_t action)
{
    sync();
    if(action == VQEAF_SHUTDOWN_RESTART) reboot(RB_AUTOBOOT);
    else reboot(RB_POWER_OFF);
    return -1;
}

/* The Denial compositor needs DRM master, so it must start before QEAF opens
 * card0. When it exits (or was never selected) the QEAF shell takes over. */
static void try_denial_session(void)
{
    if(!bringup.denial_present) return;
    if(!mtk_session_wants_denial(bringup.denial_path)) return;
    set_stage("denial");
    if(mtk_session_run_denial(bringup.denial_path) == 0)
        vio_log("session: Denial ended; starting the QEAF shell");
    else
        vio_log("session: Denial could not start; QEAF shell continues");
}

int main(void)
{
    uint64_t last_gadget_retry = 0;

    signal(SIGPIPE, SIG_IGN);
    main_thread = pthread_self();
    install_ui_fault_handlers();
    drm.fd = -1;
    lun.fd = -1;
    vio_drivers_evidence_init(&driver_evidence);
    vio_drivers_set_evidence(&driver_evidence);

    vio_setup_environment();
    vio_log("boot: " QEAF_OS_STRING " init on %s (PID %d)", mtk_board_name(),
            (int)getpid());

    mtk_bringup(&bringup);
    {
        MtkHwProbe hw;
        if(mtk_caps_probe_device(&hw) == 0)
            mtk_caps_publish(&hw);
    }

    rescue_set_snapshot_hook(snapshot_scanout);
    rescue_set_screenshot_hook(screenshot_png);
    rescue_set_status_hook(status_fields);
    rescue_set_tap_hook(ui_tap_hook);
    rescue_set_hold_hook(ui_hold_hook);
    rescue_set_swipe_hook(ui_swipe_hook);
    rescue_set_page_hook(ui_page_hook);
    rescue_set_power_hook(ui_power_hook);
    rescue_set_fault_hook(ui_fault_hook);
    rescue_set_unlock_hook(ui_unlock_hook);
    vqeaf_shutdown_set_hooks(shutdown_stage, shutdown_final);
    vqeaf_ui_set_usb_mode_provider(usb_mode_for_ui);
    set_stage("lun");

    if(sigsetjmp(ui_recovery, 1) == 0) {
        in_ui = 1;
        if(vio_lun_init(&lun) == 0) {
            if(vio_lun_start_gadget(&lun) < 0)
                vio_log("boot: LUN built but USB gadget not bound; will retry");
        }
        else {
            vio_log("boot: QEAFRESCUE volume unavailable");
        }
        driver_evidence.usb_gadget_bound = lun.ready && lun.gadget;
        vio_drivers_set_evidence(&driver_evidence);
        in_ui = 0;
    }
    else {
        in_ui = 0;
        vio_log("boot: LUN init faulted; continuing");
    }

    if(pthread_create(&rescue_thread, NULL, rescue_worker, NULL) == 0)
        vio_log("boot: rescue worker owns QEAF.CMD");
    else
        vio_log("boot: rescue worker did not start; main loop will poll");

    set_stage("dri-wait");
    if(vio_wait_for_path("/dev/dri/card0", 8000) < 0)
        vio_log("boot: /dev/dri/card0 did not appear; continuing without UI");

    /* Power-on session chooser: hold VOL+/VOL- for ~1.8 s at boot. It writes
     * /etc/qeaf/session before the compositor choice is made below. */
    mtk_session_boot_choose(1800);

    /* Denial must be tried after card0 exists (it takes DRM master) and
     * before QEAF opens it. When it is absent or exits, the QEAF shell
     * starts normally below. */
    try_denial_session();

    set_stage("backlight");
    if(sigsetjmp(ui_recovery, 1) == 0) {
        in_ui = 1;
        vio_set_backlight(85);
        in_ui = 0;
    }
    else {
        in_ui = 0;
        vio_log("boot: backlight set faulted");
    }

    set_stage("drm-init");
    if(sigsetjmp(ui_recovery, 1) == 0) {
        in_ui = 1;
        vio_ui_canvas_size(display_orientation(), &ui_w, &ui_h);
        ui_orientation = (int)display_orientation();
        vio_panel_init(&panel, ui_w, ui_h, MTK_PANEL_W, MTK_PANEL_H,
                       display_orientation_degrees(display_orientation()));
        if(vio_drm_init(&drm, MTK_PANEL_W, MTK_PANEL_H) == 0) {
            drm_ready = 1;
            driver_evidence.drm_modeset_ok = 1;
            vio_drivers_set_evidence(&driver_evidence);
            vio_panel_set_stride(&panel, (int)(drm.pitch / 4));
            vio_display_bind(&panel);
            set_stage("splash");
            splash_present();
            if(start_phone_ui() == 0) {
                ui_orientation = (int)display_orientation();
                ui_ready = 1;
                set_stage("phone-ui");
            }
            else {
                ui_ready = 0;
                vio_log("boot: LVGL shell unavailable; splash + LUN stay up");
            }
            in_ui = 0;
        }
        else {
            in_ui = 0;
            vio_log("boot: DRM scanout unavailable; UI disabled, LUN stays up");
        }
    }
    else {
        in_ui = 0;
        vio_log("boot: panel/DRM init faulted; UI disabled, LUN stays up");
    }

    set_stage("loop");
    for(;;) {
        int sleeping;

        if(sigsetjmp(main_recovery, 1) != 0) {
            in_ui = 0;
            vio_log("boot: recovered from a fault outside the UI; still running");
        }
        sleeping = ui_ready && vqeaf_ui_sleeping();
        vio_sleep_ms(sleeping ? 100 : 16);
        mtk_sensor_poll();

        if(drm_ready) {
            if(sigsetjmp(ui_recovery, 1) == 0) {
                in_ui = 1;
                if(ui_fault_request) {
                    volatile unsigned long *probe =
                        (volatile unsigned long *)0;
                    ui_fault_request = 0;
                    vio_log("fault: host asked for a deliberate UI fault");
                    *probe = 0x5EAFul;
                }
                if(!sleeping) display_poll();
                if(ui_ready && ui_display) {
                    if(sleeping) {
                        int wake = 0;
                        if(power_btn.nfd > 0 &&
                           vio_power_poll(&power_btn) != 0) wake = 1;
                        vio_touch_poll(&touch);
                        if(touch.pressed) wake = 1;
                        if(ui_power_request) {
                            ui_power_request = 0;
                            wake = 1;
                        }
                        if(ui_unlock_request) wake = 1;
                        if(ui_page_request >= 0) wake = 1;
                        if(wake) vqeaf_ui_activity();
                    }
                    else {
                        sync_phone_ui_orientation();
                        if(ui_page_request >= 0) {
                            int page = ui_page_request;
                            ui_page_request = -1;
                            vqeaf_ui_show((vqeaf_page_t)page);
                        }
                        if(ui_power_request) {
                            ui_power_request = 0;
                            vqeaf_ui_toggle_screen();
                        }
                        if(ui_unlock_request) {
                            ui_unlock_request = 0;
                            vqeaf_ui_show(VQEAF_PAGE_HOME);
                            vqeaf_ui_activity();
                        }
                        lv_timer_handler();
                        rec_tick();
                        if(!vqeaf_shutdown_active() && power_btn.nfd > 0) {
                            int pr = vio_power_poll(&power_btn);
                            if(power_btn.volume_delta != 0)
                                vqeaf_ui_volume_adjust(power_btn.volume_delta);
                            if(pr == 1) vqeaf_ui_power_menu_show();
                            else if(pr == 2) vqeaf_ui_toggle_screen();
                        }
                        {
                            int cable = usb_gadget_cable();
                            int pick;

                            if(cable != usb_cable_seen) {
                                usb_cable_seen = cable;
                                if(cable) vqeaf_ui_usb_dialog_show();
                                else vqeaf_ui_usb_dialog_hide();
                            }
                            pick = vqeaf_ui_take_usb_choice();
                            if(pick >= 0) {
                                enum usb_mode mode =
                                    pick == VQEAF_USB_CHARGE ? USB_MODE_CHARGE
                                                             : USB_MODE_MTP;
                                if(vio_lun_set_mode(&lun, mode) < 0)
                                    vqeaf_ui_notify_event("USB",
                                                          "Không đổi được chế độ USB",
                                                          VQEAF_SYM_DELETE,
                                                          0xFF8A7A,
                                                          VQEAF_SOUND_WARNING);
                                else
                                    vqeaf_ui_notify("USB",
                                                    pick == VQEAF_USB_CHARGE ?
                                                    "Chỉ sạc pin" :
                                                    "Đã bật truyền tệp: ổ QEAFRESCUE",
                                                    VQEAF_SYM_FOLDER, 0x35E0C8);
                            }
                        }
                    }
                }
                else if(ui_orientation != (int)display_orientation()) {
                    splash_present();
                }
                in_ui = 0;
            }
            else {
                in_ui = 0;
                vio_log("boot: UI poll faulted; LUN stays up");
            }
        }

        if(lun.ready && !lun.gadget && !lun.charge_only &&
           vio_monotonic_ms() - last_gadget_retry >= 3000) {
            last_gadget_retry = vio_monotonic_ms();
            if(sigsetjmp(ui_recovery, 1) == 0) {
                in_ui = 1;
                if(vio_lun_start_gadget(&lun) == 0)
                    vio_log("boot: USB gadget bound on retry");
                driver_evidence.usb_gadget_bound = lun.ready && lun.gadget;
                vio_drivers_set_evidence(&driver_evidence);
                in_ui = 0;
            }
            else {
                in_ui = 0;
                vio_log("boot: gadget retry faulted");
            }
        }

        if(lun.ready && !rescue_thread_up) {
            volume_lock_init();
            if(pthread_mutex_trylock(&volume_lock) == 0) {
                volume_lock_depth++;
                rescue_poll(&lun.volume);
                vio_lun_poll(&lun);
                volume_lock_release();
            }
        }
    }
}
