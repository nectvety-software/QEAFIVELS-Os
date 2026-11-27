/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_qeapp_device.h - native QEAPP execution.
 *
 * A signed native .qeapp module is executed in a forked, sandboxed child for
 * the target architecture and renders into a shared XRGB8888 canvas. One-shot launches retain a
 * hard timeout; modules exporting on_input can instead run an interactive
 * session until the shell closes its stdin pipe or kills the child. Nothing
 * here runs on the host render harness.
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
#define VQEAF_QEAPP_SESSION_STOP_MS 1000

typedef struct {
    uint32_t type;
    uint32_t code;
    int32_t x;
    int32_t y;
    uint32_t pressed;
} vqeaf_qeapp_device_input_t;

/* Starts an interactive session for a module exporting on_input. Returns 1
 * when the validated module has no input export, so the caller can use the
 * one-shot launcher; returns -1 with an error on failure. Only one session may
 * be active, and starting another stops the previous child first. */
int vqeaf_qeapp_device_session_start(const uint8_t *package, size_t size,
                                     uint32_t effective_permissions,
                                     char *error, size_t error_cap);
int vqeaf_qeapp_device_session_send(const vqeaf_qeapp_device_input_t *event);
int vqeaf_qeapp_device_session_poll(char *error, size_t error_cap);
void vqeaf_qeapp_device_session_stop(void);
int vqeaf_qeapp_device_session_active(void);
uint32_t vqeaf_qeapp_device_session_frame_seq(void);

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
