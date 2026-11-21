/* SPDX-License-Identifier: MIT */
/* platform/mt689x/mt689x_bringup.h - early hardware setup for chopin/ares.
 *
 * Everything here is read-only with respect to the stock partitions: firmware
 * and vendor data are mounted read-only, and nothing is written to UFS. The
 * one writable path is the QEAFRESCUE RAM volume, which is tmpfs-backed.
 */
#ifndef QEAF_MT689X_BRINGUP_H
#define QEAF_MT689X_BRINGUP_H

#include <stddef.h>

typedef struct {
    int vendor_mounted;     /* something ext4/erofs is mounted at /vendor */
    int firmware_ready;     /* a mediatek/mt6893 firmware dir is visible */
    char firmware_dir[160]; /* first directory that actually has the blobs */
    int modules_loaded;     /* .ko files init_module() accepted */
    int modules_failed;     /* .ko files that were present but rejected */
    int denial_present;     /* a Denial compositor binary was found */
    char denial_path[160];
    int audio_module;       /* mt6893 MT6359 card module loaded */
    int sensor_module;      /* SCP sensor hub module loaded */
} MtkBringup;

/* Mounts vendor/firmware read-only when the by-name nodes exist, points
 * firmware_class at the directory that really holds the MT6893 blobs, and
 * loads the QEAF module set. Safe to call once, early; failures are logged
 * and reported, never fatal. */
void mtk_bringup(MtkBringup *out);

/* init_module() one file from the packaged module directory. Returns 0 when
 * the module is loaded (or was already), -1 otherwise. */
int mtk_load_module(const char *file);

/* Reads the SCP sensor-hub accelerometer and feeds display_feed_sensor().
 * No-op when auto-rotate is off or the IIO device is absent. */
void mtk_sensor_poll(void);

/* True when the IIO accelerometer can be read right now. */
int mtk_accel_available(void);

#endif /* QEAF_MT689X_BRINGUP_H */
