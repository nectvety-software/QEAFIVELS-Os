/* Simulator fixture: hangs in start() so the supervisor timeout can be proven.
 * The supervisor must kill the child and keep the shell alive. */
#include "qeaf_api.h"

static const qeaf_app_api_v2 *g_api;
static volatile unsigned long spin;

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
    if(g_api != NULL)
        g_api->log_utf8("hang: spinning");
    for(;;) spin++; /* never returns: supervisor must time out and kill */
    return 0;
}

int pause(void *handle) { (void)handle; return 0; }
int resume(void *handle) { (void)handle; return 0; }
int stop(void *handle) { (void)handle; return 0; }
void destroy(void *handle) { (void)handle; g_api = NULL; }