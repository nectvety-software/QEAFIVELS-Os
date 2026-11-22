/* SPDX-License-Identifier: MIT */
/* platform/violet/violet.h - shared helpers for the VQEAF-OS violet port.
 *
 * Derived from QeafdropOs homo/platform/violet (MIT, same owner). The violet
 * target is not a normal Linux userspace: VQEAF-OS is PID 1 on a stock Xiaomi
 * kernel whose initramfs is the whole root filesystem. There is no devtmpfs,
 * no fbdev and no udev, so this layer owns the pieces a normal distro would
 * provide: mounts, device nodes, logging and the panel geometry.
 */
#ifndef HOMO_VIOLET_H
#define HOMO_VIOLET_H

#include <stddef.h>
#include <stdint.h>

/* Physical panel of the Redmi Note 7 Pro, in its native portrait orientation. */
#define VIO_PANEL_W 1080
#define VIO_PANEL_H 2340

/* The VQEAF phone shell renders into a 720x1560 portrait canvas (or 1560x720
 * landscape). Both are exact 1.5x fits of the 1080x2340 panel, so the scaler
 * fills the glass with no letterbox bars in either orientation. The canvas
 * is taller than the 720x1280 QEMU reference: the shell layout is height
 * relative (dock and home bar anchor to the bottom), so the same renderer
 * serves both without a second layout. */
#define VIO_UI_PORTRAIT_W 720
#define VIO_UI_PORTRAIT_H 1560
#define VIO_UI_LAND_W 1560
#define VIO_UI_LAND_H 720

/* Console + kernel-log logging. Safe before and after /dev is mounted. */
void vio_log(const char *fmt, ...);

/* RAM tail of every vio_log line since boot; published on the volume as
 * HOMOLOG.TXT. */
const char *vio_log_tail(void);

/* Last `cap` bytes of /dev/kmsg, unfiltered. Returns length or -1. */
int vio_kernel_tail(char *buf, size_t cap);

int vio_write_file(const char *path, const char *text);
int vio_read_file(const char *path, char *buf, size_t cap);
int vio_read_int(const char *path, int *out);

uint64_t vio_monotonic_ms(void);
void vio_sleep_ms(unsigned ms);

/* Screen recorder: samples the live DRM scanout into a QVF1 clip
 * (docs/QEAF_VIDEO_FORMAT.md, 240x416 RGB565 + LZ4) saved as
 * UserData/Videos/VIDnnnn.QVF. Start only arms the recorder; the UI loop
 * captures one frame per interval, so it never freezes the shell. */
int  vio_rec_start(int fps, int seconds);
int  vio_rec_stop(void);
int  vio_rec_active(void);
const char *vio_rec_status(void);

/* Chụp màn hình: encodes the live scanout as a PNG into
 * UserData/Pictures/SHOTnnnn.PNG. Returns the byte count or -errno. */
int  vio_screenshot_now(void);
/* Name of the most recent successfully written capture (RAM-only). */
const char *vio_last_screenshot(void);

int vio_mkdir_p(const char *path, unsigned mode);
int vio_mount_fs(const char *source, const char *target, const char *type,
                 unsigned long flags, const char *data);

/* Mounts proc/sys/dev/run/tmp/configfs and creates the writable directories. */
int vio_setup_environment(void);

/* Creates /dev nodes from /sys (no devtmpfs on this kernel). Idempotent. */
int vio_create_devnodes(void);
int vio_wait_for_path(const char *path, unsigned timeout_ms);

/* Best-effort panel backlight: /sys/class/backlight or /sys/class/leds. */
int vio_set_backlight(int percent);
int vio_haptics_available(void);
int vio_haptic_pulse(unsigned duration_ms);

/* Finds the multitouch evdev node. Returns its path in `buf` or NULL. */
const char *vio_find_touch_device(char *buf, size_t cap);

#endif /* HOMO_VIOLET_H */
