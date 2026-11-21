/* Simulator fixture: framework API v4 typography weight (draw_text_weighted).
 *
 * Draws the same string twice through the v4 table at well-separated rows:
 * once REGULAR, once BOLD. The host gate counts lit pixels in each band and
 * asserts the bold band has strictly more ink (the 8x8 face is dilated one
 * column right for bold). Also probes an unknown weight, which the runtime
 * must reject. Freestanding PIC, no libc/syscall/hardware. */
#include "qeaf_api.h"

static const qeaf_app_api_v4 *g_api;

void *create(const qeaf_app_api_v2 *api)
{
    if(api == NULL || api->abi_version != VQEAF_APP_API_V4_VERSION)
        return NULL;
    g_api = (const qeaf_app_api_v4 *)api;
    g_api->log_utf8("weight fixture: create");
    return (void *)&g_api;
}

int start(void *handle)
{
    (void)handle;
    if(g_api == NULL) return -1;
    g_api->log_utf8("weight fixture: start");
    g_api->draw_text_weighted(48, 120, "Weight", QEAF_TEXT_WEIGHT_REGULAR);
    g_api->draw_text_weighted(48, 400, "Weight", QEAF_TEXT_WEIGHT_BOLD);
    int bad = g_api->draw_text_weighted(48, 680, "Weight", 2u);
    g_api->log_utf8(bad == -1 ? "weight fixture: invalid weight rejected"
                              : "weight fixture: invalid weight ACCEPTED");
    return 0;
}

int pause(void *handle) { (void)handle; return 0; }
int resume(void *handle) { (void)handle; return 0; }
int stop(void *handle) { (void)handle; return 0; }
void destroy(void *handle) { (void)handle; g_api = NULL; }