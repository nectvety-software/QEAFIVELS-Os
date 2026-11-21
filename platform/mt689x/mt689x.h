/* SPDX-License-Identifier: MIT */
/* platform/mt689x/mt689x.h - shared geometry and helpers for the
 * MT6891/MT6893 (chopin / ares) QEAF platform.
 *
 * Same shape as platform/violet: QEAF is PID 1 on a Linux kernel and owns
 * mounts, /dev, the panel and the rescue LUN. Everything here is the part
 * that differs from violet: a 1080x2400 DSI panel, UFS instead of eMMC and
 * a MediaTek connectivity/sensor stack instead of Qualcomm's.
 */
#ifndef QEAF_MT689X_H
#define QEAF_MT689X_H

#include <stddef.h>
#include <stdint.h>

/* The two chopin panels (k10a_36_02_0a, l16_36_02_0b) are 1080x2400. */
#define MTK_PANEL_W 1080
#define MTK_PANEL_H 2400

/* 1.5x canvas, same scale factor as violet, so the shell layout (which is
 * height relative) gets 40 more rows without a second layout. */
#define MTK_UI_PORTRAIT_W 720
#define MTK_UI_PORTRAIT_H 1600
#define MTK_UI_LAND_W 1600
#define MTK_UI_LAND_H 720

/* Shared helpers from the violet platform that the MTK port reuses verbatim:
 * logging, /dev construction, mounts, the DRM backend and the scaler. They
 * are compiled from platform/violet with no source fork, so a fix in one
 * applies to both boards. */
void vio_log(const char *fmt, ...);
const char *vio_log_tail(void);
int vio_kernel_tail(char *buf, size_t cap);
int vio_write_file(const char *path, const char *text);
int vio_read_file(const char *path, char *buf, size_t cap);
int vio_read_int(const char *path, int *out);
uint64_t vio_monotonic_ms(void);
void vio_sleep_ms(unsigned ms);
int vio_mkdir_p(const char *path, unsigned mode);
int vio_mount_fs(const char *source, const char *target, const char *type,
                 unsigned long flags, const char *data);
int vio_setup_environment(void);
int vio_create_devnodes(void);
int vio_wait_for_path(const char *path, unsigned timeout_ms);
int vio_set_backlight(int percent);
const char *vio_find_touch_device(char *buf, size_t cap);

#endif /* QEAF_MT689X_H */
