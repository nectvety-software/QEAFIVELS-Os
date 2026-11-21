/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_qeapp_runtime.h - guarded native runtime (Phase 4).
 *
 * Runs a signed native .qeapp in an isolated, time-bounded child process so a
 * crashing or hanging module cannot take down the shell. On a clean run the
 * module's 720x1560 XRGB8888 frame is returned as an LVGL image descriptor the
 * shell can blit. The child is the simulator/runner executable; the supervisor
 * only spawns, waits, parses the report and releases resources.
 */
#ifndef VQEAF_QEAPP_RUNTIME_H
#define VQEAF_QEAPP_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VQEAF_NATIVE_CANVAS_W   720
#define VQEAF_NATIVE_CANVAS_H   1560
#define VQEAF_NATIVE_TIMEOUT_MS 5000

typedef struct {
    int crashed;                 /* child died from a signal / exception */
    int timed_out;               /* child exceeded the timeout and was killed */
    int exit_code;               /* child exit status (0 = clean run) */
    uint32_t framebuffer_hash;   /* FNV-1a of the returned frame */
    size_t outstanding_allocs;   /* leaked read_file buffers reported by child */
    char lifecycle[128];         /* e.g. "create start pause resume stop destroy" */
    char log[512];               /* child report, truncated */
} vqeaf_native_report_t;

/* Guarded launch. runner_path = runner executable, package_path = signed
 * .qeapp. Fills canvas (720x1560 XRGB8888) and report on a clean run. Returns
 * 0 on a clean guarded run, -1 on crash / timeout / launch / validation error
 * (reason in error). The caller (shell) always survives. */
int vqeaf_qeapp_native_launch(const char *runner_path, const char *package_path,
                              lv_image_dsc_t *canvas,
                              vqeaf_native_report_t *report,
                              char *error, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_QEAPP_RUNTIME_H */