/* SPDX-License-Identifier: MIT */
/* fs/rescue.h - QEAF.CMD command loop over the RAM FAT16 LUN. */

#ifndef UMOS_RESCUE_H
#define UMOS_RESCUE_H

#include "fs/fat16.h"

/* Publish HELP.TXT / SUMMARY.TXT / RESPONSE.TXT / QEAFBOOT.LOG. */
void rescue_init(struct fat16_volume *vol);

/* Called once per main-loop tick. Runs a QEAF.CMD batch once (dedup by
 * content) and writes the answer back to RESPONSE.TXT. */
void rescue_poll(struct fat16_volume *vol);

/* Host-gate seam: point the flash verb at a fixture file instead of p77. */
void rescue_set_partition_path(const char *path);

/* Optional screen dump. The platform registers a function that reads the
 * live scanout buffer, downscales it to RGB565 and writes it into `vol` as
 * SCREEN.RAW. Returns the byte count, or a negative error. Without a hook
 * the `snap` verb answers "unavailable" and nothing is written. */
#define RESCUE_SCREEN_W 240
#define RESCUE_SCREEN_H 520
#define RESCUE_SCREEN_BYTES (RESCUE_SCREEN_W * RESCUE_SCREEN_H * 2)
/* SCREEN.RAW = RGB565 pixels + this ASCII trailer
 * ("frame=<n> fnv=<hash> st=<c>"). Hosts keep reading the pixel block at
 * offset 0; the counter and hash tell a fresh capture from a stale copy of the
 * previous one, and st names the boot stage that was current when the frame
 * was sampled (0 start, l lun, w dri-wait, b backlight, d drm-init, s splash,
 * p phone-ui, o loop). A black frame at a pre-paint stage is a capture taken
 * too early, not evidence of a rendering fault. */
#define RESCUE_SCREEN_TRAILER 40
#define RESCUE_SCREEN_TOTAL (RESCUE_SCREEN_BYTES + RESCUE_SCREEN_TRAILER)
typedef int (*rescue_snapshot_fn)(struct fat16_volume *vol);
void rescue_set_snapshot_hook(rescue_snapshot_fn fn);

/* Optional PNG screenshot. Returns the byte count and writes the generated
 * file name into `name`; without a hook the `shot` verb says unavailable. */
typedef int (*rescue_screenshot_fn)(struct fat16_volume *vol, char *name,
                                    size_t cap);
void rescue_set_screenshot_hook(rescue_screenshot_fn fn);

/* Opens a captured photo in the shell's full-screen preview. */
typedef void (*rescue_open_image_fn)(const char *name);
void rescue_set_open_image_hook(rescue_open_image_fn fn);

/* Optional UI language switch. The platform registers a function that applies
 * a language value to the running UI and persists it. Returns 0 when the
 * language was applied, 1 when it was queued for the next desktop frame
 * (the UI belongs to the UI thread), a negative error otherwise. Without a
 * hook the `lang` verb answers "unavailable". */
typedef int (*rescue_language_fn)(int lang);
void rescue_set_language_hook(rescue_language_fn fn);

/* Optional touch injection. The platform maps canvas pixels (the UI buffer
 * coordinate space, not raw panel coordinates) to the same hit-test a finger
 * tap uses. Returns 0 when applied, 1 when queued for the UI thread, negative
 * when the point is outside the canvas or the UI is down. Without a hook the
 * `tap` verb answers "unavailable". Lets a host gate the touch path end to
 * end without a finger on the glass. */
typedef int (*rescue_tap_fn)(int x, int y);
void rescue_set_tap_hook(rescue_tap_fn fn);

/* Optional synthetic drag. The platform presses at (x0,y0), walks `steps`
 * points to (x1,y1) and releases, all on the UI thread, so a scrollable page
 * can be moved from the host exactly like a finger drag. Canvas pixels again.
 * Returns 0 when applied, 1 when queued, negative when refused. Without a
 * hook the `swipe` verb answers "unavailable". */
typedef int (*rescue_hold_fn)(int x, int y, int ms);
void rescue_set_hold_hook(rescue_hold_fn fn);

typedef int (*rescue_swipe_fn)(int x0, int y0, int x1, int y1, int steps);
void rescue_set_swipe_hook(rescue_swipe_fn fn);

/* Optional page switch. The platform maps a short page name ("files", "apps",
 * "home", "settings") to its UI page and shows it on the UI thread. Returns 0
 * when applied, 1 when queued for the next frame, negative when the name is
 * unknown or the UI is down. Without a hook `page` answers "unavailable". */
typedef int (*rescue_page_fn)(const char *name);
void rescue_set_page_hook(rescue_page_fn fn);

/* Optional power-key equivalent. The platform runs its short-press handler on
 * the UI thread: screen off locks to the lock page, the next press wakes to
 * it. Lets a host exercise lock and unlock without a finger on the button.
 * Returns 0 when applied, 1 when queued, negative when the UI is down.
 * Without a hook `power` answers "unavailable". */
typedef int (*rescue_power_fn)(void);
void rescue_set_power_hook(rescue_power_fn fn);

/* Optional boot-stage reporter. The platform appends its own
 * "; stage=...; drm=..; ui=..; uptime=..s" fields (no newline) so `status`
 * says where a boot is stuck, not only what the hardware looks like. Without
 * a hook the line ends after control_lun. */
typedef void (*rescue_status_fn)(char *resp, int cap);
void rescue_set_status_hook(rescue_status_fn fn);

/* Optional fault injection for the crash-record path. The platform queues a
 * real fault on the UI thread, so the record writer (signal handler -> rescue
 * volume) is proved on the bench instead of trusted. Returns 1 when queued,
 * negative when the UI is down. Without a hook `fault` answers "unavailable". */
typedef int (*rescue_fault_fn)(void);
void rescue_set_fault_hook(rescue_fault_fn fn);

/* Optional unlock. The platform wakes the panel and shows Home on the UI
 * thread, so a device check does not have to reproduce the unlock swipe (a
 * synthetic drag on the lock page is not reliable). Returns 0 applied, 1
 * queued, negative when the UI is down. Without a hook `unlock` answers
 * "unavailable". */
typedef int (*rescue_unlock_fn)(void);
void rescue_set_unlock_hook(rescue_unlock_fn fn);

/* Boot trace lines from the platform, appended to QEAFBOOT.LOG. rescue_note()
 * only buffers; rescue_flush_log() puts the buffer on the volume, so a boot
 * that hangs after the line still leaves its last stage on the LUN for the
 * host to read - the channel's own liveness proof. */
void rescue_note(const char *line);
void rescue_flush_log(struct fat16_volume *vol);

/* 1 = last flash verified, negative = refused/failed, 0 = no flash yet. */
int rescue_last_result(void);

#endif
