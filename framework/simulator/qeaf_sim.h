/* SPDX-License-Identifier: MIT */
/* framework/simulator/qeaf_sim.h - host simulator for framework API v2.
 *
 * Loads a signed .qeapp v2 package through the real device inspector
 * (vqeaf_qeapp_inspect_native), validates and maps the .qen module into an
 * executable arena, resolves its imports/exports, and drives the lifecycle
 * against fake display/input/storage. x86_64 modules only; the host executes
 * SysV code through the QEAF_APP_CALL convention. No device hardware is used.
 */
#ifndef QEAF_SIM_H
#define QEAF_SIM_H

#include <stddef.h>
#include <stdint.h>

#include "qeaf_device.h"
#include "qeaf_native_abi.h"

#define QEAF_SIM_MAX_LOG    256
#define QEAF_SIM_MAX_DRAWS  256
#define QEAF_SIM_MAX_TRACE  64
#define QEAF_SIM_MAX_FILES  16
#define QEAF_SIM_MAX_INPUT  64
#define QEAF_SIM_MAX_ALLOCS 64
#define QEAF_SIM_MAX_SPLITS 32
#define QEAF_SIM_MAX_SPLIT_RECTS 8

/* Hard bound on the mapped module arena (code+rodata+data+bss). A module that
 * would need more is rejected before anything is mapped or executed. */
#define QEAF_SIM_MAX_ARENA  (2u * 1024u * 1024u)

typedef struct {
    const char *path;
    const uint8_t *data;
    size_t size;
} qeaf_sim_file_t;

typedef struct {
    const uint8_t *package;
    size_t package_size;
    const qeaf_sim_file_t *files;
    size_t file_count;
    const qeaf_input_event_t *input;
    size_t input_count;
    uint32_t width;    /* 0 -> profile canvas, else 720 */
    uint32_t height;   /* 0 -> profile canvas, else 1560 */
    /* Non-NULL selects a device VM: canvas/panel geometry, quota defaults,
     * panel touch mapping and optional scaled scanout all come from here. */
    const qeaf_device_profile_t *device;
    /* Phase 5 broker/quota overrides; 0/NULL -> defaults. scope "" = safety-only
     * path checks (no containment); a non-empty scope is a required prefix. */
    const char *scope;
    size_t memory_budget;     /* 0 -> QEAF_QUOTA_MEMORY_BYTES */
    uint32_t call_budget_ms;  /* 0 -> QEAF_QUOTA_CALL_MS */
} qeaf_sim_config_t;

typedef struct qeaf_sim qeaf_sim_t;

/* Load + validate + map a module. Returns NULL with a reason on error. */
qeaf_sim_t *qeaf_sim_new(const qeaf_sim_config_t *cfg, char *error, size_t cap);
void qeaf_sim_free(qeaf_sim_t *sim);

/* Lifecycle steps; return 0 on success, -1 with a reason on error. */
int qeaf_sim_create_app(qeaf_sim_t *sim, char *error, size_t cap);
int qeaf_sim_start(qeaf_sim_t *sim, char *error, size_t cap);
int qeaf_sim_pause(qeaf_sim_t *sim, char *error, size_t cap);
int qeaf_sim_resume(qeaf_sim_t *sim, char *error, size_t cap);
/* Deliver one input event to on_input() while the app is started. Used by
 * the interactive --ui window (and any host that drives a keyboard app);
 * cfg.input is still delivered once by qeaf_sim_start(). */
int qeaf_sim_input(qeaf_sim_t *sim, const qeaf_input_event_t *event,
                   char *error, size_t cap);
int qeaf_sim_stop(qeaf_sim_t *sim, char *error, size_t cap);
int qeaf_sim_destroy_app(qeaf_sim_t *sim, char *error, size_t cap);

/* Full lifecycle create->start->pause->resume->stop->destroy. Fails if the
 * app leaks allocations (outstanding read_file buffers after destroy). */
int qeaf_sim_run(qeaf_sim_t *sim, char *error, size_t cap);

/* Inspection. */
const char *qeaf_sim_state(const qeaf_sim_t *sim);
uint32_t qeaf_sim_permissions(const qeaf_sim_t *sim);
size_t qeaf_sim_outstanding_allocs(const qeaf_sim_t *sim);
uint32_t qeaf_sim_framebuffer_hash(const qeaf_sim_t *sim);
const uint32_t *qeaf_sim_framebuffer(const qeaf_sim_t *sim);
uint32_t qeaf_sim_width(const qeaf_sim_t *sim);
uint32_t qeaf_sim_height(const qeaf_sim_t *sim);
const qeaf_device_profile_t *qeaf_sim_device(const qeaf_sim_t *sim);
uint32_t qeaf_sim_panel_width(const qeaf_sim_t *sim);
uint32_t qeaf_sim_panel_height(const qeaf_sim_t *sim);
size_t qeaf_sim_arena_size(const qeaf_sim_t *sim);

/* Phase 5 broker/quota inspection. */
int qeaf_sim_call_timeout(const qeaf_sim_t *sim);
uint32_t qeaf_sim_max_call_ms(const qeaf_sim_t *sim);
uint32_t qeaf_sim_deny_count(const qeaf_sim_t *sim);
size_t qeaf_sim_memory_used(const qeaf_sim_t *sim);
size_t qeaf_sim_audit_count(const qeaf_sim_t *sim);
const char *qeaf_sim_audit_at(const qeaf_sim_t *sim, size_t index);

/* Trace accessors. */
size_t qeaf_sim_log_count(const qeaf_sim_t *sim);
const char *qeaf_sim_log_at(const qeaf_sim_t *sim, size_t index);
size_t qeaf_sim_draw_count(const qeaf_sim_t *sim);
int qeaf_sim_draw_at(const qeaf_sim_t *sim, size_t index, int *x, int *y,
                     char *text, size_t cap);
/* Text weight (QEAF_TEXT_WEIGHT_*) recorded for a draw; v4 draw_text_weighted
 * sets it, plain draw_text records REGULAR. */
int qeaf_sim_draw_weight_at(const qeaf_sim_t *sim, size_t index, uint32_t *weight);
size_t qeaf_sim_trace_count(const qeaf_sim_t *sim);
const char *qeaf_sim_trace_at(const qeaf_sim_t *sim, size_t index);
size_t qeaf_sim_input_delivered(const qeaf_sim_t *sim);

/* api_blit_pixels (framework API v3) inspection. */
size_t qeaf_sim_blit_count(const qeaf_sim_t *sim);
int qeaf_sim_blit_last(const qeaf_sim_t *sim, int *x, int *y, int *w, int *h);

/* api_layout_split_weighted (framework API v5) inspection. Every call is
 * recorded, including rejected ones (rc == -1), so the gate can assert both
 * the proportional rects and the negative controls. Up to
 * QEAF_SIM_MAX_SPLIT_RECTS rects per call are kept. */
size_t qeaf_sim_split_count(const qeaf_sim_t *sim);
int qeaf_sim_split_at(const qeaf_sim_t *sim, size_t index, int *rc, uint32_t *n);
int qeaf_sim_split_rect_at(const qeaf_sim_t *sim, size_t index, uint32_t rect,
                           qeaf_rect_t *out);

#endif /* QEAF_SIM_H */