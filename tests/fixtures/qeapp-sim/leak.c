/* Simulator fixture: reads a file through the API but never frees it, so the
 * host must report a leaked allocation after destroy. Freestanding PIC. */
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
    void *buf = NULL;
    size_t size = 0;
    if(g_api->read_file("data/hello.txt", &buf, &size) == 0)
        g_api->log_utf8("leak: buffer kept");
    /* deliberately no g_api->free(buf): the host must detect the leak */
    return 0;
}

int pause(void *handle) { (void)handle; return 0; }
int resume(void *handle) { (void)handle; return 0; }
int stop(void *handle) { (void)handle; return 0; }
void destroy(void *handle) { (void)handle; g_api = NULL; }