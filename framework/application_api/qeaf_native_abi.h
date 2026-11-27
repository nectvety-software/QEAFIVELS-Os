/* QEAPP native ABI v2 - module/simulator contract.
 *
 * The framework call table lives in qeaf_api.h (qeaf_app_api_v2). This header
 * defines the permission bits, the lifecycle event ids, the module export and
 * import names, and the optional input event delivered by host and device sessions.
 * The .qen container layout is documented in docs/QEAPP_NATIVE_ABI_V2.md and
 * implemented in sdk/compiler/qen.py + platform/violet/ui/vqeaf_qeapp_native.c.
 */
#ifndef VQEAF_QEAF_NATIVE_ABI_H
#define VQEAF_QEAF_NATIVE_ABI_H

#include <stdint.h>
#include <string.h>

#include "qeaf_api.h"

#define VQEAF_QEAPP_ABI_VERSION 2u
#define VQEAF_QEN_MAGIC "QEAF-QEN1"

typedef enum {
    QEAF_PERM_UI_DISPLAY = 1u << 0,
    QEAF_PERM_STORAGE_READ = 1u << 1,
    QEAF_PERM_STORAGE_WRITE_APP = 1u << 2,
    QEAF_PERM_INPUT_RECEIVE = 1u << 3,
    QEAF_PERM_MEDIA_READ = 1u << 4,
} qeaf_permission_t;

typedef enum {
    QEAF_APP_CREATE = 0,
    QEAF_APP_START,
    QEAF_APP_PAUSE,
    QEAF_APP_RESUME,
    QEAF_APP_STOP,
    QEAF_APP_DESTROY,
} qeaf_lifecycle_event_t;

/* Required module exports (name -> offset into the code section). pause and
 * resume are optional; on_input opts into interactive host/device sessions and
 * is called only while input.receive is granted. */
#define QEAPP_EXPORT_CREATE   "create"
#define QEAPP_EXPORT_START    "start"
#define QEAPP_EXPORT_PAUSE    "pause"
#define QEAPP_EXPORT_RESUME   "resume"
#define QEAPP_EXPORT_STOP     "stop"
#define QEAPP_EXPORT_DESTROY  "destroy"
#define QEAPP_EXPORT_ON_INPUT "on_input"

/* The only framework import a v2 module may declare. The API table itself is
 * passed to create(); imports are a capability check, not address binding.
 * v3 modules import QEAPP_IMPORT_API_V3 instead and keep the same container
 * rules otherwise (the .qen header's abi_version byte becomes 3). v4 modules
 * import QEAPP_IMPORT_API_V4 (abi_version byte 4) and add draw_text_weighted.
 * v5 modules import QEAPP_IMPORT_API_V5 (abi_version byte 5) and add
 * layout_split_weighted (proportional layout weight). */
#define QEAPP_IMPORT_API_V2 "qeaf_app_api_v2"
#define QEAPP_IMPORT_API_V3 "qeaf_app_api_v3"
#define QEAPP_IMPORT_API_V4 "qeaf_app_api_v4"
#define QEAPP_IMPORT_API_V5 "qeaf_app_api_v5"

/* Maps a manifest permission string to its bit, or 0 when unknown. */
static inline uint32_t qeaf_permission_bit(const char *name)
{
    if(name == NULL) return 0u;
    if(strcmp(name, "ui.display") == 0) return QEAF_PERM_UI_DISPLAY;
    if(strcmp(name, "storage.read") == 0) return QEAF_PERM_STORAGE_READ;
    if(strcmp(name, "storage.write.app") == 0) return QEAF_PERM_STORAGE_WRITE_APP;
    if(strcmp(name, "input.receive") == 0) return QEAF_PERM_INPUT_RECEIVE;
    if(strcmp(name, "media.read") == 0) return QEAF_PERM_MEDIA_READ;
    return 0u;
}

/* Optional input event delivered to on_input(handle, event) between start and
 * pause. Coordinates are in the app canvas (720x1560 portrait). */
typedef enum {
    QEAF_INPUT_KEY = 1,
    QEAF_INPUT_TOUCH = 2,
} qeaf_input_type_t;

typedef struct {
    uint32_t type;     /* qeaf_input_type_t */
    uint32_t code;     /* key code, or touch pointer id */
    int32_t x;
    int32_t y;
    uint32_t pressed;  /* 1 = down, 0 = up */
} qeaf_input_event_t;

typedef void (QEAF_APP_CALL *qeapp_input_fn)(void *handle,
                                             const qeaf_input_event_t *event);

#endif /* VQEAF_QEAF_NATIVE_ABI_H */