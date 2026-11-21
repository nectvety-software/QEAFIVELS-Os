/* SPDX-License-Identifier: MIT */
/* framework/simulator/qeaf_sim.c - host simulator for framework API v2. */
#include "qeaf_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qeaf_broker.h"
#include "panel_scale.h"
#include "vqeaf_qeapp.h"
#include "vqeaf_qeapp_native.h"
#include "vqeaf/font8x8.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <time.h>
#endif

#define SIM_DEFAULT_W 720u
#define SIM_DEFAULT_H 1560u

typedef struct {
    int x, y;
    char text[128];
    uint32_t weight;
} sim_draw_t;

typedef struct {
    void *ptr;
    size_t size;
} sim_alloc_t;

typedef struct {
    int rc;
    uint32_t n;
    qeaf_rect_t rects[QEAF_SIM_MAX_SPLIT_RECTS];
} sim_split_t;

struct qeaf_sim {
    uint32_t width, height;       /* app canvas */
    const qeaf_device_profile_t *device;
    vio_panel_t panel;            /* valid only when device != NULL */
    int has_device;
    uint32_t *pixels;

    uint8_t *arena;
    size_t arena_size;
    uint32_t code_size;
    void *handle;

    qeapp_create_fn fn_create;
    qeapp_lifecycle_fn fn_start, fn_pause, fn_resume, fn_stop;
    qeapp_destroy_fn fn_destroy;
    qeapp_input_fn fn_input;

    qeaf_app_api_v2 api;
    qeaf_app_api_v3 api_v3;
    qeaf_app_api_v4 api_v4;
    qeaf_app_api_v5 api_v5;
    int api_version;             /* 2, 3, 4 or 5: which table the module imports */

    qeaf_broker_t broker;
    int call_timeout;
    uint32_t max_call_ms;
    char state[16];

    size_t blit_count;           /* api_blit_pixels calls that drew */
    int blit_x, blit_y;          /* last blit placement */
    int blit_w, blit_h;          /* last blit size */

    sim_split_t splits[QEAF_SIM_MAX_SPLITS];
    size_t split_count;          /* api_layout_split_weighted calls (all rc) */

    char trace[QEAF_SIM_MAX_TRACE][16];
    size_t trace_count;
    char log[QEAF_SIM_MAX_LOG][256];
    size_t log_count;
    sim_draw_t draws[QEAF_SIM_MAX_DRAWS];
    size_t draw_count;

    qeaf_sim_file_t files[QEAF_SIM_MAX_FILES];
    size_t file_count;
    qeaf_input_event_t input[QEAF_SIM_MAX_INPUT];
    size_t input_count;
    size_t input_delivered;

    sim_alloc_t allocs[QEAF_SIM_MAX_ALLOCS];
    size_t alloc_count;
};

/* The API table has no context argument, so callbacks reach the active
 * simulator through this single-app global (the host runs one app at a time). */
static qeaf_sim_t *g_active;

/* Monotonic wall clock for the per-call CPU-time quota. QPC gives sub-ms
 * resolution on Windows (GetTickCount64 is too coarse for a small budget). */
static uint64_t now_ms(void)
{
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000) / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
#endif
}

/* Record a finished native call's duration against the per-call budget. A
 * finite but slow call overruns here; an infinite hang never returns and is
 * killed by the supervisor's whole-run timeout (Phase 4). */
static void call_done(qeaf_sim_t *sim, uint64_t start)
{
    uint64_t ms = now_ms() - start;
    if(ms > sim->max_call_ms) sim->max_call_ms = (uint32_t)ms;
    if(ms > sim->broker.call_budget_ms) sim->call_timeout = 1;
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void *arena_alloc(size_t size)
{
#ifdef _WIN32
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE,
                        PAGE_EXECUTE_READWRITE);
#else
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return ptr == MAP_FAILED ? NULL : ptr;
#endif
}

static void arena_release(void *ptr, size_t size)
{
#ifdef _WIN32
    (void)size;
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, size);
#endif
}

static void trace_add(qeaf_sim_t *sim, const char *event)
{
    if(sim->trace_count < QEAF_SIM_MAX_TRACE)
        snprintf(sim->trace[sim->trace_count++], 16, "%s", event);
}

/* ---- fake framework API -------------------------------------------------- */

static void QEAF_APP_CALL api_log(const char *message)
{
    qeaf_sim_t *sim = g_active;
    if(sim == NULL || message == NULL) return;
    if(sim->log_count < QEAF_SIM_MAX_LOG)
        snprintf(sim->log[sim->log_count++], 256, "%s", message);
}

static void raster_text(qeaf_sim_t *sim, int x, int y, const char *utf8,
                        uint32_t weight)
{
    int bold = (weight == QEAF_TEXT_WEIGHT_BOLD);
    int cx = x;
    for(const unsigned char *p = (const unsigned char *)utf8; *p; p++) {
        unsigned char c = *p < 128 ? *p : (unsigned char)'?';
        const uint8_t *glyph = vqeaf_font8x8[c];
        for(int row = 0; row < 8; row++) {
            for(int col = 0; col < 8; col++) {
                if(!(glyph[row] & (1u << col))) continue;
                int px = cx + col, py = y + row;
                if(px >= 0 && py >= 0 &&
                   (uint32_t)px < sim->width && (uint32_t)py < sim->height)
                    sim->pixels[(uint32_t)py * sim->width + (uint32_t)px] = 0xFFFFFFFFu;
                /* Bold weight: the 8x8 bitmap face has no separate bold cut,
                 * so dilate each glyph pixel one column right for a heavier
                 * raster. This is the QEAPP bold weight, distinct from the
                 * shell's LVGL bold faces. */
                if(bold && px + 1 >= 0 && py >= 0 &&
                   (uint32_t)(px + 1) < sim->width && (uint32_t)py < sim->height)
                    sim->pixels[(uint32_t)py * sim->width + (uint32_t)(px + 1)] = 0xFFFFFFFFu;
            }
        }
        cx += 8;
    }
}

static int QEAF_APP_CALL api_draw_text(int x, int y, const char *utf8)
{
    qeaf_sim_t *sim = g_active;
    if(sim == NULL || utf8 == NULL) return -1;
    if(!qeaf_broker_check(&sim->broker, QEAF_PERM_UI_DISPLAY)) return -1;
    if(sim->draw_count < QEAF_SIM_MAX_DRAWS) {
        sim_draw_t *d = &sim->draws[sim->draw_count++];
        d->x = x;
        d->y = y;
        d->weight = QEAF_TEXT_WEIGHT_REGULAR;
        snprintf(d->text, sizeof(d->text), "%s", utf8);
    }
    raster_text(sim, x, y, utf8, QEAF_TEXT_WEIGHT_REGULAR);
    return 0;
}

static int QEAF_APP_CALL api_request_permission(const char *permission)
{
    qeaf_sim_t *sim = g_active;
    if(sim == NULL) return 0;
    uint32_t bit = qeaf_permission_bit(permission);
    return qeaf_broker_request(&sim->broker, bit);
}

static int QEAF_APP_CALL api_read_file(const char *path, void **out, size_t *size)
{
    qeaf_sim_t *sim = g_active;
    if(sim == NULL || path == NULL || out == NULL || size == NULL) return -1;
    if(!qeaf_broker_check(&sim->broker, QEAF_PERM_STORAGE_READ)) return -1;
    if(!qeaf_broker_check_path(&sim->broker, path, 0)) return -1;
    for(size_t i = 0; i < sim->file_count; i++) {
        if(strcmp(sim->files[i].path, path) != 0) continue;
        if(sim->alloc_count >= QEAF_SIM_MAX_ALLOCS) return -1;
        if(!qeaf_broker_mem_acquire(&sim->broker, sim->files[i].size)) return -1;
        void *buf = malloc(sim->files[i].size ? sim->files[i].size : 1);
        if(buf == NULL) {
            qeaf_broker_mem_release(&sim->broker, sim->files[i].size);
            return -1;
        }
        memcpy(buf, sim->files[i].data, sim->files[i].size);
        sim->allocs[sim->alloc_count].ptr = buf;
        sim->allocs[sim->alloc_count].size = sim->files[i].size;
        sim->alloc_count++;
        *out = buf;
        *size = sim->files[i].size;
        return 0;
    }
    return -1;
}

static void QEAF_APP_CALL api_free(void *ptr)
{
    qeaf_sim_t *sim = g_active;
    if(sim == NULL || ptr == NULL) return;
    for(size_t i = 0; i < sim->alloc_count; i++) {
        if(sim->allocs[i].ptr != ptr) continue;
        qeaf_broker_mem_release(&sim->broker, sim->allocs[i].size);
        free(ptr);
        sim->allocs[i] = sim->allocs[sim->alloc_count - 1];
        sim->alloc_count--;
        return;
    }
}

/* ---- framework API v3 additions ------------------------------------------ */

static int QEAF_APP_CALL api_blit_pixels(int x, int y, int w, int h,
                                         int stride, const uint16_t *rgb565)
{
    qeaf_sim_t *sim = g_active;
    if(sim == NULL || rgb565 == NULL) return -1;
    if(!qeaf_broker_check(&sim->broker, QEAF_PERM_UI_DISPLAY)) return -1;
    if(w <= 0 || h <= 0 || stride < w) return -1;
    for(int row = 0; row < h; row++) {
        int py = y + row;
        if(py < 0 || py >= (int)sim->height) continue;
        for(int col = 0; col < w; col++) {
            int px = x + col;
            if(px < 0 || px >= (int)sim->width) continue;
            uint16_t c = rgb565[(size_t)row * (size_t)stride + (size_t)col];
            uint32_t r = ((c >> 11) & 0x1Fu) * 255u / 31u;
            uint32_t g = ((c >> 5) & 0x3Fu) * 255u / 63u;
            uint32_t b = (c & 0x1Fu) * 255u / 31u;
            sim->pixels[(uint32_t)py * sim->width + (uint32_t)px] =
                0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
    sim->blit_count++;
    sim->blit_x = x;
    sim->blit_y = y;
    sim->blit_w = w;
    sim->blit_h = h;
    return 0;
}

static int QEAF_APP_CALL api_read_file_part(const char *path, uint32_t offset,
                                            size_t size, void *out)
{
    qeaf_sim_t *sim = g_active;
    if(sim == NULL || path == NULL || out == NULL || size == 0) return -1;
    if(!qeaf_broker_check(&sim->broker, QEAF_PERM_STORAGE_READ)) return -1;
    if(!qeaf_broker_check_path(&sim->broker, path, 0)) return -1;
    for(size_t i = 0; i < sim->file_count; i++) {
        if(strcmp(sim->files[i].path, path) != 0) continue;
        if((size_t)offset > sim->files[i].size) return -1;
        if(size > sim->files[i].size - (size_t)offset) return -1;
        /* Transient charge: the caller's buffer exists only for this call. */
        if(!qeaf_broker_mem_acquire(&sim->broker, size)) return -1;
        memcpy(out, sim->files[i].data + offset, size);
        qeaf_broker_mem_release(&sim->broker, size);
        return 0;
    }
    return -1;
}

/* ---- framework API v4 additions ------------------------------------------ */

static int QEAF_APP_CALL api_draw_text_weighted(int x, int y, const char *utf8,
                                                uint32_t weight)
{
    qeaf_sim_t *sim = g_active;
    if(sim == NULL || utf8 == NULL) return -1;
    if(weight != QEAF_TEXT_WEIGHT_REGULAR && weight != QEAF_TEXT_WEIGHT_BOLD)
        return -1;
    if(!qeaf_broker_check(&sim->broker, QEAF_PERM_UI_DISPLAY)) return -1;
    if(sim->draw_count < QEAF_SIM_MAX_DRAWS) {
        sim_draw_t *d = &sim->draws[sim->draw_count++];
        d->x = x;
        d->y = y;
        d->weight = weight;
        snprintf(d->text, sizeof(d->text), "%s", utf8);
    }
    raster_text(sim, x, y, utf8, weight);
    return 0;
}

/* ---- framework API v5 additions ------------------------------------------ */

/* Pure proportional layout split. No permission and no display side effect,
 * so it succeeds even for an app without ui.display; the shared inline keeps
 * the host pixel-identical to the device runtime and the arm64 harness. Every
 * call (including rejected ones) is recorded for the deterministic gate. */
static int QEAF_APP_CALL api_layout_split_weighted(int x, int y, int w, int h,
                                                   uint32_t axis,
                                                   const uint32_t *weights,
                                                   uint32_t n, uint32_t gap,
                                                   qeaf_rect_t *out)
{
    qeaf_sim_t *sim = g_active;
    if(sim == NULL) return -1;
    int rc = qeaf_layout_split_rect(x, y, w, h, axis, weights, n, gap, out);
    if(sim->split_count < QEAF_SIM_MAX_SPLITS) {
        sim_split_t *s = &sim->splits[sim->split_count++];
        s->rc = rc;
        s->n = n;
        memset(s->rects, 0, sizeof(s->rects));
        if(rc == 0 && out != NULL) {
            uint32_t keep = n < QEAF_SIM_MAX_SPLIT_RECTS ? n : QEAF_SIM_MAX_SPLIT_RECTS;
            for(uint32_t i = 0; i < keep; i++) s->rects[i] = out[i];
        }
    }
    return rc;
}

/* ---- module loading ------------------------------------------------------ */

static int resolve_exports(qeaf_sim_t *sim, const uint8_t *module,
                           char *error, size_t cap)
{
    uint32_t code_size = le32(module + 21);
    uint32_t rodata_size = le32(module + 25);
    uint32_t data_size = le32(module + 29);
    uint32_t imports_size = le32(module + 41);
    uint32_t exports_size = le32(module + 45);
    size_t exports_off = VQEAF_QEN_HEADER_SIZE + code_size + rodata_size +
                         data_size + imports_size;
    size_t end = exports_off + exports_size;
    size_t i = exports_off;
    while(i < end) {
        size_t j = i;
        while(j < end && module[j] != 0) j++;
        if(j >= end || j == i || j + 5 > end) {
            snprintf(error, cap, "exports table malformed");
            return -1;
        }
        const char *name = (const char *)(module + i);
        uint32_t offset = le32(module + j + 1);
        void *fn = sim->arena + offset;
        if(strcmp(name, QEAPP_EXPORT_CREATE) == 0) sim->fn_create = (qeapp_create_fn)fn;
        else if(strcmp(name, QEAPP_EXPORT_START) == 0) sim->fn_start = (qeapp_lifecycle_fn)fn;
        else if(strcmp(name, QEAPP_EXPORT_PAUSE) == 0) sim->fn_pause = (qeapp_lifecycle_fn)fn;
        else if(strcmp(name, QEAPP_EXPORT_RESUME) == 0) sim->fn_resume = (qeapp_lifecycle_fn)fn;
        else if(strcmp(name, QEAPP_EXPORT_STOP) == 0) sim->fn_stop = (qeapp_lifecycle_fn)fn;
        else if(strcmp(name, QEAPP_EXPORT_DESTROY) == 0) sim->fn_destroy = (qeapp_destroy_fn)fn;
        else if(strcmp(name, QEAPP_EXPORT_ON_INPUT) == 0) sim->fn_input = (qeapp_input_fn)fn;
        i = j + 5;
    }
    if(sim->fn_create == NULL || sim->fn_start == NULL ||
       sim->fn_stop == NULL || sim->fn_destroy == NULL) {
        snprintf(error, cap, "module missing required exports "
                 "(create/start/stop/destroy)");
        return -1;
    }
    return 0;
}

static int check_imports(const uint8_t *module, int *api_version_out,
                         char *error, size_t cap)
{
    uint32_t code_size = le32(module + 21);
    uint32_t rodata_size = le32(module + 25);
    uint32_t data_size = le32(module + 29);
    uint32_t imports_size = le32(module + 41);
    uint8_t abi_version = module[10];
    size_t off = VQEAF_QEN_HEADER_SIZE + code_size + rodata_size + data_size;
    size_t end = off + imports_size;
    int found_v2 = 0, found_v3 = 0, found_v4 = 0, found_v5 = 0;
    size_t i = off;
    while(i < end) {
        size_t j = i;
        while(j < end && module[j] != 0) j++;
        if(j >= end || j == i) {
            snprintf(error, cap, "imports table malformed");
            return -1;
        }
        const char *name = (const char *)(module + i);
        if(strcmp(name, QEAPP_IMPORT_API_V2) == 0) found_v2 = 1;
        else if(strcmp(name, QEAPP_IMPORT_API_V3) == 0) found_v3 = 1;
        else if(strcmp(name, QEAPP_IMPORT_API_V4) == 0) found_v4 = 1;
        else if(strcmp(name, QEAPP_IMPORT_API_V5) == 0) found_v5 = 1;
        else {
            snprintf(error, cap, "unknown framework import: %s", name);
            return -1;
        }
        i = j + 1;
    }
    if(found_v2 + found_v3 + found_v4 + found_v5 != 1) {
        snprintf(error, cap, "module must import exactly one of %s / %s / %s / %s",
                 QEAPP_IMPORT_API_V2, QEAPP_IMPORT_API_V3, QEAPP_IMPORT_API_V4,
                 QEAPP_IMPORT_API_V5);
        return -1;
    }
    *api_version_out = found_v5 ? 5 : (found_v4 ? 4 : (found_v3 ? 3 : 2));
    if(abi_version != 0 && abi_version != (uint8_t)*api_version_out) {
        snprintf(error, cap, "module header abi_version %u does not match "
                 "its import (api v%d)", (unsigned)abi_version, *api_version_out);
        return -1;
    }
    return 0;
}

qeaf_sim_t *qeaf_sim_new(const qeaf_sim_config_t *cfg, char *error, size_t cap)
{
    vqeaf_qeapp_native_info_t info;
    const uint8_t *module = NULL;
    size_t module_size = 0;
    qeaf_sim_t *sim;

    if(cfg == NULL || cfg->package == NULL || cfg->package_size == 0) {
        snprintf(error, cap, "simulator: no package");
        return NULL;
    }
    if(vqeaf_qeapp_inspect_native(cfg->package, cfg->package_size, &info,
                                  &module, &module_size, error, cap) != 0)
        return NULL;
    if(info.arch_id != VQEAF_QEN_ARCH_X86_64) {
        snprintf(error, cap, "host simulator executes x86_64 modules only "
                 "(package is %s)", info.arch);
        return NULL;
    }
    if(vqeaf_qeapp_native_validate_module(module, module_size,
                                          VQEAF_QEN_ARCH_X86_64) != 0) {
        snprintf(error, cap, "native module failed validation");
        return NULL;
    }
    uint32_t code_size = le32(module + 21);
    uint32_t rodata_size = le32(module + 25);
    uint32_t data_size = le32(module + 29);
    uint32_t bss_size = le32(module + 33);
    uint16_t min_api = le16(module + 53);
    if(code_size == 0) {
        snprintf(error, cap, "module has an empty code section (stub); "
                 "nothing to execute");
        return NULL;
    }
    if(min_api > VQEAF_APP_API_V5_VERSION) {
        snprintf(error, cap, "module requires framework API %u > %u",
                 min_api, VQEAF_APP_API_V5_VERSION);
        return NULL;
    }
    int api_version = 0;
    if(check_imports(module, &api_version, error, cap) != 0) return NULL;

    sim = calloc(1, sizeof(*sim));
    if(sim == NULL) {
        snprintf(error, cap, "simulator: out of memory");
        return NULL;
    }
    sim->device = cfg->device;
    sim->width = cfg->width ? cfg->width
               : (cfg->device ? cfg->device->canvas_width : SIM_DEFAULT_W);
    sim->height = cfg->height ? cfg->height
                : (cfg->device ? cfg->device->canvas_height : SIM_DEFAULT_H);
    size_t memory_budget = cfg->memory_budget;
    uint32_t call_budget_ms = cfg->call_budget_ms;
    if(cfg->device != NULL) {
        if(memory_budget == 0) memory_budget = cfg->device->memory_budget;
        if(call_budget_ms == 0) call_budget_ms = cfg->device->call_budget_ms;
        vio_panel_init(&sim->panel, (int)sim->width, (int)sim->height,
                       (int)cfg->device->panel_width,
                       (int)cfg->device->panel_height,
                       (int)cfg->device->rotation);
        sim->has_device = 1;
    }
    qeaf_broker_init(&sim->broker, info.permissions, cfg->scope,
                     memory_budget, call_budget_ms);
    sim->pixels = calloc((size_t)sim->width * sim->height, sizeof(uint32_t));
    sim->api_version = api_version;
    sim->arena_size = (size_t)code_size + rodata_size + data_size + bss_size;
    if(sim->arena_size > QEAF_SIM_MAX_ARENA) {
        snprintf(error, cap, "module arena %zu bytes exceeds bound %u",
                 sim->arena_size, (unsigned)QEAF_SIM_MAX_ARENA);
        qeaf_sim_free(sim);
        return NULL;
    }
    sim->arena = arena_alloc(sim->arena_size);
    sim->code_size = code_size;
    if(sim->pixels == NULL || sim->arena == NULL) {
        snprintf(error, cap, "simulator: arena/display allocation failed");
        qeaf_sim_free(sim);
        return NULL;
    }
    memcpy(sim->arena, module + VQEAF_QEN_HEADER_SIZE, code_size);
    memcpy(sim->arena + code_size, module + VQEAF_QEN_HEADER_SIZE + code_size,
           rodata_size);
    memcpy(sim->arena + code_size + rodata_size,
           module + VQEAF_QEN_HEADER_SIZE + code_size + rodata_size, data_size);
    memset(sim->arena + code_size + rodata_size + data_size, 0, bss_size);

    if(resolve_exports(sim, module, error, cap) != 0) {
        qeaf_sim_free(sim);
        return NULL;
    }

    sim->api.size = (uint32_t)sizeof(qeaf_app_api_v2);
    sim->api.abi_version = VQEAF_APP_API_V2_VERSION;
    sim->api.log_utf8 = api_log;
    sim->api.draw_text = api_draw_text;
    sim->api.request_permission = api_request_permission;
    sim->api.read_file = api_read_file;
    sim->api.free = api_free;

    sim->api_v3.size = (uint32_t)sizeof(qeaf_app_api_v3);
    sim->api_v3.abi_version = VQEAF_APP_API_V3_VERSION;
    sim->api_v3.log_utf8 = api_log;
    sim->api_v3.draw_text = api_draw_text;
    sim->api_v3.request_permission = api_request_permission;
    sim->api_v3.read_file = api_read_file;
    sim->api_v3.free = api_free;
    sim->api_v3.blit_pixels = api_blit_pixels;
    sim->api_v3.read_file_part = api_read_file_part;

    sim->api_v4.size = (uint32_t)sizeof(qeaf_app_api_v4);
    sim->api_v4.abi_version = VQEAF_APP_API_V4_VERSION;
    sim->api_v4.log_utf8 = api_log;
    sim->api_v4.draw_text = api_draw_text;
    sim->api_v4.request_permission = api_request_permission;
    sim->api_v4.read_file = api_read_file;
    sim->api_v4.free = api_free;
    sim->api_v4.blit_pixels = api_blit_pixels;
    sim->api_v4.read_file_part = api_read_file_part;
    sim->api_v4.draw_text_weighted = api_draw_text_weighted;

    sim->api_v5.size = (uint32_t)sizeof(qeaf_app_api_v5);
    sim->api_v5.abi_version = VQEAF_APP_API_V5_VERSION;
    sim->api_v5.log_utf8 = api_log;
    sim->api_v5.draw_text = api_draw_text;
    sim->api_v5.request_permission = api_request_permission;
    sim->api_v5.read_file = api_read_file;
    sim->api_v5.free = api_free;
    sim->api_v5.blit_pixels = api_blit_pixels;
    sim->api_v5.read_file_part = api_read_file_part;
    sim->api_v5.draw_text_weighted = api_draw_text_weighted;
    sim->api_v5.layout_split_weighted = api_layout_split_weighted;

    for(size_t i = 0; i < cfg->file_count && i < QEAF_SIM_MAX_FILES; i++)
        sim->files[sim->file_count++] = cfg->files[i];
    for(size_t i = 0; i < cfg->input_count && i < QEAF_SIM_MAX_INPUT; i++) {
        qeaf_input_event_t event = cfg->input[i];
        /* In device mode the caller feeds raw panel coordinates; the phone's
         * own panel_scale mapping turns them into app canvas coordinates. */
        if(sim->has_device && event.type == QEAF_INPUT_TOUCH) {
            int ui_x = 0, ui_y = 0;
            if(vio_panel_touch_map(&sim->panel, (int)event.x, (int)event.y,
                                   (int)sim->device->panel_width,
                                   (int)sim->device->panel_height,
                                   &ui_x, &ui_y) != 0)
                continue; /* letterbox: there is no app pixel under the touch */
            event.x = (int32_t)ui_x;
            event.y = (int32_t)ui_y;
        }
        sim->input[sim->input_count++] = event;
    }

    snprintf(sim->state, sizeof(sim->state), "loaded");
    return sim;
}

void qeaf_sim_free(qeaf_sim_t *sim)
{
    if(sim == NULL) return;
    for(size_t i = 0; i < sim->alloc_count; i++) free(sim->allocs[i].ptr);
    if(sim->arena != NULL) arena_release(sim->arena, sim->arena_size);
    free(sim->pixels);
    free(sim);
}

/* ---- lifecycle ----------------------------------------------------------- */

static int fail(char *error, size_t cap, const char *msg)
{
    snprintf(error, cap, "%s", msg);
    return -1;
}

int qeaf_sim_create_app(qeaf_sim_t *sim, char *error, size_t cap)
{
    if(sim == NULL) return fail(error, cap, "no simulator");
    if(strcmp(sim->state, "loaded") != 0)
        return fail(error, cap, "create called out of order");
    g_active = sim;
    uint64_t t0 = now_ms();
    const qeaf_app_api_v2 *table =
        sim->api_version == 5 ? (const qeaf_app_api_v2 *)&sim->api_v5
        : sim->api_version == 4 ? (const qeaf_app_api_v2 *)&sim->api_v4
        : sim->api_version == 3 ? (const qeaf_app_api_v2 *)&sim->api_v3
        : &sim->api;
    sim->handle = sim->fn_create(table);
    call_done(sim, t0);
    g_active = NULL;
    if(sim->call_timeout)
        return fail(error, cap, "create() exceeded the per-call CPU budget");
    if(sim->handle == NULL)
        return fail(error, cap, "create() returned NULL");
    snprintf(sim->state, sizeof(sim->state), "created");
    trace_add(sim, "create");
    return 0;
}

static void deliver_input(qeaf_sim_t *sim)
{
    if(sim->fn_input == NULL || !qeaf_broker_check(&sim->broker, QEAF_PERM_INPUT_RECEIVE))
        return;
    g_active = sim;
    for(size_t i = 0; i < sim->input_count; i++) {
        uint64_t t0 = now_ms();
        sim->fn_input(sim->handle, &sim->input[i]);
        call_done(sim, t0);
        sim->input_delivered++;
    }
    g_active = NULL;
}

int qeaf_sim_start(qeaf_sim_t *sim, char *error, size_t cap)
{
    if(sim == NULL) return fail(error, cap, "no simulator");
    if(strcmp(sim->state, "created") != 0)
        return fail(error, cap, "start called out of order");
    g_active = sim;
    uint64_t t0 = now_ms();
    int rc = sim->fn_start(sim->handle);
    call_done(sim, t0);
    g_active = NULL;
    if(sim->call_timeout)
        return fail(error, cap, "start() exceeded the per-call CPU budget");
    if(rc != 0) return fail(error, cap, "start() returned nonzero");
    snprintf(sim->state, sizeof(sim->state), "started");
    trace_add(sim, "start");
    deliver_input(sim);
    if(sim->call_timeout)
        return fail(error, cap, "on_input() exceeded the per-call CPU budget");
    return 0;
}

int qeaf_sim_pause(qeaf_sim_t *sim, char *error, size_t cap)
{
    if(sim == NULL) return fail(error, cap, "no simulator");
    if(strcmp(sim->state, "started") != 0)
        return fail(error, cap, "pause called out of order");
    if(sim->fn_pause != NULL) {
        g_active = sim;
        uint64_t t0 = now_ms();
        int rc = sim->fn_pause(sim->handle);
        call_done(sim, t0);
        g_active = NULL;
        if(sim->call_timeout)
            return fail(error, cap, "pause() exceeded the per-call CPU budget");
        if(rc != 0) return fail(error, cap, "pause() returned nonzero");
    }
    snprintf(sim->state, sizeof(sim->state), "paused");
    trace_add(sim, "pause");
    return 0;
}

int qeaf_sim_resume(qeaf_sim_t *sim, char *error, size_t cap)
{
    if(sim == NULL) return fail(error, cap, "no simulator");
    if(strcmp(sim->state, "paused") != 0)
        return fail(error, cap, "resume called out of order");
    if(sim->fn_resume != NULL) {
        g_active = sim;
        uint64_t t0 = now_ms();
        int rc = sim->fn_resume(sim->handle);
        call_done(sim, t0);
        g_active = NULL;
        if(sim->call_timeout)
            return fail(error, cap, "resume() exceeded the per-call CPU budget");
        if(rc != 0) return fail(error, cap, "resume() returned nonzero");
    }
    snprintf(sim->state, sizeof(sim->state), "started");
    trace_add(sim, "resume");
    return 0;
}

int qeaf_sim_input(qeaf_sim_t *sim, const qeaf_input_event_t *event,
                   char *error, size_t cap)
{
    if(sim == NULL || event == NULL) return fail(error, cap, "no input event");
    if(strcmp(sim->state, "started") != 0)
        return fail(error, cap, "input needs a started app");
    if(sim->fn_input == NULL)
        return fail(error, cap, "app has no on_input export");
    if(!qeaf_broker_check(&sim->broker, QEAF_PERM_INPUT_RECEIVE))
        return fail(error, cap, "input.receive was not granted");
    g_active = sim;
    uint64_t t0 = now_ms();
    sim->fn_input(sim->handle, event);
    call_done(sim, t0);
    g_active = NULL;
    sim->input_delivered++;
    if(sim->call_timeout)
        return fail(error, cap, "on_input() exceeded the per-call CPU budget");
    return 0;
}

int qeaf_sim_stop(qeaf_sim_t *sim, char *error, size_t cap)
{
    if(sim == NULL) return fail(error, cap, "no simulator");
    if(strcmp(sim->state, "started") != 0 && strcmp(sim->state, "paused") != 0)
        return fail(error, cap, "stop called out of order");
    g_active = sim;
    uint64_t t0 = now_ms();
    int rc = sim->fn_stop(sim->handle);
    call_done(sim, t0);
    g_active = NULL;
    if(sim->call_timeout)
        return fail(error, cap, "stop() exceeded the per-call CPU budget");
    if(rc != 0) return fail(error, cap, "stop() returned nonzero");
    snprintf(sim->state, sizeof(sim->state), "stopped");
    trace_add(sim, "stop");
    return 0;
}

int qeaf_sim_destroy_app(qeaf_sim_t *sim, char *error, size_t cap)
{
    if(sim == NULL) return fail(error, cap, "no simulator");
    if(strcmp(sim->state, "stopped") != 0)
        return fail(error, cap, "destroy called out of order");
    g_active = sim;
    uint64_t t0 = now_ms();
    sim->fn_destroy(sim->handle);
    call_done(sim, t0);
    g_active = NULL;
    sim->handle = NULL;
    if(sim->call_timeout)
        return fail(error, cap, "destroy() exceeded the per-call CPU budget");
    snprintf(sim->state, sizeof(sim->state), "destroyed");
    trace_add(sim, "destroy");
    return 0;
}

int qeaf_sim_run(qeaf_sim_t *sim, char *error, size_t cap)
{
    if(qeaf_sim_create_app(sim, error, cap) != 0) return -1;
    if(qeaf_sim_start(sim, error, cap) != 0) return -1;
    if(qeaf_sim_pause(sim, error, cap) != 0) return -1;
    if(qeaf_sim_resume(sim, error, cap) != 0) return -1;
    if(qeaf_sim_stop(sim, error, cap) != 0) return -1;
    if(qeaf_sim_destroy_app(sim, error, cap) != 0) return -1;
    if(sim->call_timeout) {
        snprintf(error, cap, "native call exceeded the per-call CPU budget "
                 "(max %u ms > %u ms)", sim->max_call_ms,
                 sim->broker.call_budget_ms);
        return -1;
    }
    if(sim->alloc_count != 0) {
        snprintf(error, cap, "app leaked %zu allocation(s) after destroy",
                 sim->alloc_count);
        return -1;
    }
    return 0;
}

/* ---- inspection ---------------------------------------------------------- */

const char *qeaf_sim_state(const qeaf_sim_t *sim)
{
    return sim ? sim->state : "";
}

uint32_t qeaf_sim_permissions(const qeaf_sim_t *sim)
{
    return sim ? qeaf_broker_granted(&sim->broker) : 0u;
}

int qeaf_sim_call_timeout(const qeaf_sim_t *sim)
{
    return sim ? sim->call_timeout : 0;
}

uint32_t qeaf_sim_max_call_ms(const qeaf_sim_t *sim)
{
    return sim ? sim->max_call_ms : 0u;
}

uint32_t qeaf_sim_deny_count(const qeaf_sim_t *sim)
{
    return sim ? qeaf_broker_deny_count(&sim->broker) : 0u;
}

size_t qeaf_sim_memory_used(const qeaf_sim_t *sim)
{
    return sim ? qeaf_broker_memory_used(&sim->broker) : 0u;
}

size_t qeaf_sim_audit_count(const qeaf_sim_t *sim)
{
    return sim ? qeaf_broker_audit_count(&sim->broker) : 0u;
}

const char *qeaf_sim_audit_at(const qeaf_sim_t *sim, size_t index)
{
    return sim ? qeaf_broker_audit_at(&sim->broker, index) : NULL;
}

size_t qeaf_sim_outstanding_allocs(const qeaf_sim_t *sim)
{
    return sim ? sim->alloc_count : 0u;
}

uint32_t qeaf_sim_framebuffer_hash(const qeaf_sim_t *sim)
{
    if(sim == NULL || sim->pixels == NULL) return 0u;
    uint32_t value = 0x811C9DC5u;
    const uint8_t *bytes = (const uint8_t *)sim->pixels;
    size_t count = (size_t)sim->width * sim->height * sizeof(uint32_t);
    for(size_t i = 0; i < count; i++) {
        value ^= bytes[i];
        value *= 0x01000193u;
    }
    return value;
}

const uint32_t *qeaf_sim_framebuffer(const qeaf_sim_t *sim)
{
    return sim ? sim->pixels : NULL;
}

uint32_t qeaf_sim_width(const qeaf_sim_t *sim) { return sim ? sim->width : 0u; }
uint32_t qeaf_sim_height(const qeaf_sim_t *sim) { return sim ? sim->height : 0u; }
const qeaf_device_profile_t *qeaf_sim_device(const qeaf_sim_t *sim)
{
    return sim ? sim->device : NULL;
}
uint32_t qeaf_sim_panel_width(const qeaf_sim_t *sim)
{
    return sim && sim->device ? sim->device->panel_width : qeaf_sim_width(sim);
}
uint32_t qeaf_sim_panel_height(const qeaf_sim_t *sim)
{
    return sim && sim->device ? sim->device->panel_height : qeaf_sim_height(sim);
}
size_t qeaf_sim_arena_size(const qeaf_sim_t *sim) { return sim ? sim->arena_size : 0u; }

size_t qeaf_sim_log_count(const qeaf_sim_t *sim) { return sim ? sim->log_count : 0u; }

const char *qeaf_sim_log_at(const qeaf_sim_t *sim, size_t index)
{
    if(sim == NULL || index >= sim->log_count) return NULL;
    return sim->log[index];
}

size_t qeaf_sim_draw_count(const qeaf_sim_t *sim) { return sim ? sim->draw_count : 0u; }

int qeaf_sim_draw_at(const qeaf_sim_t *sim, size_t index, int *x, int *y,
                     char *text, size_t cap)
{
    if(sim == NULL || index >= sim->draw_count) return -1;
    const sim_draw_t *d = &sim->draws[index];
    if(x) *x = d->x;
    if(y) *y = d->y;
    if(text && cap) snprintf(text, cap, "%s", d->text);
    return 0;
}

int qeaf_sim_draw_weight_at(const qeaf_sim_t *sim, size_t index, uint32_t *weight)
{
    if(sim == NULL || index >= sim->draw_count) return -1;
    if(weight) *weight = sim->draws[index].weight;
    return 0;
}

size_t qeaf_sim_trace_count(const qeaf_sim_t *sim) { return sim ? sim->trace_count : 0u; }

const char *qeaf_sim_trace_at(const qeaf_sim_t *sim, size_t index)
{
    if(sim == NULL || index >= sim->trace_count) return NULL;
    return sim->trace[index];
}

size_t qeaf_sim_input_delivered(const qeaf_sim_t *sim)
{
    return sim ? sim->input_delivered : 0u;
}

size_t qeaf_sim_blit_count(const qeaf_sim_t *sim)
{
    return sim ? sim->blit_count : 0u;
}

int qeaf_sim_blit_last(const qeaf_sim_t *sim, int *x, int *y, int *w, int *h)
{
    if(sim == NULL || sim->blit_count == 0) return -1;
    if(x) *x = sim->blit_x;
    if(y) *y = sim->blit_y;
    if(w) *w = sim->blit_w;
    if(h) *h = sim->blit_h;
    return 0;
}

size_t qeaf_sim_split_count(const qeaf_sim_t *sim)
{
    return sim ? sim->split_count : 0u;
}

int qeaf_sim_split_at(const qeaf_sim_t *sim, size_t index, int *rc, uint32_t *n)
{
    if(sim == NULL || index >= sim->split_count) return -1;
    if(rc) *rc = sim->splits[index].rc;
    if(n) *n = sim->splits[index].n;
    return 0;
}

int qeaf_sim_split_rect_at(const qeaf_sim_t *sim, size_t index, uint32_t rect,
                           qeaf_rect_t *out)
{
    if(sim == NULL || index >= sim->split_count) return -1;
    const sim_split_t *s = &sim->splits[index];
    if(rect >= QEAF_SIM_MAX_SPLIT_RECTS || rect >= s->n) return -1;
    if(out) *out = s->rects[rect];
    return 0;
}