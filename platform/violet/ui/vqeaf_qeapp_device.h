/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_qeapp_device.h - native QEAPP execution on violet.
 *
 * Phase 4 device gate: a signed arm64 .qeapp module is executed in a forked
 * child on the phone, renders into a shared XRGB8888 canvas, and is killed at
 * a hard timeout. The shell keeps the returned frame and shows it on the
 * QEAPP_NATIVE page. Nothing here runs on the host render harness.
 */
#ifndef VQEAF_QEAPP_DEVICE_H
#define VQEAF_QEAPP_DEVICE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VQEAF_QEAPP_DEVICE_W 720
#define VQEAF_QEAPP_DEVICE_HEIGHT 1560
#define VQEAF_QEAPP_DEVICE_TIMEOUT_MS 5000

/* Runs the package in a forked child. `report` receives the child's
 * lifecycle/hash lines; `error` the failure reason. Returns 0 on a clean run. */
int vqeaf_qeapp_device_launch(const uint8_t *package, size_t size,
                              uint32_t effective_permissions,
                              char *report, size_t report_cap,
                              char *error, size_t error_cap);

/* Frame from the last clean run (XRGB8888, 720x1560), or NULL. */
const uint32_t *vqeaf_qeapp_device_frame(void);
const char *vqeaf_qeapp_device_lifecycle(void);
uint32_t vqeaf_qeapp_device_hash(void);

/* "app pid N · shell pid M · uid U" for the last fork: the measured point
 * where a launch leaves the shell and becomes its own process. */
const char *vqeaf_qeapp_device_process(void);

/* Set once after a clean run so the UI thread can switch to the native page. */
int vqeaf_qeapp_device_page_pending(void);
void vqeaf_qeapp_device_page_ack(void);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_QEAPP_DEVICE_H */
