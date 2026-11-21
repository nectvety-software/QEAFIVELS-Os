#ifndef VQEAF_QEAF_API_H
#define VQEAF_QEAF_API_H
#include <stdint.h>
#include <stddef.h>

#define VQEAF_FRAMEWORK_ABI_MAJOR 1u
#define VQEAF_FRAMEWORK_ABI_MINOR 0u
#define VQEAF_FRAMEWORK_ABI_VERSION ((VQEAF_FRAMEWORK_ABI_MAJOR<<16)|VQEAF_FRAMEWORK_ABI_MINOR)

/* App-facing calling convention. x86_64 native modules use the SysV ABI even
 * when the host simulator is built for Windows; aarch64 uses AAPCS64. Keeping
 * this in the shared header makes both sides of every call agree. */
#if defined(__x86_64__) && (defined(_WIN32) || defined(_WIN64) || defined(__WINDOWS__))
#define QEAF_APP_CALL __attribute__((sysv_abi))
#else
#define QEAF_APP_CALL
#endif

typedef struct { uint32_t size; uint32_t abi_version; void (*log_utf8)(const char *message); int (*draw_text)(int x,int y,const char *utf8); } qeaf_app_api_v1;

/* Framework API v2 (QEAPP native ABI v2). Apps call only through this table;
 * they never receive kernel pointers, hardware registers or libc entry points.
 * The layout is frozen by docs/QEAPP_NATIVE_ABI_V2.md. */
#define VQEAF_APP_API_V2_VERSION 2u
typedef struct {
    uint32_t size;
    uint32_t abi_version;
    void (QEAF_APP_CALL *log_utf8)(const char *message);
    int  (QEAF_APP_CALL *draw_text)(int x, int y, const char *utf8);
    int  (QEAF_APP_CALL *request_permission)(const char *permission);
    int  (QEAF_APP_CALL *read_file)(const char *path, void **out, size_t *size);
    void (QEAF_APP_CALL *free)(void *ptr);
} qeaf_app_api_v2;

/* Framework API v3 (QEAPP native ABI v3). Strict superset of v2: the first
 * six fields are byte-identical to qeaf_app_api_v2, so a v3 table can be
 * passed to a v2-aware prefix reader. v3 adds pixel blitting (video/media
 * frames) and ranged file reads (streaming large media without loading the
 * whole file). The v2 table remains frozen; v3 modules declare
 * min_api 3 and import "qeaf_app_api_v3". */
#define VQEAF_APP_API_V3_VERSION 3u

/* Copies `h` rows of `w` RGB565 pixels into the app framebuffer at (x,y).
 * `stride` is the source row pitch in pixels (>= w). Rows/columns outside
 * the canvas are clipped, not rejected. Returns 0, or -1 without any
 * permission or on invalid arguments. Requires ui.display. */
typedef int (QEAF_APP_CALL *qeaf_blit_pixels_fn)(int x, int y, int w, int h,
                                                 int stride,
                                                 const uint16_t *rgb565);

/* Reads up to `size` bytes at `offset` from a file into a caller-provided
 * buffer. Unlike read_file the buffer is not owned by the runtime and must
 * not be passed to free(); the transient copy is charged against the
 * app's memory quota for the duration of the call. Returns 0, or -1 when
 * the range is out of bounds or storage.read is not granted. */
typedef int (QEAF_APP_CALL *qeaf_read_file_part_fn)(const char *path,
                                                    uint32_t offset,
                                                    size_t size, void *out);

typedef struct {
    /* v2 prefix (frozen layout, keep byte-identical). */
    uint32_t size;
    uint32_t abi_version;
    void (QEAF_APP_CALL *log_utf8)(const char *message);
    int  (QEAF_APP_CALL *draw_text)(int x, int y, const char *utf8);
    int  (QEAF_APP_CALL *request_permission)(const char *permission);
    int  (QEAF_APP_CALL *read_file)(const char *path, void **out, size_t *size);
    void (QEAF_APP_CALL *free)(void *ptr);
    /* v3 additions. */
    qeaf_blit_pixels_fn blit_pixels;
    qeaf_read_file_part_fn read_file_part;
} qeaf_app_api_v3;

/* Framework API v4 (QEAPP native ABI v4). Strict superset of v3: the first
 * nine fields are byte-identical to qeaf_app_api_v3, so a v4 table can be
 * passed to a v2/v3-aware prefix reader. v4 exposes the typography weight
 * scale to apps through draw_text_weighted. The v2 and v3 tables remain
 * frozen; v4 modules declare min_api 4 and import "qeaf_app_api_v4". */
#define VQEAF_APP_API_V4_VERSION 4u

/* Text weight passed to draw_text_weighted. Mirrors the shell typography
 * vqeaf_font_weight_t (platform/violet/ui/fonts/vqeaf_fonts.h): 0 = regular,
 * 1 = bold. Any other value is rejected by the runtime. */
#define QEAF_TEXT_WEIGHT_REGULAR 0u
#define QEAF_TEXT_WEIGHT_BOLD    1u

/* Draws `utf8` at (x,y) in the given text weight. weight must be
 * QEAF_TEXT_WEIGHT_REGULAR or QEAF_TEXT_WEIGHT_BOLD; the bold face is a real
 * heavier raster, not a colour change. Returns 0, or -1 without ui.display,
 * on a NULL string, or on an unknown weight. Requires ui.display. */
typedef int (QEAF_APP_CALL *qeaf_draw_text_weighted_fn)(int x, int y,
                                                        const char *utf8,
                                                        uint32_t weight);

typedef struct {
    /* v2 prefix (frozen layout, keep byte-identical). */
    uint32_t size;
    uint32_t abi_version;
    void (QEAF_APP_CALL *log_utf8)(const char *message);
    int  (QEAF_APP_CALL *draw_text)(int x, int y, const char *utf8);
    int  (QEAF_APP_CALL *request_permission)(const char *permission);
    int  (QEAF_APP_CALL *read_file)(const char *path, void **out, size_t *size);
    void (QEAF_APP_CALL *free)(void *ptr);
    /* v3 additions (keep byte-identical). */
    qeaf_blit_pixels_fn blit_pixels;
    qeaf_read_file_part_fn read_file_part;
    /* v4 additions. */
    qeaf_draw_text_weighted_fn draw_text_weighted;
} qeaf_app_api_v4;

/* Framework API v5 (QEAPP native ABI v5). Strict superset of v4: the first
 * ten fields are byte-identical to qeaf_app_api_v4, so a v5 table can be
 * passed to a v2/v3/v4-aware prefix reader. v5 exposes the shell's layout
 * weight semantics (vqeaf_weight / weighted_row / weighted_column) to apps as
 * a pure proportional rect split, so immediate-mode canvases can lay out in
 * ratios without owning an LVGL widget tree. The v2/v3/v4 tables remain
 * frozen; v5 modules declare min_api 5 and import "qeaf_app_api_v5". */
#define VQEAF_APP_API_V5_VERSION 5u

#define QEAF_LAYOUT_AXIS_HORIZONTAL 0u
#define QEAF_LAYOUT_AXIS_VERTICAL   1u

typedef struct { int32_t x, y, w, h; } qeaf_rect_t;

/* Pure proportional split, shared verbatim by every runtime so host and
 * device agree pixel-for-pixel. Mirrors LVGL flex-grow: after reserving
 * gap*(n-1) pixels, the remaining span is divided in ratio to weights[i];
 * the integer remainder is handed one pixel at a time to the leading rects
 * so the sizes always sum to the free span exactly. */
static inline int qeaf_layout_split_rect(int x, int y, int w, int h,
                                         uint32_t axis, const uint32_t *weights,
                                         uint32_t n, uint32_t gap,
                                         qeaf_rect_t *out)
{
    if(weights == NULL || out == NULL || n == 0) return -1;
    if(axis != QEAF_LAYOUT_AXIS_HORIZONTAL && axis != QEAF_LAYOUT_AXIS_VERTICAL)
        return -1;
    if(w < 0 || h < 0) return -1;
    int32_t span = (axis == QEAF_LAYOUT_AXIS_HORIZONTAL) ? w : h;
    int32_t gaps = (int32_t)gap * (int32_t)(n - 1);
    if(gaps > span) return -1;
    int32_t free_span = span - gaps;
    uint64_t total = 0;
    for(uint32_t i = 0; i < n; i++) total += weights[i];
    if(total == 0) return -1;
    int32_t used = 0, pos = (axis == QEAF_LAYOUT_AXIS_HORIZONTAL) ? x : y;
    int32_t assigned = 0;
    for(uint32_t i = 0; i < n; i++) {
        int32_t size = (int32_t)((uint64_t)free_span * weights[i] / total);
        out[i].x = x; out[i].y = y; out[i].w = w; out[i].h = h;
        if(axis == QEAF_LAYOUT_AXIS_HORIZONTAL) { out[i].x = pos; out[i].w = size; }
        else                                    { out[i].y = pos; out[i].h = size; }
        pos += size + (int32_t)gap;
        assigned += size;
        used = pos;
    }
    /* Hand the rounding remainder to the leading rects, one pixel each. */
    int32_t rem = free_span - assigned;
    for(uint32_t i = 0; i < n && rem > 0; i++, rem--) {
        if(axis == QEAF_LAYOUT_AXIS_HORIZONTAL) {
            out[i].w += 1;
            for(uint32_t j = i + 1; j < n; j++) out[j].x += 1;
        } else {
            out[i].h += 1;
            for(uint32_t j = i + 1; j < n; j++) out[j].y += 1;
        }
    }
    (void)used;
    return 0;
}

/* Splits (x,y,w,h) into n sub-rects along `axis` proportional to weights[i],
 * with `gap` pixels between neighbours; writes n rects to out. Pure
 * computation: no permission and no display side effect. Returns 0, or -1 on
 * NULL weights/out, n==0, unknown axis, negative w/h, gap overflow or zero
 * total weight. */
typedef int (QEAF_APP_CALL *qeaf_layout_split_weighted_fn)(
    int x, int y, int w, int h, uint32_t axis, const uint32_t *weights,
    uint32_t n, uint32_t gap, qeaf_rect_t *out);

typedef struct {
    /* v2 prefix (frozen layout, keep byte-identical). */
    uint32_t size;
    uint32_t abi_version;
    void (QEAF_APP_CALL *log_utf8)(const char *message);
    int  (QEAF_APP_CALL *draw_text)(int x, int y, const char *utf8);
    int  (QEAF_APP_CALL *request_permission)(const char *permission);
    int  (QEAF_APP_CALL *read_file)(const char *path, void **out, size_t *size);
    void (QEAF_APP_CALL *free)(void *ptr);
    /* v3 additions (keep byte-identical). */
    qeaf_blit_pixels_fn blit_pixels;
    qeaf_read_file_part_fn read_file_part;
    /* v4 additions (keep byte-identical). */
    qeaf_draw_text_weighted_fn draw_text_weighted;
    /* v5 additions. */
    qeaf_layout_split_weighted_fn layout_split_weighted;
} qeaf_app_api_v5;

/* Lifecycle exports. create returns an app handle (NULL on failure);
 * start/pause/resume/stop return 0 on success; destroy returns void. */
typedef void *(QEAF_APP_CALL *qeapp_create_fn)(const qeaf_app_api_v2 *api);
typedef int  (QEAF_APP_CALL *qeapp_lifecycle_fn)(void *handle);
typedef void (QEAF_APP_CALL *qeapp_destroy_fn)(void *handle);

uint32_t vqeaf_framework_abi_version(void);
#endif