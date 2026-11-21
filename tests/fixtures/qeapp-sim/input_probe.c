/* Simulator fixture: logs the app-canvas coordinates of delivered input.
 * Device-mode input arrives in raw panel coordinates; this fixture proves the
 * simulator mapped them through the real violet panel_scale before delivery.
 * Freestanding PIC, no libc. */
#include "qeaf_api.h"
#include "qeaf_native_abi.h"

static const qeaf_app_api_v2 *g_api;

static void log_number(const char *label, int value)
{
    char buffer[32];
    size_t at = 0;
    for(const char *p = label; *p != '\0' && at + 1 < sizeof(buffer); p++)
        buffer[at++] = *p;
    if(value < 0 && at + 1 < sizeof(buffer)) {
        buffer[at++] = '-';
        value = -value;
    }
    char digits[12];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while(value != 0 && count < sizeof(digits));
    while(count > 0 && at + 1 < sizeof(buffer))
        buffer[at++] = digits[--count];
    buffer[at] = '\0';
    g_api->log_utf8(buffer);
}

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
    return g_api == NULL ? -1 : 0;
}

void on_input(void *handle, const qeaf_input_event_t *event)
{
    (void)handle;
    if(g_api == NULL || event == NULL) return;
    log_number("input x=", (int)event->x);
    log_number("input y=", (int)event->y);
}

int pause(void *handle) { (void)handle; return 0; }
int resume(void *handle) { (void)handle; return 0; }
int stop(void *handle) { (void)handle; return 0; }
void destroy(void *handle) { (void)handle; g_api = NULL; }
