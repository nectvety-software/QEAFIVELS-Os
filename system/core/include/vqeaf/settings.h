/* SPDX-License-Identifier: MIT */
/* system/core/include/vqeaf/settings.h - VQEAF Settings Service.
 *
 * A real typed key/value store owned by the system, not by any UI page. UI
 * controls read and write settings through this service so a preference has
 * one source of truth. Persistence is pluggable: with no durable backend the
 * store lives in RAM for the session (the truthful state of violet's read-only
 * initramfs, which mounts no writable prefs partition); attaching a backend
 * makes every set write through and every boot reload the saved values.
 */
#ifndef VQEAF_SETTINGS_H
#define VQEAF_SETTINGS_H

#include <stdint.h>
#include <stddef.h>

#define VQEAF_SETTINGS_KEY_MAX 32u   /* key string capacity incl. NUL */
#define VQEAF_SETTINGS_STR_MAX 64u   /* string value capacity incl. NUL */
#define VQEAF_SETTINGS_MAX     32u   /* maximum distinct keys */
#define VQEAF_SETTINGS_BLOB_SIZE 512u /* fixed durable image size (one cluster) */

typedef enum {
    VQEAF_SETTING_BOOL = 1,
    VQEAF_SETTING_INT  = 2,
    VQEAF_SETTING_STR  = 3
} vqeaf_setting_type_t;

/* Durable backend. save writes the whole fixed-capacity image; load reads it
 * back (returning the byte count via *len). Both return 0 on success and a
 * negative value on failure; load returns negative when no image exists yet.
 * ctx is backend-private and is not copied. */
typedef struct {
    int (*save)(void *ctx, const uint8_t *blob, uint32_t len);
    int (*load)(void *ctx, uint8_t *blob, uint32_t cap, uint32_t *len);
    void *ctx;
} vqeaf_settings_backend_t;

/* Clear the RAM store and detach any backend: a fresh boot before load. */
void vqeaf_settings_reset(void);

/* Attach a durable backend and reload persisted values into RAM. Returns 0
 * when a usable backend is attached (settings are persistent), negative when
 * the backend is missing or has no save/load (store stays session-only). A
 * missing or corrupt image is not fatal: the store starts empty and the next
 * set writes a fresh image. */
int vqeaf_settings_attach_backend(const vqeaf_settings_backend_t *backend);

/* 1 when a durable backend is attached, 0 when the store is session-only. */
int vqeaf_settings_persistent(void);

/* Setters return 0 on success. With a backend attached the value is written
 * through immediately; a negative result means the RAM value was kept but the
 * durable image could not be written (e.g. no space left in the fixed image).
 * Keys longer than VQEAF_SETTINGS_KEY_MAX-1 or a full store are rejected. */
int vqeaf_settings_set_bool(const char *key, int value);
int vqeaf_settings_set_int(const char *key, int32_t value);
int vqeaf_settings_set_str(const char *key, const char *value);

/* Getters return the stored value, or `def` when the key is absent or holds a
 * different type. get_str returns a pointer into the store (valid until the
 * next set/reset) or `def`. */
int     vqeaf_settings_get_bool(const char *key, int def);
int32_t vqeaf_settings_get_int(const char *key, int32_t def);
const char *vqeaf_settings_get_str(const char *key, const char *def);

#endif /* VQEAF_SETTINGS_H */