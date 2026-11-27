/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_qeapp_device.c - native QEAPP execution on violet.
 *
 * The loader follows docs/QEAPP_NATIVE_ABI_V2.md: validate the signed package
 * and the .qen structure, copy code/rodata/data into a 4096-byte-aligned
 * 2 MiB arena, resolve the lifecycle exports, and call them through the frozen
 * API table for the target architecture. The module never gets a kernel
 * pointer, a file descriptor or a syscall: it only sees the API table below.
 *
 * The run happens in a forked child. The frame lives in a MAP_SHARED mapping
 * created before fork, so a clean child leaves the pixels visible to the shell
 * while a crash or a hang cannot touch them.
 */
#include "vqeaf_qeapp_device.h"

#include "qeaf_api.h"
#include "qeaf_native_abi.h"
#include "vqeaf/font8x8.h"
#include "vqeaf_qeapp.h"
#include "vqeaf_qeapp_native.h"
#include "vqeaf_qeapp_sandbox.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ARENA_CAP (2u * 1024u * 1024u)
#define FRAME_BYTES ((size_t)VQEAF_QEAPP_DEVICE_W * VQEAF_QEAPP_DEVICE_HEIGHT * 4u)

#if defined(__x86_64__)
#define DEVICE_ARCH_ID VQEAF_QEN_ARCH_X86_64
#define DEVICE_ARCH_NAME "x86_64"
#elif defined(__aarch64__)
#define DEVICE_ARCH_ID VQEAF_QEN_ARCH_ARM64
#define DEVICE_ARCH_NAME "arm64"
#else
#error "vqeaf_qeapp_device: unsupported target architecture"
#endif

typedef void *(*create_fn)(const qeaf_app_api_v2 *);
typedef int (*life_fn)(void *);
typedef void (*destroy_fn)(void *);
typedef void (*input_fn)(void *, const qeaf_input_event_t *);

#define DEVICE_RESULT_MAGIC 0x56455152u

typedef struct {
    _Atomic uint32_t magic;
    _Atomic uint32_t sandboxed;
    _Atomic uint32_t uid;
    _Atomic uint32_t gid;
    _Atomic uint32_t completed;
    _Atomic uint32_t hash;
    char lifecycle[128];
} device_result_t;

typedef struct {
    _Atomic uint32_t ready;
    _Atomic uint32_t frame_seq;
    _Atomic uint32_t input_count;
    device_result_t result;
} device_session_control_t;

static uint32_t *g_frame;
static char g_life[128];
static char g_process[128];
static uint32_t g_hash;
static volatile int g_page_pending;
static pid_t g_session_pid;
static int g_session_input_fd = -1;
static int g_session_report_fd = -1;
static device_session_control_t *g_session_control;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t fnv1a32(const uint8_t *data, size_t size)
{
    uint32_t value = 0x811C9DC5u;
    for(size_t i = 0; i < size; i++) {
        value ^= data[i];
        value *= 0x01000193u;
    }
    return value;
}

static void device_result_identity(device_result_t *result)
{
    if(result == NULL) return;
    atomic_store_explicit(&result->uid, (uint32_t)getuid(), memory_order_release);
    atomic_store_explicit(&result->gid, (uint32_t)getgid(), memory_order_release);
    atomic_store_explicit(&result->sandboxed, 1u, memory_order_release);
}

static void device_result_complete(device_result_t *result)
{
    if(result == NULL) return;
    snprintf(result->lifecycle, sizeof(result->lifecycle), "%s", g_life);
    atomic_store_explicit(&result->hash, g_hash, memory_order_release);
    atomic_store_explicit(&result->completed, 1u, memory_order_release);
}

static int device_result_identity_load(const device_result_t *result,
                                       uint32_t *uid, uint32_t *gid)
{
    if(result == NULL || uid == NULL || gid == NULL ||
       atomic_load_explicit(&result->magic, memory_order_acquire) != DEVICE_RESULT_MAGIC ||
       atomic_load_explicit(&result->sandboxed, memory_order_acquire) != 1u)
        return -1;
    *uid = atomic_load_explicit(&result->uid, memory_order_acquire);
    *gid = atomic_load_explicit(&result->gid, memory_order_acquire);
    return 0;
}

static int device_result_load(const device_result_t *result, char *lifecycle,
                              size_t cap, uint32_t *hash, uint32_t *uid,
                              uint32_t *gid)
{
    char shared[sizeof(result->lifecycle)];

    if(cap == 0 || device_result_identity_load(result, uid, gid) != 0 ||
       atomic_load_explicit(&result->completed, memory_order_acquire) != 1u)
        return -1;
    memcpy(shared, result->lifecycle, sizeof(shared));
    shared[sizeof(shared) - 1] = '\0';
    snprintf(lifecycle, cap, "%s", shared);
    *hash = atomic_load_explicit(&result->hash, memory_order_acquire);
    return 0;
}

/* ---- framework API v2, implemented by the phone ------------------------- */

static uint32_t g_permissions;

static void api_log(const char *message)
{
    printf("log: %s\n", message ? message : "(null)");
}

static void raster_text(int x, int y, const char *utf8, uint32_t weight)
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
                if(px >= 0 && py >= 0 && px < VQEAF_QEAPP_DEVICE_W &&
                   py < VQEAF_QEAPP_DEVICE_HEIGHT)
                    g_frame[(uint32_t)py * VQEAF_QEAPP_DEVICE_W + (uint32_t)px] =
                        0xFFFFFFFFu;
                /* Bold weight: the 8x8 bitmap face has no separate bold cut,
                 * so dilate each glyph pixel one column right for a heavier
                 * raster. Same QEAPP bold weight as the host simulator. */
                if(bold && px + 1 >= 0 && py >= 0 &&
                   px + 1 < VQEAF_QEAPP_DEVICE_W && py < VQEAF_QEAPP_DEVICE_HEIGHT)
                    g_frame[(uint32_t)py * VQEAF_QEAPP_DEVICE_W + (uint32_t)(px + 1)] =
                        0xFFFFFFFFu;
            }
        }
        cx += 8;
    }
}

static int api_draw(int x, int y, const char *utf8)
{
    if(!(g_permissions & QEAF_PERM_UI_DISPLAY) || utf8 == NULL || g_frame == NULL)
        return -1;
    raster_text(x, y, utf8, QEAF_TEXT_WEIGHT_REGULAR);
    return 0;
}

static int api_request_permission(const char *permission)
{
    uint32_t bit = qeaf_permission_bit(permission);
    return (bit != 0u && (g_permissions & bit) != 0u) ? 1 : 0;
}

/* No storage capability is granted to the device runtime yet: the child has no
 * rescue-volume handle and this must stay an honest refusal, not a stub that
 * pretends to have read a file. */
static int api_read_file(const char *path, void **out, size_t *size)
{
    (void)path;
    if(out) *out = NULL;
    if(size) *size = 0;
    return -1;
}

static void api_free(void *ptr)
{
    free(ptr);
}

/* ---- framework API v3/v4 additions, implemented by the phone -------------- */

static int api_blit_pixels(int x, int y, int w, int h, int stride,
                           const uint16_t *rgb565)
{
    if(!(g_permissions & QEAF_PERM_UI_DISPLAY) || rgb565 == NULL || g_frame == NULL)
        return -1;
    if(w <= 0 || h <= 0 || stride < w) return -1;
    for(int row = 0; row < h; row++) {
        int py = y + row;
        if(py < 0 || py >= VQEAF_QEAPP_DEVICE_HEIGHT) continue;
        for(int col = 0; col < w; col++) {
            int px = x + col;
            if(px < 0 || px >= VQEAF_QEAPP_DEVICE_W) continue;
            uint16_t c = rgb565[(size_t)row * (size_t)stride + (size_t)col];
            uint32_t r = ((c >> 11) & 0x1Fu) * 255u / 31u;
            uint32_t g = ((c >> 5) & 0x3Fu) * 255u / 63u;
            uint32_t b = (c & 0x1Fu) * 255u / 31u;
            g_frame[(uint32_t)py * VQEAF_QEAPP_DEVICE_W + (uint32_t)px] =
                0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
    return 0;
}

/* No storage capability on the device runtime: an honest refusal, matching
 * api_read_file. The caller's buffer is left untouched. */
static int api_read_file_part(const char *path, uint32_t offset, size_t size,
                              void *out)
{
    (void)path; (void)offset; (void)size; (void)out;
    return -1;
}

static int api_draw_text_weighted(int x, int y, const char *utf8, uint32_t weight)
{
    if(!(g_permissions & QEAF_PERM_UI_DISPLAY) || utf8 == NULL || g_frame == NULL)
        return -1;
    if(weight != QEAF_TEXT_WEIGHT_REGULAR && weight != QEAF_TEXT_WEIGHT_BOLD)
        return -1;
    raster_text(x, y, utf8, weight);
    return 0;
}

/* ---- framework API v5 additions, implemented by the phone ---------------- */

/* Pure proportional layout split: no permission and no framebuffer side
 * effect, so it runs even for an app without ui.display. The shared inline
 * keeps the device pixel-identical to the host simulator and arm64 harness. */
static int api_layout_split_weighted(int x, int y, int w, int h, uint32_t axis,
                                     const uint32_t *weights, uint32_t n,
                                     uint32_t gap, qeaf_rect_t *out)
{
    return qeaf_layout_split_rect(x, y, w, h, axis, weights, n, gap, out);
}

/* ---- module execution ---------------------------------------------------- */

static void report_lifecycle(const char *stage)
{
    size_t used = strlen(g_life);
    if(used + strlen(stage) + 2 >= sizeof(g_life)) return;
    if(used > 0) g_life[used++] = ' ';
    snprintf(g_life + used, sizeof(g_life) - used, "%s", stage);
}

/* Which framework API table the module imports (2, 3, 4 or 5). Mirrors the host
 * simulator's check_imports: exactly one known import, and the header
 * abi_version byte must agree. Returns -1 on a malformed/mismatched table. */
static int device_api_version(const uint8_t *module, uint32_t code,
                              uint32_t rodata, uint32_t data, uint32_t imports,
                              char *error, size_t cap)
{
    uint8_t abi_version = module[10];
    size_t off = 64u + code + rodata + data;
    size_t end = off + imports;
    int found_v2 = 0, found_v3 = 0, found_v4 = 0, found_v5 = 0;
    size_t i = off;
    while(i < end) {
        size_t j = i;
        while(j < end && module[j] != 0u) j++;
        if(j >= end || j == i) {
            snprintf(error, cap, "device runtime: imports table malformed");
            return -1;
        }
        const char *name = (const char *)(module + i);
        if(strcmp(name, QEAPP_IMPORT_API_V2) == 0) found_v2 = 1;
        else if(strcmp(name, QEAPP_IMPORT_API_V3) == 0) found_v3 = 1;
        else if(strcmp(name, QEAPP_IMPORT_API_V4) == 0) found_v4 = 1;
        else if(strcmp(name, QEAPP_IMPORT_API_V5) == 0) found_v5 = 1;
        else {
            snprintf(error, cap, "device runtime: unknown framework import: %s",
                     name);
            return -1;
        }
        i = j + 1;
    }
    if(found_v2 + found_v3 + found_v4 + found_v5 != 1) {
        snprintf(error, cap,
                 "device runtime: module must import exactly one framework API");
        return -1;
    }
    int version = found_v5 ? 5 : (found_v4 ? 4 : (found_v3 ? 3 : 2));
    if(abi_version != 0 && abi_version != (uint8_t)version) {
        snprintf(error, cap, "device runtime: header abi_version %u does not "
                 "match its import (api v%d)", (unsigned)abi_version, version);
        return -1;
    }
    return version;
}

static int device_module_has_input(const uint8_t *module, size_t module_size)
{
    uint32_t code, rodata, data, imports, exports;
    size_t off, end;

    if(module == NULL || module_size < 64u ||
       vqeaf_qeapp_native_validate_module(module, module_size,
                                          DEVICE_ARCH_ID) != 0)
        return -1;
    code = rd32(module + 21);
    rodata = rd32(module + 25);
    data = rd32(module + 29);
    imports = rd32(module + 41);
    exports = rd32(module + 45);
    off = 64u + code + rodata + data + imports;
    end = off + exports;
    while(off < end) {
        size_t name_end = off;
        while(name_end < end && module[name_end] != 0u) name_end++;
        if(name_end >= end || name_end + 5 > end) return -1;
        if(strcmp((const char *)(module + off), QEAPP_EXPORT_ON_INPUT) == 0)
            return 1;
        off = name_end + 5;
    }
    return 0;
}

static int device_exec(const uint8_t *package, size_t size,
                       uint32_t effective_permissions,
                       device_session_control_t *session_control,
                       char *error, size_t cap)
{
    const uint8_t *module = NULL;
    size_t module_size = 0;
    uint8_t *arena = MAP_FAILED;
    uint32_t code, rodata, data, bss, imports, exports, arch;
    size_t total;
    create_fn fn_create = NULL;
    life_fn fn_start = NULL, fn_pause = NULL, fn_resume = NULL, fn_stop = NULL;
    destroy_fn fn_destroy = NULL;
    input_fn fn_input = NULL;
    int session_failed = 0;

    g_life[0] = '\0';
    g_hash = 0;
    memset(g_frame, 0, FRAME_BYTES);

    {
        vqeaf_qeapp_native_info_t info;
        if(vqeaf_qeapp_inspect_native(package, size, &info, &module, &module_size,
                                      error, cap) != 0)
            return -1;
        if(info.arch_id != DEVICE_ARCH_ID) {
            snprintf(error, cap,
                     "device runtime: package architecture is not %s",
                     DEVICE_ARCH_NAME);
            return -1;
        }
        /* Runtime capabilities are the intersection of the signed manifest and persisted user grants. */
        g_permissions = info.permissions & effective_permissions;
    }
    if(vqeaf_qeapp_native_validate_module(module, module_size,
                                          DEVICE_ARCH_ID) != 0) {
        snprintf(error, cap, "device runtime: invalid .qen module");
        return -1;
    }

    arch = module[11];
    code = rd32(module + 21);
    rodata = rd32(module + 25);
    data = rd32(module + 29);
    bss = rd32(module + 33);
    imports = rd32(module + 41);
    exports = rd32(module + 45);
    if(arch != DEVICE_ARCH_ID) {
        snprintf(error, cap, "device runtime: module is not %s", DEVICE_ARCH_NAME);
        return -1;
    }
    total = (size_t)code + rodata + data + bss;
    if(total == 0 || total > ARENA_CAP) {
        snprintf(error, cap, "device runtime: module exceeds the 2 MiB arena");
        return -1;
    }

    arena = mmap(NULL, ARENA_CAP, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(arena == MAP_FAILED) {
        snprintf(error, cap, "device runtime: arena mmap failed (%d)", errno);
        return -1;
    }
    memcpy(arena, module + 64, code);
    memcpy(arena + code, module + 64 + code, rodata);
    memcpy(arena + code + rodata, module + 64 + code + rodata, data);
    memset(arena + code + rodata + data, 0, bss);
    __builtin___clear_cache((char *)arena, (char *)arena + total);

    {
        size_t off = 64u + code + rodata + data + imports;
        size_t end = off + exports;
        while(off < end) {
            size_t name_end = off;
            while(name_end < end && module[name_end] != 0u) name_end++;
            if(name_end >= end || name_end + 5 > end) break;
            const char *name = (const char *)(module + off);
            void *fn = arena + rd32(module + name_end + 1);
            if(strcmp(name, QEAPP_EXPORT_CREATE) == 0) fn_create = (create_fn)fn;
            else if(strcmp(name, QEAPP_EXPORT_START) == 0) fn_start = (life_fn)fn;
            else if(strcmp(name, QEAPP_EXPORT_PAUSE) == 0) fn_pause = (life_fn)fn;
            else if(strcmp(name, QEAPP_EXPORT_RESUME) == 0) fn_resume = (life_fn)fn;
            else if(strcmp(name, QEAPP_EXPORT_STOP) == 0) fn_stop = (life_fn)fn;
            else if(strcmp(name, QEAPP_EXPORT_DESTROY) == 0) fn_destroy = (destroy_fn)fn;
            else if(strcmp(name, QEAPP_EXPORT_ON_INPUT) == 0) fn_input = (input_fn)fn;
            off = name_end + 5;
        }
    }
    if(fn_create == NULL || fn_start == NULL || fn_stop == NULL ||
       fn_destroy == NULL) {
        snprintf(error, cap, "device runtime: module is missing lifecycle exports");
        munmap(arena, ARENA_CAP);
        return -1;
    }

    {
        int api_version = device_api_version(module, code, rodata, data, imports,
                                             error, cap);
        if(api_version < 0) {
            munmap(arena, ARENA_CAP);
            return -1;
        }
        qeaf_app_api_v2 api;
        qeaf_app_api_v3 api_v3;
        qeaf_app_api_v4 api_v4;
        qeaf_app_api_v5 api_v5;
        const qeaf_app_api_v2 *table;
        void *handle;

        api.size = (uint32_t)sizeof(api);
        api.abi_version = VQEAF_APP_API_V2_VERSION;
        api.log_utf8 = api_log;
        api.draw_text = api_draw;
        api.request_permission = api_request_permission;
        api.read_file = api_read_file;
        api.free = api_free;

        api_v3.size = (uint32_t)sizeof(api_v3);
        api_v3.abi_version = VQEAF_APP_API_V3_VERSION;
        api_v3.log_utf8 = api_log;
        api_v3.draw_text = api_draw;
        api_v3.request_permission = api_request_permission;
        api_v3.read_file = api_read_file;
        api_v3.free = api_free;
        api_v3.blit_pixels = api_blit_pixels;
        api_v3.read_file_part = api_read_file_part;

        api_v4.size = (uint32_t)sizeof(api_v4);
        api_v4.abi_version = VQEAF_APP_API_V4_VERSION;
        api_v4.log_utf8 = api_log;
        api_v4.draw_text = api_draw;
        api_v4.request_permission = api_request_permission;
        api_v4.read_file = api_read_file;
        api_v4.free = api_free;
        api_v4.blit_pixels = api_blit_pixels;
        api_v4.read_file_part = api_read_file_part;
        api_v4.draw_text_weighted = api_draw_text_weighted;

        api_v5.size = (uint32_t)sizeof(api_v5);
        api_v5.abi_version = VQEAF_APP_API_V5_VERSION;
        api_v5.log_utf8 = api_log;
        api_v5.draw_text = api_draw;
        api_v5.request_permission = api_request_permission;
        api_v5.read_file = api_read_file;
        api_v5.free = api_free;
        api_v5.blit_pixels = api_blit_pixels;
        api_v5.read_file_part = api_read_file_part;
        api_v5.draw_text_weighted = api_draw_text_weighted;
        api_v5.layout_split_weighted = api_layout_split_weighted;

        table = api_version == 5 ? (const qeaf_app_api_v2 *)&api_v5
              : api_version == 4 ? (const qeaf_app_api_v2 *)&api_v4
              : api_version == 3 ? (const qeaf_app_api_v2 *)&api_v3
              : &api;
        handle = fn_create(table);
        report_lifecycle("create");
        if(handle == NULL) {
            snprintf(error, cap, "device runtime: create() returned no handle");
            munmap(arena, ARENA_CAP);
            return -1;
        }
        if(fn_start(handle) != 0) {
            snprintf(error, cap, "device runtime: start() failed");
            fn_destroy(handle);
            munmap(arena, ARENA_CAP);
            return -1;
        }
        report_lifecycle("start");
        if(session_control != NULL) {
            uint8_t pending[sizeof(qeaf_input_event_t)];
            size_t pending_used = 0;

            if(fn_input == NULL) {
                snprintf(error, cap, "device runtime: session has no on_input export");
                fn_stop(handle);
                fn_destroy(handle);
                munmap(arena, ARENA_CAP);
                return -1;
            }
            atomic_store_explicit(&session_control->frame_seq, 1u,
                                  memory_order_release);
            atomic_store_explicit(&session_control->ready, 1u,
                                  memory_order_release);
            for(;;) {
                ssize_t got = read(STDIN_FILENO, pending + pending_used,
                                   sizeof(pending) - pending_used);
                if(got < 0) {
                    if(errno == EINTR) continue;
                    snprintf(error, cap, "device runtime: session input read failed (%d)",
                             errno);
                    session_failed = 1;
                    break;
                }
                if(got == 0) break;
                pending_used += (size_t)got;
                if(pending_used < sizeof(pending)) continue;
                qeaf_input_event_t event;
                memcpy(&event, pending, sizeof(event));
                pending_used = 0;
                if((event.type != QEAF_INPUT_KEY && event.type != QEAF_INPUT_TOUCH) ||
                   event.pressed > 1u ||
                   (event.type == QEAF_INPUT_TOUCH &&
                    (event.x < 0 || event.y < 0 ||
                     event.x >= VQEAF_QEAPP_DEVICE_W ||
                     event.y >= VQEAF_QEAPP_DEVICE_HEIGHT))) {
                    snprintf(error, cap, "device runtime: invalid session input event");
                    session_failed = 1;
                    break;
                }
                if(g_permissions & QEAF_PERM_INPUT_RECEIVE) {
                    fn_input(handle, &event);
                    atomic_fetch_add_explicit(&session_control->input_count, 1u,
                                              memory_order_relaxed);
                    atomic_fetch_add_explicit(&session_control->frame_seq, 1u,
                                              memory_order_release);
                }
            }
        }
        else {
            if(fn_pause && fn_pause(handle) == 0) report_lifecycle("pause");
            if(fn_resume && fn_resume(handle) == 0) report_lifecycle("resume");
        }
        if(fn_stop(handle) == 0) report_lifecycle("stop");
        fn_destroy(handle);
        report_lifecycle("destroy");
    }

    if(session_failed) {
        munmap(arena, ARENA_CAP);
        return -1;
    }
    g_hash = fnv1a32((const uint8_t *)g_frame, FRAME_BYTES);
    printf("lifecycle: %s\n", g_life);
    printf("framebuffer: %dx%d hash=0x%08x\n", VQEAF_QEAPP_DEVICE_W,
           VQEAF_QEAPP_DEVICE_HEIGHT, g_hash);
    printf("outstanding allocs: 0\n");
    fflush(stdout);
    munmap(arena, ARENA_CAP);
    return 0;
}

/* ---- interactive session launcher --------------------------------------- */

static uint64_t now_ms(void);

static pid_t wait_child(pid_t pid, int *status, int options)
{
    for(;;) {
        pid_t waited = waitpid(pid, status, options);
        if(waited < 0 && errno == EINTR) continue;
        return waited;
    }
}

static void session_drain_report(char *out, size_t cap, size_t *used)
{
    char chunk[1024];
    size_t budget = 16384u;

    while(budget > 0 && g_session_report_fd >= 0) {
        ssize_t got = read(g_session_report_fd, chunk,
                           sizeof(chunk) < budget ? sizeof(chunk) : budget);
        if(got <= 0) break;
        budget -= (size_t)got;
        if(*used + 1 < cap) {
            size_t room = cap - 1 - *used;
            size_t copy = (size_t)got < room ? (size_t)got : room;
            memcpy(out + *used, chunk, copy);
            *used += copy;
        }
    }
}

static int session_load_identity(pid_t pid)
{
    uint32_t uid, gid;

    if(g_session_control == NULL ||
       device_result_identity_load(&g_session_control->result, &uid, &gid) != 0)
        return -1;
    snprintf(g_process, sizeof(g_process),
             "app pid %d · shell pid %d · app uid %u · app gid %u · seccomp",
             (int)pid, (int)getpid(), uid, gid);
    return 0;
}

static int session_finish(int status)
{
    uint32_t hash, uid, gid;
    int completed = 0;

    if(g_session_input_fd >= 0) {
        close(g_session_input_fd);
        g_session_input_fd = -1;
    }
    if(g_session_report_fd >= 0) {
        close(g_session_report_fd);
        g_session_report_fd = -1;
    }
    if(g_session_control != NULL) {
        completed = device_result_load(&g_session_control->result, g_life,
                                       sizeof(g_life), &hash, &uid, &gid) == 0;
        if(completed) g_hash = hash;
        munmap(g_session_control, sizeof(*g_session_control));
        g_session_control = NULL;
    }
    g_session_pid = -1;
    if(WIFSIGNALED(status)) return -2;
    if(!completed || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;
    return 1;
}

int vqeaf_qeapp_device_session_start(const uint8_t *package, size_t size,
                                     uint32_t effective_permissions,
                                     char *error, size_t error_cap)
{
    int input_pipe[2] = {-1, -1};
    int report_pipe[2] = {-1, -1};
    vqeaf_qeapp_native_info_t info;
    const uint8_t *module = NULL;
    size_t module_size = 0;
    device_session_control_t *control = NULL;
    pid_t pid;
    int status = 0;
    int has_input;
    char out[8192];
    size_t used = 0;
    uint64_t deadline;

    if(error == NULL || error_cap == 0) return -1;
    error[0] = '\0';
    if(package == NULL || size == 0) {
        snprintf(error, error_cap, "device runtime: empty package");
        return -1;
    }
    vqeaf_qeapp_device_session_stop();
    if(vqeaf_qeapp_inspect_native(package, size, &info, &module, &module_size,
                                  error, error_cap) != 0)
        return -1;
    if(info.arch_id != DEVICE_ARCH_ID) {
        snprintf(error, error_cap,
                 "device runtime: package architecture is not %s",
                 DEVICE_ARCH_NAME);
        return -1;
    }
    has_input = device_module_has_input(module, module_size);
    if(has_input < 0) {
        snprintf(error, error_cap, "device runtime: invalid .qen module");
        return -1;
    }
    if(has_input == 0) return 1;
    if(g_frame == NULL) {
        void *map = mmap(NULL, FRAME_BYTES, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if(map == MAP_FAILED) {
            snprintf(error, error_cap, "device runtime: frame mmap failed (%d)",
                     errno);
            return -1;
        }
        g_frame = (uint32_t *)map;
    }
    memset(g_frame, 0, FRAME_BYTES);
    g_life[0] = '\0';
    g_hash = 0;
    g_process[0] = '\0';
    control = mmap(NULL, sizeof(*control), PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(control == MAP_FAILED) {
        snprintf(error, error_cap, "device runtime: session mmap failed (%d)", errno);
        return -1;
    }
    memset(control, 0, sizeof(*control));
    atomic_store_explicit(&control->result.magic, DEVICE_RESULT_MAGIC,
                          memory_order_release);
    if(pipe(input_pipe) != 0 || pipe(report_pipe) != 0) {
        snprintf(error, error_cap, "device runtime: session pipe failed (%d)", errno);
        if(input_pipe[0] >= 0) { close(input_pipe[0]); close(input_pipe[1]); }
        if(report_pipe[0] >= 0) { close(report_pipe[0]); close(report_pipe[1]); }
        munmap(control, sizeof(*control));
        return -1;
    }
    pid = fork();
    if(pid < 0) {
        snprintf(error, error_cap, "device runtime: fork failed (%d)", errno);
        close(input_pipe[0]); close(input_pipe[1]);
        close(report_pipe[0]); close(report_pipe[1]);
        munmap(control, sizeof(*control));
        return -1;
    }
    if(pid == 0) {
        char child_error[256];
        close(input_pipe[1]);
        close(report_pipe[0]);
        dup2(input_pipe[0], STDIN_FILENO);
        dup2(report_pipe[1], STDOUT_FILENO);
        dup2(report_pipe[1], STDERR_FILENO);
        close(input_pipe[0]);
        close(report_pipe[1]);
        child_error[0] = '\0';
        if(vqeaf_qeapp_sandbox_apply(child_error, sizeof(child_error)) != 0) {
            printf("error: %s\n", child_error[0] ? child_error : "sandbox");
            fflush(stdout);
            _exit(1);
        }
        device_result_identity(&control->result);
        printf("identity: uid=%lu gid=%lu\n",
               (unsigned long)getuid(), (unsigned long)getgid());
        int rc = device_exec(package, size, effective_permissions, control,
                             child_error, sizeof(child_error));
        if(rc == 0) device_result_complete(&control->result);
        else
            printf("error: %s\n", child_error[0] ? child_error : "unknown");
        fflush(stdout);
        _exit(rc == 0 ? 0 : 1);
    }
    close(input_pipe[0]);
    close(report_pipe[1]);
    (void)fcntl(input_pipe[1], F_SETFL,
                fcntl(input_pipe[1], F_GETFL) | O_NONBLOCK);
    (void)fcntl(report_pipe[0], F_SETFL,
                fcntl(report_pipe[0], F_GETFL) | O_NONBLOCK);
    g_session_pid = pid;
    g_session_input_fd = input_pipe[1];
    g_session_report_fd = report_pipe[0];
    g_session_control = control;
    out[0] = '\0';
    snprintf(g_process, sizeof(g_process),
             "app pid %d · shell pid %d · app uid %u · seccomp",
             (int)pid, (int)getpid(), vqeaf_qeapp_sandbox_uid());
    deadline = now_ms() + VQEAF_QEAPP_DEVICE_TIMEOUT_MS;
    for(;;) {
        session_drain_report(out, sizeof(out), &used);
        if(atomic_load_explicit(&control->ready, memory_order_acquire) != 0u)
            break;
        pid_t waited = wait_child(pid, &status, WNOHANG);
        if(waited == pid || waited < 0) {
            session_drain_report(out, sizeof(out), &used);
            out[used] = '\0';
            const char *why = strstr(out, "error:");
            snprintf(error, error_cap, "device runtime: session startup failed%s%s",
                     why ? ": " : "", why ? why + 6 : "");
            (void)session_finish(status);
            return -1;
        }
        if(now_ms() > deadline) {
            kill(pid, SIGKILL);
            (void)wait_child(pid, &status, 0);
            session_drain_report(out, sizeof(out), &used);
            out[used] = '\0';
            (void)session_finish(status);
            snprintf(error, error_cap,
                     "device runtime: session exceeded %d ms during startup",
                     VQEAF_QEAPP_DEVICE_TIMEOUT_MS);
            return -1;
        }
        usleep(2000);
    }
    if(session_load_identity(pid) != 0) {
        kill(pid, SIGKILL);
        (void)wait_child(pid, &status, 0);
        (void)session_finish(status);
        snprintf(error, error_cap, "device runtime: missing sandbox identity evidence");
        return -1;
    }
    g_page_pending = 1;
    return 0;
}

int vqeaf_qeapp_device_session_send(const vqeaf_qeapp_device_input_t *event)
{
    const qeaf_input_event_t wire = {
        event ? event->type : 0u, event ? event->code : 0u,
        event ? event->x : 0, event ? event->y : 0,
        event ? event->pressed : 0u
    };
    ssize_t wrote;

    if(event == NULL || g_session_pid <= 0 || g_session_input_fd < 0)
        return -1;
    if((wire.type != QEAF_INPUT_KEY && wire.type != QEAF_INPUT_TOUCH) ||
       wire.pressed > 1u ||
       (wire.type == QEAF_INPUT_TOUCH &&
        (wire.x < 0 || wire.y < 0 || wire.x >= VQEAF_QEAPP_DEVICE_W ||
         wire.y >= VQEAF_QEAPP_DEVICE_HEIGHT)))
        return -1;
    do {
        wrote = write(g_session_input_fd, &wire, sizeof(wire));
    } while(wrote < 0 && errno == EINTR);
    return wrote == (ssize_t)sizeof(wire) ? 0 : -1;
}

int vqeaf_qeapp_device_session_poll(char *error, size_t error_cap)
{
    char out[8192];
    size_t used = 0;
    int status = 0;
    pid_t waited;

    if(error != NULL && error_cap > 0) error[0] = '\0';
    if(g_session_pid <= 0) return 0;
    out[0] = '\0';
    session_drain_report(out, sizeof(out), &used);
    waited = wait_child(g_session_pid, &status, WNOHANG);
    if(waited == 0) return 0;
    if(waited < 0) {
        snprintf(error, error_cap, "device runtime: session wait failed (%d)", errno);
        (void)session_finish(status);
        return -1;
    }
    session_drain_report(out, sizeof(out), &used);
    out[used] = '\0';
    int result = session_finish(status);
    if(result == 1) return 1;
    if(result == -2) {
        snprintf(error, error_cap,
                 "device runtime: module crashed (signal %d); shell survived",
                 WTERMSIG(status));
        return -1;
    }
    {
        const char *why = strstr(out, "error:");
        snprintf(error, error_cap, "device runtime: module session failed%s%s",
                 why ? ": " : "", why ? why + 6 : "");
    }
    return -1;
}

void vqeaf_qeapp_device_session_stop(void)
{
    char out[8192];
    size_t used = 0;
    int status = 0;
    uint64_t deadline;

    if(g_session_pid <= 0) return;
    out[0] = '\0';
    if(g_session_input_fd >= 0) {
        close(g_session_input_fd);
        g_session_input_fd = -1;
    }
    deadline = now_ms() + VQEAF_QEAPP_SESSION_STOP_MS;
    for(;;) {
        session_drain_report(out, sizeof(out), &used);
        pid_t waited = wait_child(g_session_pid, &status, WNOHANG);
        if(waited == g_session_pid || waited < 0) break;
        if(now_ms() > deadline) {
            kill(g_session_pid, SIGKILL);
            (void)wait_child(g_session_pid, &status, 0);
            break;
        }
        usleep(2000);
    }
    session_drain_report(out, sizeof(out), &used);
    out[used] = '\0';
    (void)session_finish(status);
}

int vqeaf_qeapp_device_session_active(void)
{
    return g_session_pid > 0;
}

uint32_t vqeaf_qeapp_device_session_frame_seq(void)
{
    if(g_session_control == NULL) return 0u;
    return atomic_load_explicit(&g_session_control->frame_seq,
                                memory_order_acquire);
}

/* ---- guarded launcher ---------------------------------------------------- */

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

int vqeaf_qeapp_device_launch(const uint8_t *package, size_t size,
                              uint32_t effective_permissions,
                              char *report, size_t report_cap,
                              char *error, size_t error_cap)
{
    int pipefd[2];
    pid_t pid;
    int status = 0;
    int timed_out = 0;
    int wait_failed = 0;
    size_t used = 0;
    uint32_t uid = 0, gid = 0, hash = 0;
    device_result_t *result = NULL;
    char out[4096];

    if(report == NULL || report_cap == 0) return -1;
    report[0] = '\0';
    if(error == NULL || error_cap == 0) return -1;
    error[0] = '\0';
    if(package == NULL || size == 0) {
        snprintf(error, error_cap, "device runtime: empty package");
        return -1;
    }
    /* SECURITY GATE: fork inherits the shell UID, and API-table checks alone
     * cannot constrain direct syscalls. The child therefore drops to an
     * unprivileged uid/gid, closes every inherited descriptor and installs a
     * seccomp allowlist before any module code runs; if any layer is refused
     * the app stays blocked (vqeaf_qeapp_sandbox_apply() is fail-closed). */
    if(g_frame == NULL) {
        void *map = mmap(NULL, FRAME_BYTES, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if(map == MAP_FAILED) {
            snprintf(error, error_cap, "device runtime: frame mmap failed (%d)",
                     errno);
            return -1;
        }
        g_frame = (uint32_t *)map;
    }
    result = mmap(NULL, sizeof(*result), PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(result == MAP_FAILED) {
        snprintf(error, error_cap, "device runtime: result mmap failed (%d)", errno);
        return -1;
    }
    memset(result, 0, sizeof(*result));
    atomic_store_explicit(&result->magic, DEVICE_RESULT_MAGIC, memory_order_release);
    g_process[0] = '\0';
    if(pipe(pipefd) != 0) {
        snprintf(error, error_cap, "device runtime: pipe failed (%d)", errno);
        munmap(result, sizeof(*result));
        return -1;
    }
    pid = fork();
    if(pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        munmap(result, sizeof(*result));
        snprintf(error, error_cap, "device runtime: fork failed (%d)", errno);
        return -1;
    }
    if(pid == 0) {
        char child_error[256];
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        child_error[0] = '\0';
        int rc = vqeaf_qeapp_sandbox_apply(child_error, sizeof(child_error));
        if(rc != 0) {
            printf("error: %s\n", child_error[0] ? child_error : "sandbox");
            fflush(stdout);
            _exit(1);
        }
        device_result_identity(result);
        printf("identity: uid=%lu gid=%lu\n",
               (unsigned long)getuid(), (unsigned long)getgid());
        rc = device_exec(package, size, effective_permissions, NULL,
                         child_error, sizeof(child_error));
        if(rc == 0) device_result_complete(result);
        else
            printf("error: %s\n", child_error[0] ? child_error : "unknown");
        fflush(stdout);
        _exit(rc == 0 ? 0 : 1);
    }
    close(pipefd[1]);
    {
        uint64_t deadline = now_ms() + VQEAF_QEAPP_DEVICE_TIMEOUT_MS;
        for(;;) {
            pid_t waited = wait_child(pid, &status, WNOHANG);
            if(waited == pid) break;
            if(waited < 0) {
                wait_failed = 1;
                kill(pid, SIGKILL);
                (void)wait_child(pid, &status, 0);
                break;
            }
            if(now_ms() > deadline) {
                kill(pid, SIGKILL);
                (void)wait_child(pid, &status, 0);
                timed_out = 1;
                break;
            }
            usleep(10000);
        }
    }
    (void)fcntl(pipefd[0], F_SETFL,
                fcntl(pipefd[0], F_GETFL) | O_NONBLOCK);
    for(;;) {
        ssize_t got;
        if(used + 1 >= sizeof(out)) break;
        got = read(pipefd[0], out + used, sizeof(out) - 1 - used);
        if(got > 0) {
            used += (size_t)got;
            continue;
        }
        if(got < 0 && errno == EINTR) continue;
        break;
    }
    out[used] = '\0';
    close(pipefd[0]);
    if(device_result_identity_load(result, &uid, &gid) == 0) {
        snprintf(g_process, sizeof(g_process),
                 "app pid %d · shell pid %d · app uid %u · app gid %u · seccomp",
                 (int)pid, (int)getpid(), uid, gid);
    }
    snprintf(report, report_cap, "%s%s%s", out, out[0] ? "\n" : "",
             g_process);

    if(timed_out) {
        snprintf(error, error_cap,
                 "device runtime: module exceeded %d ms and was killed%s%s",
                 VQEAF_QEAPP_DEVICE_TIMEOUT_MS, out[0] ? " | " : "", out);
        munmap(result, sizeof(*result));
        return -1;
    }
    if(wait_failed) {
        snprintf(error, error_cap, "device runtime: module wait failed");
        munmap(result, sizeof(*result));
        return -1;
    }
    if(WIFSIGNALED(status)) {
        snprintf(error, error_cap,
                 "device runtime: module crashed (signal %d); shell survived",
                 WTERMSIG(status));
        munmap(result, sizeof(*result));
        return -1;
    }
    if(!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        const char *why = strstr(out, "error:");
        snprintf(error, error_cap, "device runtime: module run failed%s%s",
                 why ? ": " : "", why ? why + 6 : "");
        munmap(result, sizeof(*result));
        return -1;
    }
    if(device_result_load(result, g_life, sizeof(g_life), &hash, &uid, &gid) != 0) {
        snprintf(error, error_cap,
                 "device runtime: module exited before runtime completion");
        munmap(result, sizeof(*result));
        return -1;
    }
    g_hash = hash;
    munmap(result, sizeof(*result));
    g_page_pending = 1;
    return 0;
}

const uint32_t *vqeaf_qeapp_device_frame(void)
{
    return g_frame;
}

const char *vqeaf_qeapp_device_lifecycle(void)
{
    return g_life;
}

const char *vqeaf_qeapp_device_process(void)
{
    return g_process;
}

uint32_t vqeaf_qeapp_device_hash(void)
{
    return g_hash;
}

int vqeaf_qeapp_device_page_pending(void)
{
    return g_page_pending;
}

void vqeaf_qeapp_device_page_ack(void)
{
    g_page_pending = 0;
}
