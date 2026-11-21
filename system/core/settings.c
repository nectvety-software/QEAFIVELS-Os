/* SPDX-License-Identifier: MIT */
/* system/core/settings.c - VQEAF Settings Service (pure RAM store + durable
 * write-through). No filesystem or UI dependency lives here: a backend is
 * attached from the outside (see settings_fat16.c), which keeps this module
 * linkable into the shell, the host render and the freestanding kernel alike.
 */
#include "vqeaf/settings.h"

#include <string.h>

typedef struct {
    char key[VQEAF_SETTINGS_KEY_MAX];
    uint8_t type;
    int32_t num;                       /* bool (0/1) or int value */
    char str[VQEAF_SETTINGS_STR_MAX];
} entry_t;

static entry_t entries[VQEAF_SETTINGS_MAX];
static uint32_t count;
static vqeaf_settings_backend_t backend;
static int persistent;

/* --- little-endian helpers ------------------------------------------------ */
static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8);
}
static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t fnv1a32(const uint8_t *data, uint32_t size)
{
    uint32_t value = 0x811C9DC5u;
    for(uint32_t i = 0; i < size; i++) {
        value ^= data[i];
        value *= 0x01000193u;
    }
    return value;
}

static int key_ok(const char *key)
{
    if(key == NULL) return 0;
    size_t n = strlen(key);
    return n >= 1 && n < VQEAF_SETTINGS_KEY_MAX;
}

static int find(const char *key)
{
    for(uint32_t i = 0; i < count; i++)
        if(strcmp(entries[i].key, key) == 0) return (int)i;
    return -1;
}

/* --- fixed-capacity durable image ----------------------------------------- */
/* Layout: "VQS1" | ver u8 | count u8 | used_len u16 | records | fnv1a32 u32,
 * zero-padded to VQEAF_SETTINGS_BLOB_SIZE. The checksum covers every byte up
 * to used_len, so a truncated or tampered image is rejected on load. */
static int serialize(uint8_t *blob, uint32_t cap)
{
    if(cap < VQEAF_SETTINGS_BLOB_SIZE) return -1;
    memset(blob, 0, cap);
    blob[0] = 'V'; blob[1] = 'Q'; blob[2] = 'S'; blob[3] = '1';
    blob[4] = 1;
    blob[5] = (uint8_t)count;
    uint32_t pos = 8;
    uint32_t limit = cap - 4;          /* reserve the trailing checksum */
    for(uint32_t i = 0; i < count; i++) {
        const entry_t *e = &entries[i];
        uint32_t keylen = (uint32_t)strlen(e->key);
        if(pos + 1 + keylen + 1 > limit) return -1;
        blob[pos++] = (uint8_t)keylen;
        memcpy(blob + pos, e->key, keylen); pos += keylen;
        blob[pos++] = e->type;
        if(e->type == VQEAF_SETTING_BOOL) {
            if(pos + 1 > limit) return -1;
            blob[pos++] = e->num ? 1 : 0;
        } else if(e->type == VQEAF_SETTING_INT) {
            if(pos + 4 > limit) return -1;
            put32(blob + pos, (uint32_t)e->num); pos += 4;
        } else {
            uint32_t slen = (uint32_t)strlen(e->str);
            if(pos + 1 + slen > limit) return -1;
            blob[pos++] = (uint8_t)slen;
            memcpy(blob + pos, e->str, slen); pos += slen;
        }
    }
    put16(blob + 6, (uint16_t)pos);
    put32(blob + pos, fnv1a32(blob, pos));
    return 0;
}

static int deserialize(const uint8_t *blob, uint32_t len)
{
    count = 0;
    if(len < 12) return -1;
    if(blob[0] != 'V' || blob[1] != 'Q' || blob[2] != 'S' || blob[3] != '1')
        return -1;
    if(blob[4] != 1) return -1;
    uint32_t n = blob[5];
    if(n > VQEAF_SETTINGS_MAX) return -1;
    uint32_t used = get16(blob + 6);
    if(used < 8 || used + 4 > VQEAF_SETTINGS_BLOB_SIZE || used + 4 > len)
        return -1;
    if(get32(blob + used) != fnv1a32(blob, used)) return -1;

    uint32_t pos = 8;
    for(uint32_t i = 0; i < n; i++) {
        if(pos + 1 > used) return -1;
        uint32_t keylen = blob[pos++];
        if(keylen < 1 || keylen >= VQEAF_SETTINGS_KEY_MAX ||
           pos + keylen + 1 > used) return -1;
        entry_t *e = &entries[i];
        memset(e, 0, sizeof(*e));
        memcpy(e->key, blob + pos, keylen); e->key[keylen] = 0; pos += keylen;
        e->type = blob[pos++];
        if(e->type == VQEAF_SETTING_BOOL) {
            if(pos + 1 > used) return -1;
            e->num = blob[pos++] ? 1 : 0;
        } else if(e->type == VQEAF_SETTING_INT) {
            if(pos + 4 > used) return -1;
            e->num = (int32_t)get32(blob + pos); pos += 4;
        } else if(e->type == VQEAF_SETTING_STR) {
            if(pos + 1 > used) return -1;
            uint32_t slen = blob[pos++];
            if(slen >= VQEAF_SETTINGS_STR_MAX || pos + slen > used) return -1;
            memcpy(e->str, blob + pos, slen); e->str[slen] = 0; pos += slen;
        } else {
            return -1;
        }
    }
    count = n;
    return 0;
}

static int write_through(void)
{
    if(!persistent) return 0;
    uint8_t blob[VQEAF_SETTINGS_BLOB_SIZE];
    if(serialize(blob, sizeof(blob)) != 0) return -1;
    return backend.save(backend.ctx, blob, VQEAF_SETTINGS_BLOB_SIZE);
}

/* --- public API ----------------------------------------------------------- */
void vqeaf_settings_reset(void)
{
    memset(entries, 0, sizeof(entries));
    memset(&backend, 0, sizeof(backend));
    count = 0;
    persistent = 0;
}

int vqeaf_settings_attach_backend(const vqeaf_settings_backend_t *b)
{
    memset(entries, 0, sizeof(entries));
    count = 0;
    if(b == NULL || b->save == NULL || b->load == NULL) {
        memset(&backend, 0, sizeof(backend));
        persistent = 0;
        return -1;
    }
    backend = *b;
    persistent = 1;
    uint8_t blob[VQEAF_SETTINGS_BLOB_SIZE];
    uint32_t len = 0;
    if(backend.load(backend.ctx, blob, sizeof(blob), &len) == 0)
        deserialize(blob, len);        /* corrupt/absent image leaves it empty */
    return 0;
}

int vqeaf_settings_persistent(void)
{
    return persistent;
}

static entry_t *upsert(const char *key, uint8_t type)
{
    if(!key_ok(key)) return NULL;
    int idx = find(key);
    if(idx < 0) {
        if(count == VQEAF_SETTINGS_MAX) return NULL;
        idx = (int)count++;
    }
    entry_t *e = &entries[idx];
    memset(e, 0, sizeof(*e));
    strncpy(e->key, key, VQEAF_SETTINGS_KEY_MAX - 1);
    e->type = type;
    return e;
}

int vqeaf_settings_set_bool(const char *key, int value)
{
    entry_t *e = upsert(key, VQEAF_SETTING_BOOL);
    if(e == NULL) return -1;
    e->num = value ? 1 : 0;
    return write_through();
}

int vqeaf_settings_set_int(const char *key, int32_t value)
{
    entry_t *e = upsert(key, VQEAF_SETTING_INT);
    if(e == NULL) return -1;
    e->num = value;
    return write_through();
}

int vqeaf_settings_set_str(const char *key, const char *value)
{
    if(value == NULL || strlen(value) >= VQEAF_SETTINGS_STR_MAX) return -1;
    entry_t *e = upsert(key, VQEAF_SETTING_STR);
    if(e == NULL) return -1;
    strncpy(e->str, value, VQEAF_SETTINGS_STR_MAX - 1);
    return write_through();
}

int vqeaf_settings_get_bool(const char *key, int def)
{
    if(!key_ok(key)) return def;
    int idx = find(key);
    if(idx < 0 || entries[idx].type != VQEAF_SETTING_BOOL) return def;
    return entries[idx].num ? 1 : 0;
}

int32_t vqeaf_settings_get_int(const char *key, int32_t def)
{
    if(!key_ok(key)) return def;
    int idx = find(key);
    if(idx < 0 || entries[idx].type != VQEAF_SETTING_INT) return def;
    return entries[idx].num;
}

const char *vqeaf_settings_get_str(const char *key, const char *def)
{
    if(!key_ok(key)) return def;
    int idx = find(key);
    if(idx < 0 || entries[idx].type != VQEAF_SETTING_STR) return def;
    return entries[idx].str;
}