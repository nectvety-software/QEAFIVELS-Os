/* Simulator fixture: exercises storage read/free, permission denial and input
 * through the framework API v2 table. Behavior depends on manifest permissions
 * and seeded files supplied by the test driver. Freestanding PIC, no libc. */
#include "qeaf_api.h"
#include "qeaf_native_abi.h"

static const qeaf_app_api_v2 *g_api;

void *create(const qeaf_app_api_v2 *api)
{
    if(api == NULL || api->abi_version != VQEAF_APP_API_V2_VERSION)
        return NULL;
    g_api = api;
    return (void *)&g_api;
}

int start(void *handle)
{
    (void)handle;
    if(g_api == NULL) return -1;
    void *buf = NULL;
    size_t size = 0;
    if(g_api->read_file("data/hello.txt", &buf, &size) == 0) {
        g_api->log_utf8((const char *)buf);
        g_api->free(buf);
    } else {
        g_api->log_utf8("read_file denied");
    }
    g_api->draw_text(10, 40, "probe");
    return 0;
}

void on_input(void *handle, const qeaf_input_event_t *event)
{
    (void)handle;
    (void)event;
    if(g_api != NULL)
        g_api->log_utf8("input event");
}

int pause(void *handle) { (void)handle; return 0; }
int resume(void *handle) { (void)handle; return 0; }
int stop(void *handle) { (void)handle; return 0; }
void destroy(void *handle) { (void)handle; g_api = NULL; }