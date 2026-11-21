/* Hello Native QEAPP - framework API v5 sample.
 *
 * Freestanding position-independent code: no libc, no syscall, no hardware.
 * All OS access goes through the qeaf_app_api_v5 table passed to create().
 * App state lives in static .bss; the handle returned by create() points at it.
 */
#include "qeaf_api.h"

static struct {
    const qeaf_app_api_v5 *api;
    int started;
} g_app;

void *create(const qeaf_app_api_v5 *api)
{
    if(api == NULL || api->abi_version != VQEAF_APP_API_V5_VERSION)
        return NULL;
    g_app.api = api;
    g_app.started = 0;
    api->log_utf8("Hello Native QEAPP: create");
    return &g_app;
}

int start(void *handle)
{
    (void)handle;
    if(g_app.api == NULL)
        return -1;
    g_app.started = 1;
    g_app.api->log_utf8("Hello Native QEAPP: start");
    g_app.api->draw_text(48, 120, "Hello from native QEAPP v5");
    return 0;
}

int pause(void *handle)
{
    (void)handle;
    if(g_app.api != NULL)
        g_app.api->log_utf8("Hello Native QEAPP: pause");
    return 0;
}

int resume(void *handle)
{
    (void)handle;
    if(g_app.api != NULL)
        g_app.api->log_utf8("Hello Native QEAPP: resume");
    return 0;
}

int stop(void *handle)
{
    (void)handle;
    g_app.started = 0;
    if(g_app.api != NULL)
        g_app.api->log_utf8("Hello Native QEAPP: stop");
    return 0;
}

void destroy(void *handle)
{
    (void)handle;
    g_app.api = NULL;
    g_app.started = 0;
}