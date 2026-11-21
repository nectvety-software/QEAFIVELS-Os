/* Simulator fixture: attempts to read storage through unsafe paths (traversal,
 * absolute, drive-qualified, backslash). storage.read is granted, so every
 * denial here comes from the broker's path-scope guard, not the permission bit.
 * The host audits each denial; the module only reports success/denial.
 * Freestanding PIC, no libc. */
#include "qeaf_api.h"

static const qeaf_app_api_v2 *g_api;

void *create(const qeaf_app_api_v2 *api)
{
    if(api == NULL || api->abi_version != VQEAF_APP_API_V2_VERSION)
        return NULL;
    g_api = api;
    return (void *)&g_api;
}

static void try_path(const char *path)
{
    void *buf = NULL;
    size_t size = 0;
    if(g_api->read_file(path, &buf, &size) == 0) {
        g_api->log_utf8("badpath: UNEXPECTED success");
        g_api->free(buf);
    } else {
        g_api->log_utf8("badpath: denied");
    }
}

int start(void *handle)
{
    (void)handle;
    if(g_api == NULL) return -1;
    try_path("../secret.txt");
    try_path("/etc/passwd");
    try_path("C:/windows/system32");
    try_path("data\\..\\secret");
    return 0;
}

int pause(void *handle) { (void)handle; return 0; }
int resume(void *handle) { (void)handle; return 0; }
int stop(void *handle) { (void)handle; return 0; }
void destroy(void *handle) { (void)handle; g_api = NULL; }