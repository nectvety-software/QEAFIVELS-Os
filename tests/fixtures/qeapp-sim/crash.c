/* Simulator fixture: deliberately faults in start() so the crash guard can be
 * proven. The supervisor must contain the crash and keep the shell alive. */
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
    if(g_api != NULL)
        g_api->log_utf8("crash: about to fault");
    *(volatile int *)0 = 1; /* null write: SIGSEGV / access violation */
    return 0;
}

int pause(void *handle) { (void)handle; return 0; }
int resume(void *handle) { (void)handle; return 0; }
int stop(void *handle) { (void)handle; return 0; }
void destroy(void *handle) { (void)handle; g_api = NULL; }