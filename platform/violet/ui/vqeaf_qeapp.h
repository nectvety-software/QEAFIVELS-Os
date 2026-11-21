/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_qeapp.h - signed QEAPP install/launch store. */
#ifndef VQEAF_QEAPP_H
#define VQEAF_QEAPP_H

/* Installed apps kept in RAM (each keeps a 16 KiB launcher icon), and the
 * largest catalog the boot restore path reads back. The p77 store itself is
 * bounded by bytes, not by this count. */
#define QEAPP_MAX 24

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char id[64];
    char name[64];
    char version[24];
    char message[512];
    char package_file[128];   /* QEAFRESCUE path kept for native launch */
    int native;               /* 1 = format v2 native module (arm64) */
    int abi_version;          /* native module ABI from the manifest; 0 = script */
    uint32_t permissions;     /* immutable requested mask from manifest */
    uint32_t granted_permissions; /* persisted user grants, subset of requested */
    uint8_t icon[64 * 64 * 4];   /* BGRA, from assets/icon.rgba */
    /* Per-app data blob, like /data/data/<id>: kept across an update, dropped
     * on uninstall and persisted in the p77 catalog. */
    uint8_t *data;
    size_t data_size;
    int has_icon;
    lv_image_dsc_t icon_dsc;
} vqeaf_qeapp_t;

/* Package metadata read for the install confirmation screen. It carries the
 * same identity/permissions/icon the installed record would, but reading it
 * never registers an app: the user sees what they are about to install before
 * anything is committed. */
typedef struct {
    char id[64];
    char name[64];
    char version[24];
    char message[512];
    int native;               /* 1 = format v2 native module (arm64) */
    int abi_version;          /* native module ABI from the manifest; 0 = script */
    uint32_t permissions;     /* qeaf_permission_t bits from the manifest */
    int has_icon;
    uint8_t icon[64 * 64 * 4];   /* BGRA, from assets/icon.rgba */
} vqeaf_qeapp_preview_t;

/* Reads a .qeapp from the QEAFRESCUE volume, verifies its signature and
 * parses the declarative entry script. Returns 0 or -1 with a reason. */
int vqeaf_qeapp_install(const char *file, char *error, size_t cap);

/* Verifies and parses a package without installing it, filling `out` with the
 * identity, declared permissions and launcher icon shown on the confirmation
 * screen. Returns 0 or -1 with a reason; the installed list is left untouched.
 * install() re-verifies the bytes, so a file swapped after preview is caught. */
int vqeaf_qeapp_preview(const char *file, vqeaf_qeapp_preview_t *out,
                        char *error, size_t cap);

/* Queued from the rescue worker; the UI thread performs the install (the FAT
 * read hook and catalog write must not run while the worker holds the volume
 * lock). */
void vqeaf_qeapp_request_install(const char *path);
int vqeaf_qeapp_take_install_request(char *out, size_t cap);

/* Boot restore from the p77 catalog and launch source: the package bytes come
 * from the durable store, not from QEAFRESCUE. */
int vqeaf_qeapp_install_stored(const char *name, char *error, size_t cap);
int vqeaf_qeapp_package(int index, uint8_t **out, size_t *size);

/* Per-app data. write() replaces the blob (cap 64 KiB) and rewrites the
 * catalog; read() borrows it until the next write/uninstall. */
int vqeaf_qeapp_data_read(int index, const uint8_t **out, size_t *size);
int vqeaf_qeapp_data_write(int index, const void *buf, size_t size);

/* Rescue `appdata` verbs are queued for the UI thread too. */
void vqeaf_qeapp_request_data(int index, const char *text);
int vqeaf_qeapp_take_data_request(int *index, char *out, size_t cap);
void vqeaf_qeapp_request_uninstall(int index);
int vqeaf_qeapp_take_uninstall_request(void);

/* Rescue `run <name>` is queued for the UI thread for the same reason as
 * install: native launch touches LVGL and the volume lock. */
void vqeaf_qeapp_request_launch(const char *name);
int vqeaf_qeapp_take_launch_request(char *out, size_t cap);

/* Pending checks let the main loop wake the screen for a queued request
 * instead of leaving an install unprocessed while asleep. */
int vqeaf_qeapp_install_pending(void);
int vqeaf_qeapp_launch_pending(void);
int vqeaf_qeapp_data_pending(void);
int vqeaf_qeapp_uninstall_pending(void);
int vqeaf_qeapp_count(void);
const vqeaf_qeapp_t *vqeaf_qeapp_at(int index);
int vqeaf_qeapp_uninstall(int index);
/* User grants are a subset of manifest-requested permissions and are persisted. */
int vqeaf_qeapp_set_permission(int index, uint32_t bit, int granted);
uint32_t vqeaf_qeapp_effective_permissions(int index);

/* The launcher page renders the app selected by this index. */
const lv_image_dsc_t *vqeaf_qeapp_icon(int index);
int vqeaf_qeapp_selected(void);
void vqeaf_qeapp_select(int index);

/* Native (format_version 2) package inspection. Verifies the package
 * signature and manifest, then validates the .qen module structure. It never
 * maps or executes native code. Returns 0 or -1 with a reason. */
typedef struct {
    int format_version;
    int abi_version;
    int arch_id;
    uint32_t permissions;   /* qeaf_permission_t bits declared in the manifest */
    char entry[128];
    char arch[16];
} vqeaf_qeapp_native_info_t;

int vqeaf_qeapp_inspect_native(const uint8_t *zip, size_t size,
                               vqeaf_qeapp_native_info_t *info,
                               const uint8_t **module, size_t *module_size,
                               char *error, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_QEAPP_H */
