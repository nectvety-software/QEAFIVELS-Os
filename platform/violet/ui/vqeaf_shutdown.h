/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_shutdown.h - restart / power-off sequence.
 *
 * The LVGL shell never resets the machine itself. It owns the visible
 * sequence (full-screen "restarting" overlay + real stage text), while PID 1
 * owns the platform side through hooks: flush the boot log, sync the
 * filesystem, then issue the machine reset. If the reset is refused, the
 * failure is shown instead of pretending the OS restarted.
 *
 * This module is host-buildable: no <sys/reboot.h>, no platform calls.
 */
#ifndef VQEAF_SHUTDOWN_H
#define VQEAF_SHUTDOWN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VQEAF_SHUTDOWN_NONE = 0,
    VQEAF_SHUTDOWN_RESTART,
    VQEAF_SHUTDOWN_POWER_OFF
} vqeaf_shutdown_action_t;

/* Called at each real stage: "request", "flush-log", "sync", "cancelled".
 * The platform hook logs it and publishes the boot log to the rescue volume. */
typedef void (*vqeaf_shutdown_stage_fn)(vqeaf_shutdown_action_t action,
                                        const char *stage);
/* Called once, after sync. Returns 0 when the reset has been issued (normally
 * never returns in the guest), negative when the kernel refused it. */
typedef int (*vqeaf_shutdown_final_fn)(vqeaf_shutdown_action_t action);

void vqeaf_shutdown_set_hooks(vqeaf_shutdown_stage_fn stage,
                              vqeaf_shutdown_final_fn final);

/* Called from vqeaf_ui_create() once a default LVGL display exists. */
void vqeaf_shutdown_init(void);

/* Starts the sequence. Repeated requests while a sequence is running are
 * ignored, so a double tap cannot issue two resets. */
void vqeaf_shutdown_request(vqeaf_shutdown_action_t action);
/* Aborts a refused sequence and returns to the shell. */
void vqeaf_shutdown_cancel(void);

int vqeaf_shutdown_active(void);
vqeaf_shutdown_action_t vqeaf_shutdown_action(void);
const char *vqeaf_shutdown_title(void);
/* Driven by an LVGL timer; safe to call from the main loop as well. */
void vqeaf_shutdown_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_SHUTDOWN_H */
