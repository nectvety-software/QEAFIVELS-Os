/* Simulator fixture: a finite but slow start() that overruns a tiny per-call
 * CPU budget. The volatile accumulator forces a real memory access every
 * iteration so the loop cannot be optimized away; it still terminates, so the
 * host detects the overrun after the call returns. (An infinite hang never
 * returns and is instead killed by the Phase 4 supervisor.) Freestanding PIC. */
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
    volatile uint32_t n = 0;
    for(uint32_t i = 0; i < 50000000u; i++)
        n += i;
    (void)n;
    g_api->log_utf8("slow: finished");
    return 0;
}

int pause(void *handle) { (void)handle; return 0; }
int resume(void *handle) { (void)handle; return 0; }
int stop(void *handle) { (void)handle; return 0; }
void destroy(void *handle) { (void)handle; g_api = NULL; }