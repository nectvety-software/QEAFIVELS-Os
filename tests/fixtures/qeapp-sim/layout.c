/* Simulator fixture: framework API v5 layout weight (layout_split_weighted).
 *
 * Splits a horizontal band in ratio 1:2:1 and a vertical band in ratio 1:1:1
 * with a rounding remainder, then draws a label at each computed rect origin
 * so the framebuffer hash encodes the layout (cross-arch determinism). Also
 * probes six invalid argument combinations, every one of which the runtime
 * must reject. The split is pure: it succeeds even without ui.display, while
 * the label draws are denied. Freestanding PIC, no libc/syscall/hardware. */
#include "qeaf_api.h"

static const qeaf_app_api_v5 *g_api;

void *create(const qeaf_app_api_v2 *api)
{
    if(api == NULL || api->abi_version != VQEAF_APP_API_V5_VERSION)
        return NULL;
    g_api = (const qeaf_app_api_v5 *)api;
    g_api->log_utf8("layout fixture: create");
    return (void *)&g_api;
}

int start(void *handle)
{
    (void)handle;
    if(g_api == NULL) return -1;
    g_api->log_utf8("layout fixture: start");

    qeaf_rect_t row[3];
    static const uint32_t row_w[3] = {1u, 2u, 1u};
    int r0 = g_api->layout_split_weighted(40, 100, 640, 80,
                                          QEAF_LAYOUT_AXIS_HORIZONTAL,
                                          row_w, 3u, 16u, row);
    if(r0 == 0) {
        g_api->draw_text(row[0].x, row[0].y, "A");
        g_api->draw_text(row[1].x, row[1].y, "B");
        g_api->draw_text(row[2].x, row[2].y, "C");
    }

    qeaf_rect_t col[3];
    static const uint32_t col_w[3] = {1u, 1u, 1u};
    int r1 = g_api->layout_split_weighted(40, 300, 200, 301,
                                          QEAF_LAYOUT_AXIS_VERTICAL,
                                          col_w, 3u, 10u, col);
    if(r1 == 0) {
        g_api->draw_text(col[0].x, col[0].y, "D");
        g_api->draw_text(col[1].x, col[1].y, "E");
        g_api->draw_text(col[2].x, col[2].y, "F");
    }

    qeaf_rect_t scratch[3];
    static const uint32_t zero_w[2] = {0u, 0u};
    int bad = 0;
    bad += (g_api->layout_split_weighted(0, 0, 100, 100,
              QEAF_LAYOUT_AXIS_HORIZONTAL, row_w, 0u, 0u, scratch) == -1);
    bad += (g_api->layout_split_weighted(0, 0, 100, 100, 2u,
              row_w, 3u, 0u, scratch) == -1);
    bad += (g_api->layout_split_weighted(0, 0, 100, 100,
              QEAF_LAYOUT_AXIS_HORIZONTAL, NULL, 3u, 0u, scratch) == -1);
    bad += (g_api->layout_split_weighted(0, 0, 100, 100,
              QEAF_LAYOUT_AXIS_HORIZONTAL, zero_w, 2u, 0u, scratch) == -1);
    bad += (g_api->layout_split_weighted(0, 0, 100, 100,
              QEAF_LAYOUT_AXIS_HORIZONTAL, row_w, 3u, 1000u, scratch) == -1);
    bad += (g_api->layout_split_weighted(0, 0, 100, 100,
              QEAF_LAYOUT_AXIS_HORIZONTAL, row_w, 3u, 0u, NULL) == -1);
    g_api->log_utf8(bad == 6 ? "layout fixture: invalid splits rejected"
                             : "layout fixture: invalid splits ACCEPTED");
    g_api->log_utf8(r0 == 0 && r1 == 0 ? "layout fixture: splits ok"
                                       : "layout fixture: splits FAILED");
    return 0;
}

int pause(void *handle) { (void)handle; return 0; }
int resume(void *handle) { (void)handle; return 0; }
int stop(void *handle) { (void)handle; return 0; }
void destroy(void *handle) { (void)handle; g_api = NULL; }