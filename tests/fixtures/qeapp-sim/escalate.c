/* Simulator fixture: tries to escalate beyond its manifest ceiling by requesting
 * permissions it never declared (storage.write.app, media.read). The broker must
 * deny each request and audit it; the module reports the outcome. Freestanding
 * PIC, no libc. */
#include "qeaf_api.h"

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
    int w = g_api->request_permission("storage.write.app");
    int m = g_api->request_permission("media.read");
    int u = g_api->request_permission("not.a.permission");
    g_api->log_utf8(w == 0 ? "escalate: write denied" : "escalate: write GRANTED");
    g_api->log_utf8(m == 0 ? "escalate: media denied" : "escalate: media GRANTED");
    g_api->log_utf8(u == 0 ? "escalate: unknown denied" : "escalate: unknown GRANTED");
    return 0;
}

int pause(void *handle) { (void)handle; return 0; }
int resume(void *handle) { (void)handle; return 0; }
int stop(void *handle) { (void)handle; return 0; }
void destroy(void *handle) { (void)handle; g_api = NULL; }