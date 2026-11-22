/* SPDX-License-Identifier: MIT */
/* platform/violet/main_violet.c - VQEAF-OS PID 1 on the Redmi Note 7 Pro.
 *
 * One process owns everything: mounts, /dev, the QEAFRESCUE LUN, the DRM
 * scanout and the boot splash rendered from this tree's own canvas. There is
 * no init system and no second binary in the initramfs; if the panel cannot
 * be brought up the process keeps the rescue drive alive instead of panicking
 * the kernel. Derived from the QeafdropOs homo PID 1 (MIT, same owner); the
 * LVGL phone shell is implemented in this project and uses its own theme/pages.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <sys/reboot.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ucontext.h>

#include <libs/lodepng/lodepng.h>

#include "lvgl.h"
#include "drm_kms.h"
#include "panel_scale.h"
#include "usb_lun.h"
#include "vio_display.h"
#include "violet.h"
#include "violet_input.h"
#include "violet_power.h"
#include <sys/stat.h>
#include "vqeaf_ui.h"
#include "vqeaf_qeapp_device.h"
#include "vqeaf_qeapp.h"
#include "appstore.h"
#include "vqeaf_state.h"
#include "ui/vqeaf_shutdown.h"
#include "wifi_bringup.h"
#include "vio_caps.h"
#include "boot_journal.h"
#include "watchdog.h"

#include "qvf_encode.h"
#include "fs/fat16.h"
#include "fs/rescue.h"
#include "ui/vqeaf_files.h"
#include "qeaf_version.h"
#include "splash.h"

/* The 720x1560 portrait canvas and the 1560x720 landscape canvas hold the
 * same pixel count, so one buffer serves both orientations. */
static uint32_t ui_buffer[VIO_UI_PORTRAIT_W * VIO_UI_PORTRAIT_H];
static int ui_w = VIO_UI_PORTRAIT_W;
static int ui_h = VIO_UI_PORTRAIT_H;
static int ui_orientation = -1;
static lv_display_t *ui_display;
static struct vio_touch touch;
static struct vio_power power_btn;
static int touch_ready;

static vio_panel_t panel;
static struct vio_drm drm;
static struct vio_lun lun;
static int drm_ready;
static volatile sig_atomic_t ui_ready;

/* The rescue channel runs on its own thread: the panel path can block inside
 * a DRM commit, and QEAF.CMD must keep being answered regardless. Both
 * threads touch the RAM FAT16 volume; volume_lock.h owns the lock and the
 * TLS depth that makes it safe to leave from a fault handler. */
#include "volume_lock.h"

static pthread_t rescue_thread;
static volatile sig_atomic_t rescue_thread_up;
static const char *volatile boot_stage = "start";

/* PID 1 must never exit: the kernel panics ("Attempted to kill init!")
 * and the USB gadget disappears with it. A fault outside the UI therefore
 * lands on a recovery point at the top of the main loop instead of
 * killing init, and a worker that faults is parked where it stopped. */
static sigjmp_buf main_recovery;
static volatile sig_atomic_t main_recovery_armed;
static pthread_t main_thread;
/* Last cable state the loop acted on: the "Dùng USB cho" sheet opens on
 * the plug edge and closes on the unplug edge, not on every tick. */
static int usb_cable_seen;

/* SCREEN.RAW is the RGB565 block followed by a fixed ASCII trailer:
 * `frame=<n> fnv=<hash>`. Hosts keep decoding the pixels at offset 0, and
 * the counter lets them tell a fresh capture from a stale copy of the
 * previous one - the failure that used to read as "the UI is frozen". */
static struct {
    uint16_t pixels[RESCUE_SCREEN_W * RESCUE_SCREEN_H];
    char trailer[RESCUE_SCREEN_TRAILER];
} snap_image;
static unsigned snap_frame;

/* One character for the boot stage that was current when the scanout was
 * sampled. A capture taken before the splash is painted is black with a valid
 * fresh hash, so without this the host cannot tell "not drawn yet" from "drawn
 * but the panel shows nothing". */
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

/* ---- rescue hooks -------------------------------------------------------- */

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
    vio_log("snap: wrote SCREEN.RAW (%dx%d RGB565 %u bytes, frame %u)",
            RESCUE_SCREEN_W, RESCUE_SCREEN_H,
            (unsigned)sizeof(snap_image.pixels), (unsigned)snap_frame);
    vqeaf_ui_notify("Cứu hộ", "Đã ghi ảnh màn hình vào SCREEN.RAW",
                    VQEAF_SYM_IMAGE, 0x35E0C8);
    return (int)sizeof(snap_image);
}

/* PNG screenshot: full panel resolution, encoded in-process with the LVGL
 * lodepng copy and published on the RAM rescue volume as 8.3 SHOTnnnn.PNG.
 * Keeping it there means a PC sees the file immediately and no flash
 * partition is touched. */
static unsigned shot_frame;
static char g_last_shot[64];

/* Captures are published inside the storage tree the OS advertises, so a PC
 * sees them next to the user's own files and the shell's folders can list
 * them; the volume root stays the rescue channel's namespace. */
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

    if(vol == NULL || !drm_ready || drm.map == NULL) return -1;
    w = drm.width;
    h = drm.height;
    rgba = malloc((size_t)w * (size_t)h * 4u);
    if(rgba == NULL) return -2;

    for(y = 0; y < h; y++) {
        const uint32_t *row = (const uint32_t *)(drm.map + (size_t)y * drm.pitch);
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
        /* lodepng is optional at runtime: fall back to an uncompressed 24-bit
         * BMP rather than losing the capture. */
        size_t stride = ((size_t)w * 3u + 3u) & ~(size_t)3u;
        size_t bmp_len = 54u + stride * (size_t)h;
        unsigned char *bmp = malloc(bmp_len);
        unsigned char *pixels;

        free(rgba);
        if(bmp == NULL) return -3;
        memset(bmp, 0, 54);
        bmp[0] = 'B'; bmp[1] = 'M';
        bmp[2] = (unsigned char)(bmp_len & 0xff);
        bmp[3] = (unsigned char)((bmp_len >> 8) & 0xff);
        bmp[4] = (unsigned char)((bmp_len >> 16) & 0xff);
        bmp[5] = (unsigned char)((bmp_len >> 24) & 0xff);
        bmp[10] = 54;                       /* pixel offset */
        bmp[14] = 40;                       /* info header size */
        bmp[18] = (unsigned char)(w & 0xff);
        bmp[19] = (unsigned char)((w >> 8) & 0xff);
        bmp[22] = (unsigned char)(h & 0xff);
        bmp[23] = (unsigned char)((h >> 8) & 0xff);
        bmp[26] = 1; bmp[28] = 24;          /* planes, bpp */
        bmp[34] = (unsigned char)(bmp_len & 0xff);
        bmp[35] = (unsigned char)((bmp_len >> 8) & 0xff);
        bmp[36] = (unsigned char)((bmp_len >> 16) & 0xff);
        pixels = bmp + 54;
        for(y = 0; y < h; y++) {
            /* BMP rows are bottom-up; rebuild them from the scanout directly. */
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
            vqeaf_ui_notify("Ảnh màn hình", name, VQEAF_SYM_IMAGE, 0x35E0C8);
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
        vqeaf_ui_notify("Ảnh màn hình", name, VQEAF_SYM_IMAGE, 0x35E0C8);
        vqeaf_ui_show_capture_thumb(name);
    }
    return rc;
}

/* ---- Screen recorder (QVF) ----------------------------------------------
 * The clip is the container the OS itself decodes (docs/QEAF_VIDEO_FORMAT.md):
 * 240x416 RGB565 frames, LZ4-compressed, written to UserData/Videos. Frames are
 * sampled from the UI loop once per interval, so the file shows the shell
 * moving; the previous implementation captured synchronously inside
 * vio_rec_start(), which froze the panel for the whole recording and produced
 * one still picture.
 */
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
static int g_rec_active, g_rec_fps, g_rec_seconds;
static unsigned g_rec_file;
static char g_rec_status[128] = "idle";
static char g_rec_name[24];

/* A clip may use at most half of the volume's free space and never more than
 * its own budget, so a long recording cannot fill the rescue drive. */
static size_t rec_budget(void)
{
    size_t free_bytes = 0, budget = REC_BUDGET;

    /* volume_lock is recursive, so this is safe even though `rec start`
     * arrives from rescue_poll(), which already holds the lock. */
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

/* Writes the finished clip. Called with the recorder already stopped, from the
 * UI thread, so the volume lock is taken only for the write itself. */
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
        vqeaf_ui_notify("Quay màn hình", g_rec_name, VQEAF_SYM_MOVIE, 0xFF7A7A);
    }
    else {
        snprintf(g_rec_status, sizeof(g_rec_status),
                 "save failed after %u frames (%s)", g_rec_frames, reason);
        vio_log("rec: %s", g_rec_status);
        vqeaf_ui_notify("Quay màn hình", "Không ghi được tệp video",
                        VQEAF_SYM_MOVIE, 0xFF8A7A);
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

    if(g_rec_active) return -16;                  /* EBUSY */
    if(!drm_ready || drm.map == NULL) return -19; /* ENODEV */
    if(fps < 1 || fps > 15) fps = 8;
    if(seconds < 1 || seconds > REC_MAX_SECONDS) seconds = 10;

    budget = rec_budget();
    if(budget == 0) {
        snprintf(g_rec_status, sizeof(g_rec_status),
                 "refused: no room on the rescue volume");
        vio_log("rec: %s", g_rec_status);
        return -28;                               /* ENOSPC */
    }
    if(g_rec_buf == NULL) g_rec_buf = malloc(REC_BUDGET);
    if(g_rec_xrgb == NULL)
        g_rec_xrgb = malloc((size_t)REC_W * REC_H * sizeof(uint32_t));
    if(g_rec_buf == NULL || g_rec_xrgb == NULL) return -12;

    g_rec_cap = budget;
    g_rec_used = vio_qvf_header(g_rec_buf, g_rec_cap, REC_W, REC_H,
                                (uint16_t)fps);
    if(g_rec_used == 0) return -22;                /* EINVAL */
    g_rec_frames = 0;
    g_rec_fps = fps;
    g_rec_seconds = seconds;
    g_rec_limit = (unsigned)fps * (unsigned)seconds;
    g_rec_interval_ms = 1000u / (unsigned)fps;
    g_rec_next_ms = 0;
    g_rec_active = 1;
    snprintf(g_rec_status, sizeof(g_rec_status),
             "recording 0/%u frames (%ux%u @%d fps)", g_rec_limit,
             REC_W, REC_H, fps);
    vio_log("rec: started, %ux%u @%d fps, %u frame(s), %u-byte budget",
            REC_W, REC_H, fps, g_rec_limit, (unsigned)g_rec_cap);
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

/* Screenshot on demand, from the shell or the rescue `shot` verb. The volume
 * lock is what keeps a capture from racing the rescue worker's own writes. */
const char *vio_last_screenshot(void) { return g_last_shot; }

int vio_screenshot_now(void)
{
    char name[24];
    int rc;

    if(!drm_ready || drm.map == NULL) return -1;
    volume_lock_acquire();
    rc = lun.ready ? screenshot_png(&lun.volume, name, sizeof(name)) : -1;
    if(rc > 0) rescue_flush_log(&lun.volume);
    volume_lock_release();
    if(rc > 0)
        vqeaf_ui_notify("Ảnh màn hình", name, VQEAF_SYM_IMAGE, 0x35E0C8);
    return rc;
}


static void status_fields(char *resp, int cap)
{
    char line[128];
    int n = (int)strlen(resp);
    int k;
    vqeaf_shutdown_action_t action = vqeaf_shutdown_action();
    const char *power = action == VQEAF_SHUTDOWN_RESTART ? "restart" :
                        action == VQEAF_SHUTDOWN_POWER_OFF ? "poweroff" : "on";

    k = snprintf(line, sizeof(line),
                 "; stage=%s; ui=%d; page=%d; drm=%d; power=%s; uptime=%us",
                 (const char *)boot_stage, ui_ready ? 1 : 0,
                 (int)vqeaf_ui_page(), drm_ready, power,
                 (unsigned)(vio_monotonic_ms() / 1000u));
    if(k < 0 || n + k >= cap) return;
    memcpy(resp + n, line, (size_t)k);
    resp[n + k] = '\0';
}

/* The UI thread asks for the QEAFRESCUE directory through this hook; the
 * volume lock is owned here, next to the rescue worker. */
static int ui_sync_list(const char *path, vqeaf_sync_entry_t *out, int max)
{
    int count;

    volume_lock_acquire();
    count = fat16_list_path(&lun.volume, path, (fat16_entry_t *)out, max);
    volume_lock_release();
    return count;
}

static void appstore_lock(void) { volume_lock_acquire(); }
static void appstore_unlock(void) { volume_lock_release(); }

static int ui_sync_read(const char *name, uint8_t **out, size_t *size)
{
    uint8_t *buffer;
    int bytes, count;

    if(name == NULL || out == NULL || size == NULL) return -1;
    volume_lock_acquire();
    bytes = fat16_file_size(&lun.volume, name);
    if(bytes <= 0 || bytes > (16 * 1024 * 1024)) {
        volume_lock_release();
        return -1;
    }
    buffer = malloc((size_t)bytes);
    if(buffer == NULL) {
        volume_lock_release();
        return -1;
    }
    count = fat16_read_path(&lun.volume, name, buffer, (size_t)bytes);
    volume_lock_release();
    if(count != bytes) {
        free(buffer);
        return -1;
    }
    *out = buffer;
    *size = (size_t)count;
    return 0;
}

static void set_stage(const char *stage)
{
    boot_stage = stage;
    /* The p77 journal is the only record that survives the panic which
     * follows a dead PID 1; 512 bytes per stage change is cheap. */
    vio_journal_stage(stage);
}

/* ---- restart / power-off (real machine reset, owned by PID 1) ------------ */

static const char *shutdown_action_name(vqeaf_shutdown_action_t action)
{
    return action == VQEAF_SHUTDOWN_POWER_OFF ? "poweroff" : "restart";
}

/* Each stage is written to QEAFBOOT.LOG before the next one runs, so a reset
 * that hangs still leaves the last completed stage on the rescue volume. */
static void shutdown_stage(vqeaf_shutdown_action_t action, const char *stage)
{
    const char *name = shutdown_action_name(action);
    char line[128];

    vio_log("shutdown: action=%s stage=%s", name, stage);
    volume_lock_acquire();
    snprintf(line, sizeof(line), "[    -    ] N qeaf: %s %s", name, stage);
    rescue_note(line);
    if(lun.ready) rescue_flush_log(&lun.volume);
    volume_lock_release();
    set_stage(name);
}

/* Called by the shutdown module after the restarting screen has been shown
 * and the log is flushed. reboot() never returns on success; if the kernel
 * refuses it we report the real errno instead of faking a restart. */
static int shutdown_final(vqeaf_shutdown_action_t action)
{
    const char *name = shutdown_action_name(action);
    int saved;

    sync();
    vio_log("shutdown: sync complete; issuing machine reset (%s)", name);
    volume_lock_acquire();
    if(lun.ready) rescue_flush_log(&lun.volume);
    volume_lock_release();
    if(action == VQEAF_SHUTDOWN_POWER_OFF) reboot(RB_POWER_OFF);
    else reboot(RB_AUTOBOOT);
    saved = errno;
    vio_log("shutdown: machine reset refused errno=%d", saved);
    return -1;
}

/* ---- Files/Video/Gallery read the real QEAFRESCUE LUN ---------------------
 * Captures live in UserData/Pictures and UserData/Videos on the RAM FAT volume.
 * Virtual paths map to those folders; everything still goes through the volume
 * lock, exactly like the rescue channel. No file is faked. */
static int lun_media_path(const char *path, char *out, size_t cap)
{
    if(path == NULL || out == NULL || cap == 0) return -1;
    if(strcmp(path, "/media") == 0 ||
       strcmp(path, "/Videos") == 0)
        return snprintf(out, cap, "%s", "UserData/Videos") >= 0 ? 0 : -1;
    if(strncmp(path, "/media/", 7) == 0)
        return snprintf(out, cap, "UserData/Videos/%s", path + 7) >= 0 ? 0 : -1;
    if(strncmp(path, "/Videos/", 8) == 0)
        return snprintf(out, cap, "UserData/Videos/%s", path + 8) >= 0 ? 0 : -1;
    if(strcmp(path, "/Pictures") == 0)
        return snprintf(out, cap, "%s", "UserData/Pictures") >= 0 ? 0 : -1;
    if(strncmp(path, "/Pictures/", 10) == 0)
        return snprintf(out, cap, "UserData/Pictures/%s", path + 10) >= 0 ? 0 : -1;
    return -1;   /* let the normal filesystem handle this path */
}

static int lun_files_read(const char *path, uint8_t *out, size_t cap)
{
    char rel[256];
    int n;
    if(lun_media_path(path, rel, sizeof(rel)) != 0 || !lun.ready) return -1;
    volume_lock_acquire();
    n = fat16_read_path(&lun.volume, rel, out, cap);
    volume_lock_release();
    return n;
}

static int lun_files_list(const char *path, vqeaf_file_t *out, int max)
{
    char rel[256];
    fat16_entry_t entries[32];
    int i, n, nmax;
    if(lun_media_path(path, rel, sizeof(rel)) != 0 || !lun.ready) return -1;
    nmax = max < (int)(sizeof(entries)/sizeof(entries[0])) ?
           max : (int)(sizeof(entries)/sizeof(entries[0]));
    volume_lock_acquire();
    n = fat16_list_path(&lun.volume, rel, entries, nmax);
    volume_lock_release();
    if(n <= 0) return n;
    for(i = 0; i < n && i < max; i++) {
        snprintf(out[i].name, sizeof(out[i].name), "%s", entries[i].name);
        out[i].size = entries[i].size;
        out[i].is_dir = entries[i].is_dir;
    }
    return n;
}

/* ---- the rescue worker ---------------------------------------------------- */

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

/* ---- UI ------------------------------------------------------------------ */

/* Boot UI init runs in its own thread. A stalled DRM ioctl or LVGL draw must
 * never pin the main thread at `splash`: the rescue worker and the watchdog
 * live on the main thread's timeline. If this does not finish in time, boot
 * continues with the splash already on the panel and UI disabled. */
static volatile int ui_init_done;
static pthread_t ui_init_tid;

static const char *const splash_lines[] = {
    "[  OK  ] drm: scanout ready",
    "[  OK  ] lun: QEAFRESCUE bound",
    "[  OK  ] rescue: QEAF.CMD online",
    NULL,
};

/* Renders the splash into the canvas for the current orientation and paints
 * the whole panel. Called after DRM init and whenever the rescue channel's
 * `rotate` verb changes the orientation. */
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
                      "QEAFIVELS OS", QEAF_OS_VERSION " violet", splash_lines);
    if(drm_ready && drm.map) {
        vio_panel_render(&panel, ui_buffer, (uint32_t *)drm.map,
                         vio_panel_full_rect(&panel));
        vio_drm_flush(&drm, 0, 0, drm.width - 1, drm.height - 1);
    }
    vio_log("ui: splash %dx%d (%s)", ui_w, ui_h,
            display_orientation_name(display_orientation()));
}

static int start_phone_ui(void); /* defined below, used by boot_ui_worker */

static void *boot_ui_worker(void *arg)
{
    (void)arg;
    vio_log("boot: UI worker started (splash paint)");
    splash_present();
    vio_log("boot: splash painted; starting phone UI");
    if(start_phone_ui() == 0) {
        ui_orientation = (int)display_orientation();
        ui_ready = 1;
        set_stage("phone-ui");
        vio_log("boot: phone UI ready");
    } else {
        ui_ready = 0;
        vio_log("boot: LVGL shell unavailable; keeping splash and rescue LUN");
    }
    ui_init_done = 1;
    return NULL;
}

static uint32_t lv_tick_cb(void)
{
    return (uint32_t)vio_monotonic_ms();
}

static void lv_flush_cb(lv_display_t *display, const lv_area_t *area,
                        uint8_t *pixels)
{
    (void)pixels; /* DIRECT mode draws into ui_buffer. */
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

static int g_hit_trace_x = -1;
static int g_hit_trace_y = -1;

/* One line per distinct synthetic contact point, naming the object the same
 * hit test LVGL runs would pick. This is what answered "the injected tap did
 * nothing": the point resolved to the sheet's scrim instead of a button. */
static void ui_trace_hit(int x, int y)
{
    lv_obj_t *hit;
    lv_area_t h;
    lv_point_t p;

    if(x == g_hit_trace_x && y == g_hit_trace_y) return;
    g_hit_trace_x = x;
    g_hit_trace_y = y;
    p.x = x;
    p.y = y;
    hit = lv_indev_search_obj(lv_display_get_layer_top(ui_display), &p);
    if(hit == NULL)
        hit = lv_indev_search_obj(lv_display_get_screen_active(ui_display), &p);
    if(hit == NULL) {
        vio_log("hit %d,%d: nothing", x, y);
        return;
    }
    lv_obj_get_coords(hit, &h);
    vio_log("hit %d,%d -> %d,%d..%d,%d kids=%d clickable=%d", x, y,
            (int)h.x1, (int)h.y1, (int)h.x2, (int)h.y2,
            (int)lv_obj_get_child_count(hit),
            (int)lv_obj_has_flag(hit, LV_OBJ_FLAG_CLICKABLE));
}

static void lv_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    /* During a restart the shell must not react to fingers or rescue taps. */
    if(vqeaf_shutdown_active()) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->continue_reading = 0;
        return;
    }
    /* Poll unconditionally: with no evdev node this still presents taps
     * injected by the rescue channel. */
    vio_touch_poll(&touch);
    if(touch.pressed && touch.tap.release_pending) ui_trace_hit(touch.x, touch.y);
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
        touch_ready = 1;
        vio_log("ui: LVGL touch input bound to %s", path);
    }
    else {
        touch_ready = 0;
        vio_log("ui: no touch device; shell accepts rescue taps only");
    }
    /* Power/volume keys: open every evdev node that reports KEY_POWER or
     * KEY_VOLUMEUP, so the side buttons work even without a fixed node name. */
    {
        char pbuf[320];
        for(int i = 0; i < 16; i++) {
            snprintf(pbuf, sizeof(pbuf), "/dev/input/event%d", i);
            struct stat st;
            if(stat(pbuf, &st) != 0) continue;
            /* probe by opening; vio_power_open handles the first node. */
            if(power_btn.nfd == 0) {
                if(vio_power_open(&power_btn, pbuf) == 0) {
                    vio_log("power: opened %s", pbuf);
                }
            } else {
                if(vio_power_open_more(&power_btn, pbuf) == 0) {
                    vio_log("power: added %s", pbuf);
                }
            }
        }
    }

    /* The pointer device exists either way: `tap` from QEAF.CMD injects into
     * vio_touch, so an undetected panel does not make the shell unoperable. */
    indev = lv_indev_create();
    if(indev) {
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, lv_touch_read_cb);
        lv_indev_set_display(indev, ui_display);
        /* The rescue `tap` keeps its contact down for three polls so a
         * scrollable page does not swallow the CLICK. Long-press must stay
         * longer than that, or a synthetic tap would start a dock drag. */
        lv_indev_set_long_press_time(indev, 900);
    }

    vqeaf_state_refresh();
    vqeaf_ui_create(ui_display);
    vqeaf_ui_set_sync_list(ui_sync_list);
    vqeaf_ui_set_sync_read(ui_sync_read);
    /* QEAFRESCUE is tmpfs, so installed packages are mirrored into the p77
     * reserve and re-created here after every boot. Reinstall goes through the
     * normal signed path, not a trust-the-catalog shortcut. */
    appstore_set_volume(&lun.volume, appstore_lock, appstore_unlock);
    {
        char names[QEAPP_MAX][APPSTORE_NAME];
        int restored = appstore_restore(names, QEAPP_MAX);
        if(restored > 0) {
            appstore_set_restoring(1);
            for(int i = 0; i < restored; i++) {
                char error[128];
                if(vqeaf_qeapp_install_stored(names[i], error, sizeof(error)) != 0)
                    vio_log("appstore: reinstall %s failed: %s", names[i], error);
            }
            appstore_set_restoring(0);
        }
        vio_log("appstore: boot restore %d app(s)", restored > 0 ? restored : 0);
    }
    lv_refr_now(ui_display);
    lv_refr_now(ui_display);
    lv_timer_handler();
    vio_log("ui: LVGL phone shell active at %dx%d", ui_w, ui_h);
    return 0;
}

/* Rescue `tap` verb: queue a click at canvas coordinates. Runs on the rescue
 * worker thread; the UI thread presents it through vio_touch_poll(), so the
 * hit-test path is the same one a finger uses. */
static void inject_three_wrapper(int dx, int dy)
{
    vio_touch_inject_three(&touch, dx, dy);
}

static int ui_tap_hook(int x, int y)
{
    if(!ui_ready || ui_display == NULL) return -1;
    /* ui_w/ui_h are written by the UI thread only on rotation; a tap racing a
     * rotation at worst lands on the previous canvas and is refused by LVGL. */
    if(x < 0 || y < 0 || x >= ui_w || y >= ui_h) return -1;
    return vio_touch_inject(&touch, x, y) == 0 ? 1 : -1;
}

/* Rescue `hold` verb: keep the synthetic contact at one point for a wall-clock
 * duration, so the shell's 900 ms long-press timer fires as it would under a
 * finger (this is how the app-info sheet is reachable from the host). */
static int ui_hold_hook(int x, int y, int ms)
{
    if(!ui_ready || ui_display == NULL) return -1;
    if(x < 0 || y < 0 || x >= ui_w || y >= ui_h) return -1;
    return vio_touch_hold(&touch, x, y, ms) == 0 ? 1 : -1;
}

/* Rescue `swipe` verb: queue a synthetic drag. Press, one move per UI poll,
 * release; a scrollable page reads it exactly like a finger drag. */
static int ui_swipe_hook(int x0, int y0, int x1, int y1, int steps)
{
    if(!ui_ready || ui_display == NULL) return -1;
    if(x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0 ||
       x0 >= ui_w || y0 >= ui_h || x1 >= ui_w || y1 >= ui_h) return -1;
    return vio_touch_swipe(&touch, x0, y0, x1, y1, steps) == 0 ? 1 : -1;
}

/* Rescue `page` verb: ask the UI thread for a named page. The request is a
 * flag the UI loop consumes, because LVGL objects belong to that thread. */
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
    else if(strcmp(name, "defaultapps") == 0)
        page = VQEAF_PAGE_DEFAULT_APPS;
    else if(strcmp(name, "terminal") == 0) page = VQEAF_PAGE_TERMINAL;
    else if(strcmp(name, "ntfs") == 0) page = VQEAF_PAGE_NTFS_SEARCH;
    else if(strcmp(name, "tasks") == 0) page = VQEAF_PAGE_TASK_MANAGER;
    else if(strcmp(name, "cleanup") == 0) page = VQEAF_PAGE_DISK_CLEANUP;
    else if(strcmp(name, "wifi") == 0) page = VQEAF_PAGE_WIFI;
    else if(strcmp(name, "gallery") == 0) page = VQEAF_PAGE_GALLERY;
    else if(strcmp(name, "video") == 0) page = VQEAF_PAGE_VIDEO;
    else return -1;
    ui_page_request = (int)page;
    return 1;
}

/* The `power` verb runs the same path as a short power-key press, so a host
 * can lock the screen and wake it again without touching the button. */
static int ui_power_hook(void)
{
    if(!ui_ready || ui_display == NULL) return -1;
    ui_power_request = 1;
    return 1;
}

/* Rescue `fault ui`: queue a deliberate UI fault for the loop below, so
 * the crash-record path is exercised on the bench instead of trusted. */
static int ui_fault_hook(void)
{
    if(!ui_ready || ui_display == NULL) return -1;
    ui_fault_request = 1;
    return 1;
}

/* Rescue `unlock`: wake to Home without a synthetic swipe, so a device
 * check does not depend on a gesture that a finger would have to make. */
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

/* ---- fault guards --------------------------------------------------------- */

/* A fault inside the panel path must not take PID 1 down: an exiting PID 1
 * panics the kernel and the USB gadget disappears. Faults elsewhere are real
 * bugs and are re-raised. */
static sigjmp_buf ui_recovery;
static volatile sig_atomic_t in_ui;

/* ---- crash record (async-signal-safe) ------------------------------------
 * ui_fault() runs in a signal: no malloc, no stdio, no LVGL calls, no
 * blocking lock. It formats the record into a static buffer with hand-rolled
 * integer conversion, writes it to stderr first (the kernel keeps that in its
 * ring, so it survives into pstore even if the process goes on to die) and
 * then tries the rescue volume with a trylock, because the fault may have
 * interrupted the rescue thread while it held the volume mutex. Anything the
 * handler could not write is republished from the main loop with the real
 * lock and the newest log tail, so CRASH.TXT ends up holding both. */
#define CRASH_LINES 40

static char crash_text[VIO_CRASH_CAP];
static volatile sig_atomic_t crash_pending;
static volatile sig_atomic_t crash_running;

static char *cr_str(char *p, char *end, const char *s)
{
    while(*s != '\0' && p < end) *p++ = *s++;
    return p;
}

static char *cr_dec(char *p, char *end, unsigned long v)
{
    char digits[24];
    int n = 0;

    do {
        digits[n++] = (char)('0' + (int)(v % 10ul));
        v /= 10ul;
    } while(v != 0 && n < (int)sizeof(digits));
    while(n > 0 && p < end) *p++ = digits[--n];
    return p;
}

static char *cr_hex(char *p, char *end, unsigned long v)
{
    static const char digits[] = "0123456789abcdef";
    char text[2 * sizeof(unsigned long)];
    int n = 0;

    p = cr_str(p, end, "0x");
    do {
        text[n++] = digits[v & 0xFul];
        v >>= 4;
    } while(v != 0 && n < (int)sizeof(text));
    while(n > 0 && p < end) *p++ = text[--n];
    return p;
}

/* Newest CRASH_LINES lines of the vio_log ring; when the record cannot hold
 * the whole tail the oldest bytes are dropped, not the newest. */
static size_t cr_log_tail(char *p, char *end)
{
    const char *tail = vio_log_tail();
    size_t len, start, room, lines = 0;

    if(tail == NULL) return 0;
    len = strlen(tail);
    start = len;
    while(start > 0 && lines < CRASH_LINES) {
        start--;
        if(tail[start] == '\n' && start + 1 < len) lines++;
    }
    room = (size_t)(end - p);
    if(len - start > room) start = len - room;
    if(start >= len) return 0;
    memcpy(p, tail + start, len - start);
    return len - start;
}

static size_t crash_build(int sig, int code, unsigned long addr,
                          unsigned long pc, unsigned long lr,
                          unsigned long sp, const char *thread)
{
    char *end = crash_text + sizeof(crash_text) - 1;
    char *p = crash_text;

    p = cr_str(p, end, "### QEAF CRASH\n");
    p = cr_str(p, end, "sig=");    p = cr_dec(p, end, (unsigned long)sig);
    p = cr_str(p, end, " code=");  p = cr_dec(p, end, (unsigned long)code);
    p = cr_str(p, end, " addr=");  p = cr_hex(p, end, addr);
    p = cr_str(p, end, "\npc=");   p = cr_hex(p, end, pc);
    p = cr_str(p, end, " lr=");    p = cr_hex(p, end, lr);
    p = cr_str(p, end, " sp=");    p = cr_hex(p, end, sp);
    p = cr_str(p, end, "\npage=");
    p = cr_dec(p, end, (unsigned long)vqeaf_ui_page());
    p = cr_str(p, end, " stage=");
    p = cr_str(p, end, boot_stage != NULL ? boot_stage : "?");
    p = cr_str(p, end, " ui=");    p = cr_dec(p, end, ui_ready ? 1ul : 0ul);
    p = cr_str(p, end, " uptime_s=");
    p = cr_dec(p, end, (unsigned long)(vio_monotonic_ms() / 1000u));
    p = cr_str(p, end, " thread=");
    p = cr_str(p, end, thread != NULL ? thread : "?");
    p = cr_str(p, end, " boots=");
    p = cr_dec(p, end, (unsigned long)vio_journal_boots());
    p = cr_str(p, end, "\n--- last ");
    p = cr_dec(p, end, CRASH_LINES);
    p = cr_str(p, end, " log lines ---\n");
    p += cr_log_tail(p, end);
    p = cr_str(p, end, "\n=== end crash record ===\n");
    *p = '\0';
    return (size_t)(p - crash_text);
}

static int crash_write_file(void)
{
    if(!lun.ready) return 0;
    return fat16_write_file(&lun.volume, "CRASH.TXT",
                            (const uint8_t *)crash_text,
                            strlen(crash_text)) >= 0;
}

/* from_handler = 1 never blocks: pthread_mutex_trylock, and the record is
 * republished from the loop anyway, so a busy volume loses nothing. */
static void crash_flush(int from_handler)
{
    int ok;

    if(crash_text[0] == '\0') return;
    if(!lun.ready) return;
    if(from_handler) {
        if(pthread_mutex_trylock(&volume_lock) != 0) return;
        volume_lock_depth++;
        (void)crash_write_file();
        volume_lock_release();
        return;
    }
    volume_lock_acquire();
    ok = crash_write_file();
    volume_lock_release();
    if(ok) crash_pending = 0;
}

static void ui_fault(int sig, siginfo_t *info, void *context)
{
    unsigned long pc = 0, lr = 0, sp = 0, addr = 0;
    int code = 0;
    int is_main;

#if defined(__aarch64__)
    const ucontext_t *uc = (const ucontext_t *)context;
    if(uc != NULL) {
        pc = (unsigned long)uc->uc_mcontext.pc;
        lr = (unsigned long)uc->uc_mcontext.regs[30];
        sp = (unsigned long)uc->uc_mcontext.sp;
    }
#else
    (void)context;
#endif
    if(info != NULL) {
        addr = (unsigned long)info->si_addr;
        code = (int)info->si_code;
    }
    is_main = pthread_equal(pthread_self(), main_thread);

    /* A fault raised while recording must not loop: skip the capture and let
     * the default action report the original signal. */
    if(!crash_running) {
        crash_running = 1;
        crash_build(sig, code, addr, pc, lr, sp,
                    is_main ? "main" : "worker");
        crash_pending = 1;
        (void)write(STDERR_FILENO, crash_text, strlen(crash_text));
        crash_flush(1);
        /* The RAM volume dies with the panic; p77 does not. */
        vio_journal_crash(crash_text);
        crash_running = 0;
    }

    /* Whatever this thread was doing when it faulted, it will never reach
     * the matching release(): either a siglongjmp leaves the guarded block
     * or the worker parks below. Hand the lock back now, or the other
     * thread blocks in volume_lock_acquire() and QEAF.CMD goes unanswered
     * while the drive still looks healthy to the host. */
    volume_lock_drop_on_fault();

    if(in_ui) {
        static const char message[] =
            "<2>vqeaf: UI fault, rescue drive stays up\n";
        ssize_t ignored = write(STDERR_FILENO, message, sizeof(message) - 1);
        (void)ignored;
        in_ui = 0;
        ui_ready = 0;
        siglongjmp(ui_recovery, 1);
    }
    /* Re-raising here used to kill PID 1, panic the kernel and reboot the
     * phone by itself - losing the rescue drive and the record with it.
     * The main thread goes back to the loop; a worker is parked where it
     * stopped. Both leave the record in p77 for the next boot. */
    if(is_main && main_recovery_armed) {
        static const char message[] =
            "<2>vqeaf: fault outside the UI, back to the main loop\n";
        ssize_t ignored = write(STDERR_FILENO, message, sizeof(message) - 1);
        (void)ignored;
        siglongjmp(main_recovery, 1);
    }
    {
        static const char message[] =
            "<2>vqeaf: worker fault, thread parked, main loop takes over QEAF.CMD\n";
        ssize_t ignored = write(STDERR_FILENO, message, sizeof(message) - 1);
        (void)ignored;
    }
    /* Hand the rescue channel to the main loop's trylock fallback. A parked
     * worker must not look "up" forever: that is what used to wedge QEAF.CMD
     * while the drive still appeared healthy to the host. */
    if(!is_main) rescue_thread_up = 0;
    for(;;) pause();
}

static void install_ui_fault_handlers(void)
{
    static const int faults[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    struct sigaction action;
    size_t i;

    memset(&action, 0, sizeof(action));
    action.sa_sigaction = ui_fault;
    sigemptyset(&action.sa_mask);
    /* SA_SIGINFO: the handler needs si_code/si_addr and the ucontext
     * registers (pc/lr/sp); a plain sa_handler never receives them. */
    action.sa_flags = SA_SIGINFO;
    for(i = 0; i < sizeof(faults) / sizeof(faults[0]); i++)
        sigaction(faults[i], &action, NULL);
}

/* Republish the record the previous boot left in the p77 journal. This is
 * how a panic reaches the host at all: the RAM volume that held CRASH.TXT
 * is rebuilt from scratch on the next boot. */
static void publish_last_boot(void)
{
    const char *previous = vio_journal_previous();
    char text[256];

    if(previous == NULL || previous[0] == '\0') {
        /* The slot is armed and counted, but holds no blank record: the last
         * boot ended without a caught signal. Write that on the drive instead
         * of leaving 15 KiB of NULs that reads like a failed publish. */
        snprintf(text, sizeof(text),
                 "QEAF boot record: boots=%u (this boot)\n"
                 "no fault record from the previous boot: it ended without a\n"
                 "caught signal (watchdog, power or kernel reset).\n",
                 vio_journal_boots());
        volume_lock_acquire();
        (void)fat16_write_file(&lun.volume, "LASTBOOT.TXT",
                               (const uint8_t *)text, strlen(text));
        volume_lock_release();
        vio_log("boot: no fault record from the previous boot (boots=%u)",
                vio_journal_boots());
        return;
    }
    volume_lock_acquire();
    (void)fat16_write_file(&lun.volume, "LASTBOOT.TXT",
                           (const uint8_t *)previous, strlen(previous));
    volume_lock_release();
    vio_log("boot: previous boot record published (LASTBOOT.TXT, %u bytes)",
            (unsigned)strlen(previous));
}

/* Settings reads the mode the platform actually applied. The gadget still
 * has one writer: main applies the sheet's pick below, as before. */
static int usb_mode_for_ui(void)
{
    if(!lun.ready) return -1;
    /* The data function is mass storage or nothing: vio_lun_set_mode() turns a
     * PTP request into MTP before it ever becomes the reported mode. */
    return usb_gadget_mode() == USB_MODE_MTP ? VQEAF_USB_FILE
                                            : VQEAF_USB_CHARGE;
}

int main(void)
{
    uint64_t last_gadget_retry = 0;

    signal(SIGPIPE, SIG_IGN);
    main_thread = pthread_self();
    volume_lock_init();   /* recursive: verb hooks may re-acquire it */
    install_ui_fault_handlers();
    drm.fd = -1;
    lun.fd = -1;

    vio_setup_environment();
    /* Device nodes exist now: count this boot and re-arm the p77 journal
     * slot, so even a panic during init leaves "boots=N stage=..."
     * behind for the next boot. */
    vio_journal_init();
    /* The stock kernel cannot read radio firmware from partitions nobody
     * mounted. Stage the read-only vendor/radio/persist sources before the UI. */
    vio_wifi_bringup();
    /* Publish the honest hardware capability census from real read-only
     * probes, so the UI and the rescue `caps` verb reflect what this device
     * actually exposes (eMMC not UFS, 60Hz panel, firmware-blocked radio). */
    {
        VioHwProbe hw;
        if (vio_caps_probe_device(&hw) == 0)
            vio_caps_publish(&hw);
    }
    vio_log("boot: " QEAF_OS_STRING " init on violet (PID %d)", (int)getpid());
    rescue_set_snapshot_hook(snapshot_scanout);
    rescue_set_screenshot_hook(screenshot_png);
    rescue_set_open_image_hook(vqeaf_ui_open_picture);
    rescue_set_three_finger_hook(inject_three_wrapper);
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

    /* The rescue drive is the only way back into a phone whose panel path is
     * unproven, so bind it before anything that can block or fault. */
    if(sigsetjmp(ui_recovery, 1) == 0) {
        in_ui = 1;
        if(vio_lun_init(&lun) == 0) {
            vqeaf_files_set_backends(lun_files_read, lun_files_list);
            if(vio_lun_start_gadget(&lun) < 0)
                vio_log("boot: LUN built but USB gadget is not bound; will retry");
        }
        else {
            vio_log("boot: QEAFRESCUE volume unavailable");
        }
        in_ui = 0;
    }
    else {
        in_ui = 0;
        vio_log("boot: LUN init faulted; continuing");
    }

    if(lun.ready) publish_last_boot();

    if(pthread_create(&rescue_thread, NULL, rescue_worker, NULL) == 0) {
        vio_log("boot: rescue worker owns QEAF.CMD");
    }
    else {
        vio_log("boot: rescue worker did not start; main loop will poll QEAF.CMD");
    }

    set_stage("dri-wait");
    if(vio_wait_for_path("/dev/dri/card0", 8000) < 0)
        vio_log("boot: /dev/dri/card0 did not appear; continuing without UI");

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
        vio_panel_init(&panel, ui_w, ui_h, VIO_PANEL_W, VIO_PANEL_H,
                       display_orientation_degrees(display_orientation()));
        if(vio_drm_init(&drm, VIO_PANEL_W, VIO_PANEL_H) == 0) {
            drm_ready = 1;
            /* The dumb buffer's pitch includes row padding (1088 pixels for
             * a 1080-pixel row on violet); rendering with panel_w as stride
             * would shear every row by 8 pixels. */
            vio_panel_set_stride(&panel, (int)(drm.pitch / 4));
            vio_display_bind(&panel);
            set_stage("splash");
            /* Bounded init: never let a display ioctl keep PID 1 at `splash`.
             * The splash itself was painted by DRM init already, so the panel
             * is not blank even when the shell never comes up. */
            ui_init_done = 0;
            if(pthread_create(&ui_init_tid, NULL, boot_ui_worker, NULL) == 0) {
                for(int tick = 0; tick < 100 && !ui_init_done; tick++)
                    vio_sleep_ms(50);   /* at most 5 s */
                if(!ui_init_done) {
                    ui_ready = 0;
                    vio_log("boot: UI init timed out after 5 s; "
                            "rescue LUN stays up");
                }
            } else {
                ui_ready = 0;
                vio_log("boot: UI init thread did not start; splash stays up");
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
    /* The bootloader arrives with the SoC watchdog armed; vendor init pets
     * it, this PID 1 did not, and that is why every boot ended in a reset
     * about 40 s in. Open it here, at the point the main loop starts. */
    vio_watchdog_start();
    for(;;) {
        int sleeping;

        /* Recovery point for a fault outside every guarded block: the
         * record is already in p77 and PID 1 keeps the drive alive. */
        if(sigsetjmp(main_recovery, 1) != 0) {
            main_recovery_armed = 0;
            vio_log("boot: recovered from a fault outside the UI; "
                    "still running");
        }
        main_recovery_armed = 1;
        /* Proof of life for the watchdog: acked every iteration, including
         * while the screen sleeps, and fed at most every 5 s. */
        vio_watchdog_ack();
        vio_watchdog_pet();
        sleeping = ui_ready && vqeaf_ui_sleeping();
        /* A fault seen by the handler is republished from here with the
         * real volume lock and the newest log tail. */
        if(crash_pending) crash_flush(0);
        /* Sleep is a real low-power state: no 60 Hz LVGL/indev loop and no
         * per-second sysfs refresh. Only the wake sources are polled. */
        vio_sleep_ms(sleeping ? 100 : 16);
        vio_wifi_poll();

        /* Three-finger swipe: capture the real scanout. Same path as the
         * Control Center tile and the Power+Volume shortcut. */
        if(vio_touch_take_three_finger(&touch)) {
            char sname[64] = "";
            int slen;
            volume_lock_acquire();
            slen = lun.ready ?
                   screenshot_png(&lun.volume, sname, sizeof(sname)) : -1;
            volume_lock_release();
            if(slen > 0)
                vqeaf_ui_notify("Ảnh màn hình", sname, VQEAF_SYM_IMAGE, 0x35E0C8);
            else
                vqeaf_ui_notify("Ảnh màn hình", "Chụp không thành công",
                                VQEAF_SYM_DELETE, 0xFF8A7A);
        }

        if(drm_ready) {
            if(sigsetjmp(ui_recovery, 1) == 0) {
                in_ui = 1;
                if(ui_fault_request) {
                    /* `fault ui`: a real SIGSEGV on the UI thread. */
                    volatile unsigned long *probe =
                        (volatile unsigned long *)0;
                    ui_fault_request = 0;
                    vio_log("fault: host asked for a deliberate UI"
                            " fault");
                    *probe = 0x5EAFul;
                }
                if(!sleeping) display_poll();
                /* Sample the live scanout for the QVF recorder. Without this
                 * tick the record verb started but captured zero frames. */
                rec_tick();
                /* A rescue `rotate` (or the sensor) changes the seam value;
                 * repaint exactly once per change. */
                if(ui_ready && ui_display) {
                    if(sleeping) {
                        int wake = 0;
                        if(power_btn.nfd > 0 &&
                           vio_power_poll(&power_btn) != 0) wake = 1;
                        vio_touch_poll(&touch);
                        if(touch.pressed) wake = 1;
                        /* Pressing power while asleep is a wake, not a new
                         * toggle; the lock page comes up with the shell's
                         * status bar. */
                        if(ui_power_request) {
                            ui_power_request = 0;
                            wake = 1;
                        }
                        /* Unlocking has to wake the panel first, then
                         * the request is consumed below. */
                        if(ui_unlock_request) wake = 1;
                        /* A queued install/launch runs on the UI thread, so an
                         * asleep screen must wake instead of stalling it. */
                        if(ui_page_request >= 0 ||
                           vqeaf_qeapp_install_pending() ||
                           vqeaf_qeapp_launch_pending() ||
                           vqeaf_qeapp_data_pending() ||
                           vqeaf_qeapp_uninstall_pending())
                            wake = 1;
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
                        /* The restarting screen keeps rendering; all hardware
                         * button input is ignored until the machine resets. */
                        lv_timer_handler();
                        rec_tick();
                        {
                            char request[64];
                            if(vqeaf_qeapp_take_launch_request(request, sizeof(request))) {
                                int index = -1;
                                for(int i = 0; i < vqeaf_qeapp_count(); i++) {
                                    const vqeaf_qeapp_t *app = vqeaf_qeapp_at(i);
                                    if(app != NULL &&
                                       (strcmp(app->name, request) == 0 ||
                                        strcmp(app->id, request) == 0)) {
                                        index = i;
                                        break;
                                    }
                                }
                                if(index >= 0) {
                                    vio_log("qeapp: launch %s", request);
                                    vqeaf_ui_run_app(index);
                                }
                                else vio_log("qeapp: launch %s not installed", request);
                            }
                        }
                        {
                            int slot = vqeaf_qeapp_take_uninstall_request();
                            if(slot >= 0) {
                                if(vqeaf_qeapp_uninstall(slot) == 0) {
                                    vio_log("qeapp: uninstalled slot %d", slot);
                                    vqeaf_ui_notify("Ứng dụng",
                                                    "Đã gỡ khỏi thiết bị",
                                                    VQEAF_SYM_DELETE,
                                                    0xFF8A7A);
                                }
                                else vio_log("qeapp: uninstall slot %d failed", slot);
                                vqeaf_ui_show(VQEAF_PAGE_APPS);
                            }
                        }
                        {
                            int slot = -1;
                            char text[512];
                            if(vqeaf_qeapp_take_data_request(&slot, text,
                                                             sizeof(text))) {
                                if(vqeaf_qeapp_data_write(slot, text,
                                                          strlen(text)) == 0)
                                    vio_log("qeapp: data slot %d = %u bytes",
                                            slot, (unsigned)strlen(text));
                                else vio_log("qeapp: data slot %d write failed",
                                             slot);
                            }
                        }
                        {
                            char request[128];
                            if(vqeaf_qeapp_take_install_request(request, sizeof(request))) {
                                char error[128];
                                if(vqeaf_qeapp_install(request, error, sizeof(error)) != 0)
                                    vio_log("qeapp: install %s failed: %s", request, error);
                                else {
                                    vio_log("qeapp: installed %s", request);
                                    vqeaf_ui_notify("Đã cài ứng dụng", request,
                                                    VQEAF_SYM_CHECK_CIRCLE,
                                                    0x35E0C8);
                                    vqeaf_ui_show(VQEAF_PAGE_APPS);
                                }
                            }
                        }
                        if(vqeaf_qeapp_device_page_pending()) {
                            vqeaf_qeapp_device_page_ack();
                            vqeaf_ui_show(VQEAF_PAGE_QEAPP_NATIVE);
                        }
                        if(!vqeaf_shutdown_active() && power_btn.nfd > 0) {
                            int pr = vio_power_poll(&power_btn);

                            if(power_btn.screenshot) {
                                char sname[64] = "";
                                int slen;

                                power_btn.screenshot = 0;
                                volume_lock_acquire();
                                if(lun.ready)
                                    slen = screenshot_png(&lun.volume, sname,
                                                          sizeof(sname));
                                else
                                    slen = -1;
                                volume_lock_release();
                                if(slen <= 0)
                                    vqeaf_ui_notify("Ảnh màn hình",
                                                    "Chụp không thành công",
                                                    VQEAF_SYM_DELETE, 0xFF8A7A);
                            }
                            if(power_btn.record_toggle) {
                                power_btn.record_toggle = 0;
                                /* Long Power+Volume (>=1 s): start/stop the
                                 * real QVF recorder, same path as the Control
                                 * Center tile and the `rec` terminal verb. */
                                if(vio_rec_active()) {
                                    vio_rec_stop();
                                } else if(vio_rec_start(8, 10) == 0) {
                                    vqeaf_ui_notify("Quay màn hình",
                                                    "Đang quay 10 giây, 8 fps",
                                                    VQEAF_SYM_MOVIE, 0xFF7A7A);
                                } else {
                                    vqeaf_ui_notify("Quay màn hình",
                                                    "Không quay được màn hình",
                                                    VQEAF_SYM_DELETE, 0xFF8A7A);
                                }
                            }
                            if(power_btn.volume_delta != 0)
                                vqeaf_ui_volume_adjust(power_btn.volume_delta);
                            if(pr == 1) vqeaf_ui_power_menu_show();
                            else if(pr == 2) vqeaf_ui_toggle_screen();
                        }
                        /* A cable that just arrived asks how the host
                         * should see the phone; the answer is applied on
                         * the platform, next tick. */
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
                                /* The sheet only offers the two modes this
                                 * kernel can honour, so no PTP fallback is
                                 * reachable here. */
                                enum usb_mode mode =
                                    pick == VQEAF_USB_CHARGE ? USB_MODE_CHARGE
                                                             : USB_MODE_MTP;
                                int rc = vio_lun_set_mode(&lun, mode);

                                if(rc < 0)
                                    vqeaf_ui_notify("USB",
                                                    "Không đổi được chế độ USB",
                                                    VQEAF_SYM_DELETE, 0xFF8A7A);
                                else if(pick == VQEAF_USB_CHARGE)
                                    /* Charge-only hides QEAFRESCUE, so the
                                     * notice names the way back. */
                                    vqeaf_ui_notify("USB",
                                                    "Chỉ sạc pin, máy đã ẩn khỏi máy tính — bật lại trong Cài đặt › USB",
                                                    VQEAF_SYM_BATTERY_CHARGING_FULL,
                                                    0x35E0C8);
                                else
                                    vqeaf_ui_notify("USB",
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

        /* The dwc3 UDC can register after the first bind attempt; keep trying
         * so a late probe still gives the host a rescue drive. A deliberate
         * "Chỉ sạc" is not a failed bind, so it is excluded: otherwise the
         * retry would put the phone back on the PC three seconds later. */
        if(lun.ready && !lun.gadget && !lun.charge_only &&
           vio_monotonic_ms() - last_gadget_retry >= 3000) {
            last_gadget_retry = vio_monotonic_ms();
            if(sigsetjmp(ui_recovery, 1) == 0) {
                in_ui = 1;
                if(vio_lun_start_gadget(&lun) == 0)
                    vio_log("boot: USB gadget bound on retry");
                in_ui = 0;
            }
            else {
                in_ui = 0;
                vio_log("boot: gadget retry faulted");
            }
        }

        /* The worker owns QEAF.CMD and the LUN when it started; the loop only
         * steps in when the thread could not be created. */
        if(lun.ready && !rescue_thread_up) {
            /* trylock: a parked worker can still hold the volume, and a
             * blocking lock here would wedge the loop that is supposed
             * to keep the phone alive. */
            if(pthread_mutex_trylock(&volume_lock) == 0) {
                volume_lock_depth++;
                rescue_poll(&lun.volume);
                vio_lun_poll(&lun);
                volume_lock_release();
            }
        }
    }
}
