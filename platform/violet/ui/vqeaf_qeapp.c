/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_qeapp.c - install/verify/launch QEAPP packages.
 *
 * This is the dev signing profile: HMAC-SHA256 over every ZIP entry except
 * SIGNATURE, keyed with the shared development key below. It authenticates a
 * package against the image (tamper detection) but is not a public-key
 * signature; the key is in the image. A future format version must move to an
 * Ed25519/RSA key whose private half never ships.
 *
 * The runtime deliberately executes only the declarative `app` and `print`
 * lines of the entry script. Arbitrary native code is never mapped.
 */
#define _GNU_SOURCE

#include "vqeaf_qeapp.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vqeaf_ui.h"
#include "vqeaf_qeapp_native.h"
#include "core/log.h"
#ifdef __linux__
#include "../appstore.h"
#endif

#define QEAPP_ZIP_MAX_ENTRIES 32
#define QEAPP_FILE_CAP (512u * 1024u)
#define QEAPP_DEV_KEY "VQEAF-QEAPP-DEV-KEY-2026"

static vqeaf_qeapp_t g_apps[QEAPP_MAX];
static int g_app_count;
static int g_selected;

/* ---- SHA-256 / HMAC ------------------------------------------------------ */

#ifdef __linux__

typedef struct {
    uint32_t state[8];
    uint64_t length;
    uint8_t block[64];
    size_t used;
} sha256_t;

static uint32_t rotr(uint32_t value, int bits)
{
    return (value >> bits) | (value << (32 - bits));
}

static void sha256_transform(sha256_t *ctx, const uint8_t block[64])
{
    static const uint32_t k[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,
        0x59f111f1u,0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,
        0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,
        0xc19bf174u,0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,
        0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,0x983e5152u,
        0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
        0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,
        0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,
        0xd6990624u,0xf40e3585u,0x106aa070u,0x19a4c116u,0x1e376c08u,
        0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,
        0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
        0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;

    for(int i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               (uint32_t)block[i * 4 + 3];
    for(int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^
                      (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^
                      (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
    for(int i = 0; i < 64; i++) {
        uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c;
    ctx->state[3] += d; ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_t *ctx)
{
    ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
    ctx->length = 0;
    ctx->used = 0;
}

static void sha256_update(sha256_t *ctx, const uint8_t *data, size_t length)
{
    ctx->length += (uint64_t)length * 8u;
    while(length > 0) {
        size_t take = 64 - ctx->used;
        if(take > length) take = length;
        memcpy(ctx->block + ctx->used, data, take);
        ctx->used += take;
        data += take;
        length -= take;
        if(ctx->used == 64) {
            sha256_transform(ctx, ctx->block);
            ctx->used = 0;
        }
    }
}

static void sha256_final(sha256_t *ctx, uint8_t out[32])
{
    uint64_t bits = ctx->length;
    uint8_t pad = 0x80;
    uint8_t zero = 0;

    sha256_update(ctx, &pad, 1);
    while(ctx->used != 56) sha256_update(ctx, &zero, 1);
    for(int i = 7; i >= 0; i--) {
        uint8_t byte = (uint8_t)(bits >> (i * 8));
        sha256_update(ctx, &byte, 1);
        ctx->length -= 8u; /* length field must not include the final size */
    }
    for(int i = 0; i < 8; i++) {
        out[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

static void hmac_sha256(const uint8_t *key, size_t key_length,
                        const uint8_t *data, size_t length, uint8_t out[32])
{
    uint8_t block[64];
    uint8_t inner[32];
    sha256_t ctx;

    memset(block, 0, sizeof(block));
    if(key_length > sizeof(block)) {
        sha256_t key_ctx;
        sha256_init(&key_ctx);
        sha256_update(&key_ctx, key, key_length);
        sha256_final(&key_ctx, block);
    }
    else memcpy(block, key, key_length);
    for(int i = 0; i < 64; i++) block[i] ^= 0x36;
    sha256_init(&ctx);
    sha256_update(&ctx, block, sizeof(block));
    sha256_update(&ctx, data, length);
    sha256_final(&ctx, inner);
    for(int i = 0; i < 64; i++) block[i] ^= 0x36 ^ 0x5c;
    sha256_init(&ctx);
    sha256_update(&ctx, block, sizeof(block));
    sha256_update(&ctx, inner, sizeof(inner));
    sha256_final(&ctx, out);
}

/* ---- ZIP (stored entries only) ------------------------------------------- */

typedef struct {
    const char *name;
    uint16_t name_len;
    const uint8_t *data;
    uint32_t size;
} zip_entry_t;

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int zip_entries(const uint8_t *zip, size_t size, zip_entry_t *out,
                       int max, char *error, size_t cap)
{
    size_t offset = 0;
    int count = 0;

    while(offset + 30 <= size && le32(zip + offset) == 0x04034b50u) {
        if(count >= max) break;
        uint16_t method = le16(zip + offset + 8);
        uint32_t csize = le32(zip + offset + 18);
        uint32_t usize = le32(zip + offset + 22);
        uint16_t nlen = le16(zip + offset + 26);
        uint16_t elen = le16(zip + offset + 28);
        size_t data_offset = offset + 30 + nlen + elen;

        if(method != 0) {
            snprintf(error, cap, "QEAPP phải là ZIP stored (không nén).");
            return -1;
        }
        if(data_offset + csize > size || csize != usize) {
            snprintf(error, cap, "Entry ZIP hỏng hoặc vượt biên.");
            return -1;
        }
        out[count].name = (const char *)(zip + offset + 30);
        out[count].name_len = nlen;
        out[count].data = zip + data_offset;
        out[count].size = csize;
        count++;
        offset = data_offset + csize;
    }
    return count;
}

static const zip_entry_t *zip_find(const zip_entry_t *entries, int count,
                                   const char *name)
{
    size_t length = strlen(name);
    for(int i = 0; i < count; i++)
        if(entries[i].name_len == length &&
           memcmp(entries[i].name, name, length) == 0)
            return &entries[i];
    return NULL;
}

static int entry_cmp(const void *a, const void *b)
{
    const zip_entry_t *left = a;
    const zip_entry_t *right = b;
    size_t common = left->name_len < right->name_len ?
                    left->name_len : right->name_len;
    int result = memcmp(left->name, right->name, common);
    if(result != 0) return result;
    return (int)left->name_len - (int)right->name_len;
}

static int verify_signature(const zip_entry_t *entries, int count,
                            const zip_entry_t *signature)
{
    zip_entry_t sorted[QEAPP_ZIP_MAX_ENTRIES];
    int n = 0;
    size_t need = 15;
    uint8_t *canonical;
    size_t used = 0;
    uint8_t digest[32];
    char hex[65];

    if(signature == NULL || signature->size < 64) return -1;
    for(int i = 0; i < count; i++)
        if(!(entries[i].name_len == 9 &&
             memcmp(entries[i].name, "SIGNATURE", 9) == 0))
            sorted[n++] = entries[i];
    qsort(sorted, (size_t)n, sizeof(sorted[0]), entry_cmp);
    for(int i = 0; i < n; i++)
        need += sorted[i].name_len + 1 + 4 + sorted[i].size;

    canonical = malloc(need);
    if(canonical == NULL) return -1;
    memcpy(canonical, "VQEAF-QEAPP-V1\0", 15);
    used = 15;
    for(int i = 0; i < n; i++) {
        size_t name_len = sorted[i].name_len;
        memcpy(canonical + used, sorted[i].name, name_len);
        used += name_len;
        canonical[used++] = '\0';
        canonical[used++] = (uint8_t)(sorted[i].size);
        canonical[used++] = (uint8_t)(sorted[i].size >> 8);
        canonical[used++] = (uint8_t)(sorted[i].size >> 16);
        canonical[used++] = (uint8_t)(sorted[i].size >> 24);
        memcpy(canonical + used, sorted[i].data, sorted[i].size);
        used += sorted[i].size;
    }
    hmac_sha256((const uint8_t *)QEAPP_DEV_KEY, strlen(QEAPP_DEV_KEY),
                canonical, used, digest);
    free(canonical);
    for(int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    hex[64] = '\0';
    for(int i = 0; i < 64; i++) {
        char c = (char)signature->data[i];
        if(c >= 'A' && c <= 'F') c = (char)(c - 'A' + 'a');
        if(c != hex[i]) return -1;
    }
    return 0;
}

static int json_string(const uint8_t *json, size_t length, const char *key,
                       char *out, size_t cap)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    for(size_t i = 0; i + strlen(needle) < length; i++) {
        if(memcmp(json + i, needle, strlen(needle)) != 0) continue;
        size_t j = i + strlen(needle);
        while(j < length && (json[j] == ' ' || json[j] == ':')) j++;
        if(j >= length || json[j] != '"') continue;
        j++;
        size_t n = 0;
        while(j < length && json[j] != '"' && n + 1 < cap) {
            if(json[j] == '\\' && j + 1 < length) j++;
            out[n++] = (char)json[j++];
        }
        out[n] = '\0';
        return 0;
    }
    return -1;
}

static int json_int(const uint8_t *json, size_t length, const char *key,
                    int *out)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    for(size_t i = 0; i + strlen(needle) < length; i++) {
        if(memcmp(json + i, needle, strlen(needle)) != 0) continue;
        size_t j = i + strlen(needle);
        while(j < length && (json[j] == ' ' || json[j] == ':')) j++;
        int value = 0;
        int digits = 0;
        while(j < length && json[j] >= '0' && json[j] <= '9') {
            value = value * 10 + (json[j] - '0');
            j++;
            digits++;
        }
        if(digits > 0) {
            *out = value;
            return 0;
        }
    }
    return -1;
}

/* Reads the first string element of a JSON array such as
 * "architecture": ["arm64"]. */
static int json_first_array_string(const uint8_t *json, size_t length,
                                   const char *key, char *out, size_t cap)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    for(size_t i = 0; i + strlen(needle) < length; i++) {
        if(memcmp(json + i, needle, strlen(needle)) != 0) continue;
        size_t j = i + strlen(needle);
        while(j < length && json[j] != '[') j++;
        if(j >= length) return -1;
        j++;
        while(j < length && json[j] != '"' && json[j] != ']') j++;
        if(j >= length || json[j] != '"') return -1;
        j++;
        size_t n = 0;
        while(j < length && json[j] != '"' && n + 1 < cap)
            out[n++] = (char)json[j++];
        out[n] = '\0';
        return n > 0 ? 0 : -1;
    }
    return -1;
}

/* Maps a manifest permission string to its bit (qeaf_permission_t). */
static uint32_t permission_bit(const char *name)
{
    if(strcmp(name, "ui.display") == 0) return 1u << 0;
    if(strcmp(name, "storage.read") == 0) return 1u << 1;
    if(strcmp(name, "storage.write.app") == 0) return 1u << 2;
    if(strcmp(name, "input.receive") == 0) return 1u << 3;
    if(strcmp(name, "media.read") == 0) return 1u << 4;
    return 0u;
}

/* ORs every string element of a JSON array such as "permissions": [...]. */
static uint32_t json_permission_mask(const uint8_t *json, size_t length,
                                     const char *key)
{
    char needle[64];
    uint32_t mask = 0u;
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    for(size_t i = 0; i + strlen(needle) < length; i++) {
        if(memcmp(json + i, needle, strlen(needle)) != 0) continue;
        size_t j = i + strlen(needle);
        while(j < length && json[j] != '[') j++;
        if(j >= length) return 0u;
        j++;
        while(j < length && json[j] != ']') {
            if(json[j] != '"') { j++; continue; }
            j++;
            char name[64];
            size_t n = 0;
            while(j < length && json[j] != '"' && n + 1 < sizeof(name))
                name[n++] = (char)json[j++];
            name[n] = '\0';
            if(j < length && json[j] == '"') j++;
            mask |= permission_bit(name);
        }
        return mask;
    }
    return 0u;
}

static void parse_script(const uint8_t *script, size_t length, char *message,
                         size_t cap)
{
    size_t used = 0;
    size_t start = 0;

    message[0] = '\0';
    for(size_t i = 0; i <= length; i++) {
        if(i != length && script[i] != '\n') continue;
        size_t end = i;
        while(end > start && (script[end - 1] == '\r' ||
                              script[end - 1] == ' '))
            end--;
        size_t line = start;
        while(line < end && script[line] == ' ') line++;
        if(end - line > 8 && memcmp(script + line, "print \"", 7) == 0) {
            size_t text = line + 7;
            size_t text_end = text;
            while(text_end < end && script[text_end] != '"') text_end++;
            if(text_end > text) {
                if(used + 2 >= cap) break;
                size_t copy = text_end - text;
                if(used + copy + 2 >= cap) copy = cap - used - 2;
                if(copy > 0) {
                    memcpy(message + used, script + text, copy);
                    used += copy;
                    message[used++] = '\n';
                }
            }
        }
        start = i + 1;
    }
    if(used > 0) used--;
    message[used] = '\0';
}

int vqeaf_qeapp_inspect_native(const uint8_t *zip, size_t size,
                               vqeaf_qeapp_native_info_t *info,
                               const uint8_t **module, size_t *module_size,
                               char *error, size_t cap)
{
    zip_entry_t entries[QEAPP_ZIP_MAX_ENTRIES];
    const zip_entry_t *manifest_entry;
    const zip_entry_t *signature;
    const zip_entry_t *entry;
    char runtime[16];
    char entry_name[128];
    char arch[16];
    int format_version;
    int abi_version;
    int arch_id;
    int count;

    if(zip == NULL || size == 0 || info == NULL || module == NULL ||
       module_size == NULL) {
        snprintf(error, cap, "Native QEAPP input khong hop le.");
        return -1;
    }
    *module = NULL;
    *module_size = 0;
    memset(info, 0, sizeof(*info));

    count = zip_entries(zip, size, entries, QEAPP_ZIP_MAX_ENTRIES, error, cap);
    if(count < 0) return -1;
    manifest_entry = zip_find(entries, count, "manifest.json");
    signature = zip_find(entries, count, "SIGNATURE");
    if(manifest_entry == NULL) {
        snprintf(error, cap, "Native QEAPP thieu manifest.json.");
        return -1;
    }
    if(signature == NULL) {
        snprintf(error, cap, "Native QEAPP thieu SIGNATURE.");
        return -1;
    }
    if(verify_signature(entries, count, signature) != 0) {
        snprintf(error, cap, "Native QEAPP chu ky khong hop le.");
        return -1;
    }
    if(json_int(manifest_entry->data, manifest_entry->size,
                "format_version", &format_version) != 0 ||
       format_version != 2) {
        snprintf(error, cap, "Native QEAPP can format_version 2.");
        return -1;
    }
    if(json_int(manifest_entry->data, manifest_entry->size,
                "abi_version", &abi_version) != 0 ||
       (abi_version != 2 && abi_version != 3 && abi_version != 4 &&
        abi_version != 5)) {
        snprintf(error, cap, "Native QEAPP can abi_version 2, 3, 4 hoac 5.");
        return -1;
    }
    if(json_string(manifest_entry->data, manifest_entry->size, "runtime",
                   runtime, sizeof(runtime)) != 0 ||
       strcmp(runtime, "native") != 0) {
        snprintf(error, cap, "Native QEAPP can runtime native.");
        return -1;
    }
    if(json_string(manifest_entry->data, manifest_entry->size, "entry",
                   entry_name, sizeof(entry_name)) != 0) {
        snprintf(error, cap, "Native QEAPP thieu entry.");
        return -1;
    }
    if(json_first_array_string(manifest_entry->data, manifest_entry->size,
                               "architecture", arch, sizeof(arch)) != 0) {
        snprintf(error, cap, "Native QEAPP thieu architecture.");
        return -1;
    }
    arch_id = vqeaf_qeapp_native_arch_id(arch);
    if(arch_id == 0) {
        snprintf(error, cap, "Native QEAPP architecture khong ho tro.");
        return -1;
    }
    entry = zip_find(entries, count, entry_name);
    if(entry == NULL) {
        snprintf(error, cap, "Native QEAPP thieu module .qen.");
        return -1;
    }
    if(vqeaf_qeapp_native_validate_module(entry->data, entry->size,
                                          arch_id) != 0) {
        snprintf(error, cap, "Native QEAPP module khong hop le.");
        return -1;
    }

    info->format_version = format_version;
    info->abi_version = abi_version;
    info->arch_id = arch_id;
    info->permissions = json_permission_mask(manifest_entry->data,
                                             manifest_entry->size, "permissions");
    snprintf(info->entry, sizeof(info->entry), "%s", entry_name);
    snprintf(info->arch, sizeof(info->arch), "%s", arch);
    *module = entry->data;
    *module_size = entry->size;
    return 0;
}

#ifdef __linux__
/* Mirrors the installed list into the p77 catalog. The package bytes are read
 * back from the rescue volume, so the catalog never trusts stale memory. */
static void persist_apps(void)
{
    appstore_entry_t entries[QEAPP_MAX];
    uint8_t *fallback[QEAPP_MAX] = { 0 };
    int n = 0;
    int rc;

    if(appstore_is_restoring()) return;
    for(int i = 0; i < g_app_count && n < QEAPP_MAX; i++) {
        const char *path = g_apps[i].package_file;
        const char *name = strrchr(path, '/');
        name = name ? name + 1 : path;
        snprintf(entries[n].name, sizeof(entries[n].name), "%s", name);
        entries[n].size = 0;
        entries[n].path = path;
        entries[n].data = NULL;
        entries[n].data_size = 0;
        entries[n].app_data = g_apps[i].data;
        entries[n].app_data_size = g_apps[i].data_size;
        entries[n].granted_permissions = g_apps[i].granted_permissions;
        /* An app restored at boot has no file in the RAM volume any more;
         * hand its stored bytes to the catalog instead of failing the save. */
        if(!appstore_volume_has(path)) {
            uint8_t *bytes = NULL;
            size_t bytes_size = 0;
            if(appstore_read(entries[n].name, &bytes, &bytes_size) == 0) {
                entries[n].data = bytes;
                entries[n].data_size = bytes_size;
                fallback[n] = bytes;
            }
        }
        n++;
    }
    rc = appstore_save(entries, n);
    for(int i = 0; i < n; i++) free(fallback[i]);
    if(rc != 0)
        umos_logf("qeapp: catalog save failed; app stays for this boot only\n");
}
#endif

/* Verifies a package and fills `meta` with its identity, declared permissions,
 * entry output and launcher icon. Never registers an app and never frees
 * `buffer`: the caller owns it. Shared by the confirmation preview and the
 * commit path so both agree on what is acceptable. */
static int read_metadata(const uint8_t *buffer, size_t size,
                         vqeaf_qeapp_preview_t *meta, char *error, size_t cap)
{
    zip_entry_t entries[QEAPP_ZIP_MAX_ENTRIES];
    const zip_entry_t *manifest_entry;
    const zip_entry_t *entry;
    const zip_entry_t *signature;
    char entry_name[128], runtime[16];
    int count;

    memset(meta, 0, sizeof(*meta));
    if(buffer == NULL || size == 0) {
        snprintf(error, cap, "Gói QEAPP rỗng.");
        return -1;
    }
    count = zip_entries(buffer, size, entries, QEAPP_ZIP_MAX_ENTRIES,
                        error, cap);
    if(count < 0) return -1;
    manifest_entry = zip_find(entries, count, "manifest.json");
    signature = zip_find(entries, count, "SIGNATURE");
    if(manifest_entry == NULL) {
        snprintf(error, cap, "Thiếu manifest.json.");
        return -1;
    }
    if(signature == NULL) {
        snprintf(error, cap, "Thiếu SIGNATURE: chưa ký.");
        return -1;
    }
    if(verify_signature(entries, count, signature) != 0) {
        snprintf(error, cap, "Chữ ký không hợp lệ hoặc tệp đã bị sửa.");
        return -1;
    }
    if(json_string(manifest_entry->data, manifest_entry->size, "name",
                   meta->name, sizeof(meta->name)) != 0)
        snprintf(meta->name, sizeof(meta->name), "QEAPP");
    if(json_string(manifest_entry->data, manifest_entry->size, "id",
                   meta->id, sizeof(meta->id)) != 0)
        snprintf(meta->id, sizeof(meta->id), "org.unknown.app");
    if(json_string(manifest_entry->data, manifest_entry->size, "version",
                   meta->version, sizeof(meta->version)) != 0)
        snprintf(meta->version, sizeof(meta->version), "1.0.0");
    runtime[0] = '\0';
    json_string(manifest_entry->data, manifest_entry->size, "runtime",
                runtime, sizeof(runtime));
    meta->native = strcmp(runtime, "native") == 0;
    if(meta->native) {
        vqeaf_qeapp_native_info_t info;
        const uint8_t *module = NULL;
        size_t module_size = 0;
        /* A native package has no main.qe: its entry is the arm64 module in
         * the manifest. The inspector verifies the signature, manifest and
         * .qen again, so install and launch agree on what is acceptable. */
        if(vqeaf_qeapp_inspect_native(buffer, size, &info, &module, &module_size,
                                      error, cap) != 0)
            return -1;
        if(info.arch_id != VQEAF_QEN_ARCH_ARM64) {
            snprintf(error, cap, "Ứng dụng native phải là arm64 để chạy trên violet.");
            return -1;
        }
        meta->abi_version = info.abi_version;
        meta->permissions = info.permissions;
        snprintf(meta->message, sizeof(meta->message),
                 "Ứng dụng native arm64 · %s", meta->id);
    }
    else {
        if(json_string(manifest_entry->data, manifest_entry->size, "entry",
                       entry_name, sizeof(entry_name)) != 0) {
            snprintf(error, cap, "Manifest thiếu entry.");
            return -1;
        }
        entry = zip_find(entries, count, entry_name);
        if(entry == NULL) {
            snprintf(error, cap, "Không thấy entry %s.", entry_name);
            return -1;
        }
        parse_script(entry->data, entry->size, meta->message,
                     sizeof(meta->message));
    }

    /* Embedded launcher icon: raw 64x64 BGRA from assets/icon.rgba. */
    {
        const zip_entry_t *icon_entry =
            zip_find(entries, count, "assets/icon.rgba");
        if(icon_entry != NULL && icon_entry->size == 64u * 64u * 4u) {
            memcpy(meta->icon, icon_entry->data, 64u * 64u * 4u);
            meta->has_icon = 1;
        }
    }
    return 0;
}

/* Verifies and registers one package. The caller owns `buffer`; this function
 * always frees it, so restore-from-store and install-from-file share the same
 * validation path. */
static int install_buffer(const char *file, uint8_t *buffer, size_t size,
                          char *error, size_t cap)
{
    vqeaf_qeapp_preview_t meta;

    if(read_metadata(buffer, size, &meta, error, cap) != 0) {
        free(buffer);
        return -1;
    }

    int slot = g_app_count;
    int is_new = 1;
    for(int i = 0; i < g_app_count; i++)
        if(strcmp(g_apps[i].id, meta.id) == 0) {
            slot = i;
            is_new = 0;
            break;
        }
    if(is_new) {
        if(g_app_count >= QEAPP_MAX) {
            snprintf(error, cap, "Bộ nhớ ứng dụng đã đầy.");
            free(buffer);
            return -1;
        }
        g_app_count++;
    }
    if(is_new) {
        /* New install: take over any data blob already stored for this app
         * (boot restore). An update keeps the in-RAM blob untouched. */
        char base[APPSTORE_NAME];
        const char *slash = strrchr(file, '/');
        const char *leaf = slash ? slash + 1 : file;

        snprintf(base, sizeof(base), "%s", leaf);
        g_apps[slot].data = NULL;
        g_apps[slot].data_size = 0;
        /* A catalog record is keyed by filename. Before reusing its grants or
         * private data, verify that the stored signed package has the same
         * manifest ID; otherwise a filename collision could inherit access. */
        g_apps[slot].granted_permissions = 0u;
        {
            uint8_t *old_package = NULL;
            size_t old_size = 0;
            int same_identity = 0;
            if(appstore_read(base, &old_package, &old_size) == 0) {
                vqeaf_qeapp_preview_t old_meta;
                char identity_error[128];
                same_identity =
                    read_metadata(old_package, old_size, &old_meta,
                                  identity_error, sizeof(identity_error)) == 0 &&
                    strcmp(old_meta.id, meta.id) == 0;
                free(old_package);
            }
            if(same_identity) {
                (void)appstore_read_grants(base,
                                           &g_apps[slot].granted_permissions);
                (void)appstore_read_data(base, &g_apps[slot].data,
                                         &g_apps[slot].data_size);
            }
            else {
                g_apps[slot].data = NULL;
                g_apps[slot].data_size = 0;
            }
        }
    }
    snprintf(g_apps[slot].id, sizeof(g_apps[slot].id), "%s", meta.id);
    snprintf(g_apps[slot].name, sizeof(g_apps[slot].name), "%s", meta.name);
    snprintf(g_apps[slot].version, sizeof(g_apps[slot].version), "%s",
             meta.version);
    snprintf(g_apps[slot].message, sizeof(g_apps[slot].message), "%s",
             meta.message);
    snprintf(g_apps[slot].package_file, sizeof(g_apps[slot].package_file),
             "%s", file);
    g_apps[slot].native = meta.native;
    /* Kept so the app page reports the ABI the package actually declares
     * instead of one build-wide constant. Boot restore re-reads the package,
     * so the value is recovered at every boot without a catalog change. */
    g_apps[slot].abi_version = meta.abi_version;
    g_apps[slot].permissions = meta.permissions;
    /* Updates never silently grant newly requested capabilities. A fresh
     * install also starts with no grants; the user grants ui.display explicitly. */
    g_apps[slot].granted_permissions &= meta.permissions;

    /* The descriptor's data pointer is refreshed on every vqeaf_qeapp_icon()
     * call because uninstall shifts the slot array. */
    g_apps[slot].has_icon = meta.has_icon;
    memcpy(g_apps[slot].icon, meta.icon, sizeof(meta.icon));
    memset(&g_apps[slot].icon_dsc, 0, sizeof(g_apps[slot].icon_dsc));
    g_apps[slot].icon_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_apps[slot].icon_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
    g_apps[slot].icon_dsc.header.w = 64;
    g_apps[slot].icon_dsc.header.h = 64;
    g_apps[slot].icon_dsc.header.stride = 64 * 4;
    g_apps[slot].icon_dsc.data_size = 64u * 64u * 4u;
    g_apps[slot].icon_dsc.data = g_apps[slot].icon;

    g_selected = slot;
    free(buffer);
#ifdef __linux__
    persist_apps();
#endif
    return 0;
}

#else /* host render harness */

static void seed(void)
{
    if(g_app_count > 0) return;
    snprintf(g_apps[0].id, sizeof(g_apps[0].id), "org.vqeaf.hello");
    snprintf(g_apps[0].name, sizeof(g_apps[0].name), "Hello VQEAF");
    snprintf(g_apps[0].version, sizeof(g_apps[0].version), "1.0.0");
    snprintf(g_apps[0].message, sizeof(g_apps[0].message),
             "Hello from QEAPP\nChạy trên VQEAF OS");
    g_app_count = 1;
}

#endif

static char g_pending_path[128];
static volatile int g_pending;

void vqeaf_qeapp_request_install(const char *path)
{
    if(path == NULL) return;
    snprintf(g_pending_path, sizeof(g_pending_path), "%s", path);
    g_pending = 1;
}

int vqeaf_qeapp_install_pending(void)
{
    return g_pending != 0;
}

int vqeaf_qeapp_take_install_request(char *out, size_t cap)
{
    if(!g_pending || out == NULL || cap == 0) return 0;
    g_pending = 0;
    snprintf(out, cap, "%s", g_pending_path);
    return 1;
}

static char g_pending_launch[64];
static volatile int g_launch_pending;

void vqeaf_qeapp_request_launch(const char *name)
{
    if(name == NULL) return;
    snprintf(g_pending_launch, sizeof(g_pending_launch), "%s", name);
    g_launch_pending = 1;
}

int vqeaf_qeapp_launch_pending(void)
{
    return g_launch_pending != 0;
}

int vqeaf_qeapp_take_launch_request(char *out, size_t cap)
{
    if(!g_launch_pending || out == NULL || cap == 0) return 0;
    g_launch_pending = 0;
    snprintf(out, cap, "%s", g_pending_launch);
    return 1;
}

int vqeaf_qeapp_install(const char *file, char *error, size_t cap)
{
#ifdef __linux__
    uint8_t *buffer = NULL;
    size_t size = 0;

    if(file == NULL) return -1;
    if(vqeaf_ui_read_file(file, &buffer, &size) != 0 || buffer == NULL) {
        snprintf(error, cap, "Không đọc được tệp từ QEAFRESCUE.");
        return -1;
    }
    return install_buffer(file, buffer, size, error, cap);
#else
    (void)file;
    seed();
    if(error != NULL && cap > 0) snprintf(error, cap, "Host render demo");
    return 0;
#endif
}

int vqeaf_qeapp_preview(const char *file, vqeaf_qeapp_preview_t *out,
                        char *error, size_t cap)
{
#ifdef __linux__
    uint8_t *buffer = NULL;
    size_t size = 0;
    int rc;

    if(file == NULL || out == NULL) return -1;
    if(vqeaf_ui_read_file(file, &buffer, &size) != 0 || buffer == NULL) {
        snprintf(error, cap, "Không đọc được tệp từ QEAFRESCUE.");
        return -1;
    }
    rc = read_metadata(buffer, size, out, error, cap);
    free(buffer);
    return rc;
#else
    /* Host render demo: the desktop has no ZIP/verify path, so the
     * confirmation layout is driven by the seeded app. The device path above
     * parses the real manifest; these demo permissions are layout data, not a
     * claim about any device. */
    (void)file;
    seed();
    if(out == NULL) return -1;
    memset(out, 0, sizeof(*out));
    snprintf(out->id, sizeof(out->id), "%s", g_apps[0].id);
    snprintf(out->name, sizeof(out->name), "%s", g_apps[0].name);
    snprintf(out->version, sizeof(out->version), "%s", g_apps[0].version);
    snprintf(out->message, sizeof(out->message), "%s", g_apps[0].message);
    out->native = g_apps[0].native;
    out->has_icon = g_apps[0].has_icon;
    memcpy(out->icon, g_apps[0].icon, sizeof(out->icon));
    out->permissions = (1u << 0) | (1u << 1);
    if(error != NULL && cap > 0) snprintf(error, cap, "Host render demo");
    return 0;
#endif
}

/* Boot path: the p77 catalog is the source, so no package file has to exist
 * in QEAFRESCUE (which resets on every boot). */
int vqeaf_qeapp_install_stored(const char *name, char *error, size_t cap)
{
#ifdef __linux__
    uint8_t *buffer = NULL;
    size_t size = 0;

    if(name == NULL) return -1;
    if(appstore_read(name, &buffer, &size) != 0 || buffer == NULL) {
        snprintf(error, cap, "Không đọc được gói từ kho p77.");
        return -1;
    }
    return install_buffer(name, buffer, size, error, cap);
#else
    (void)name; (void)error; (void)cap;
    seed();
    return 0;
#endif
}

static int g_data_index = -1;
static char g_data_text[512];
static volatile int g_data_pending;
static volatile int g_uninstall_index = -1;

void vqeaf_qeapp_request_data(int index, const char *text)
{
    snprintf(g_data_text, sizeof(g_data_text), "%s", text != NULL ? text : "");
    g_data_index = index;
    g_data_pending = 1;
}

int vqeaf_qeapp_take_data_request(int *index, char *out, size_t cap)
{
    if(!g_data_pending || index == NULL || out == NULL || cap == 0) return 0;
    g_data_pending = 0;
    *index = g_data_index;
    snprintf(out, cap, "%s", g_data_text);
    return 1;
}

int vqeaf_qeapp_data_pending(void)
{
    return g_data_pending != 0;
}

int vqeaf_qeapp_uninstall_pending(void)
{
    return g_uninstall_index >= 0;
}

void vqeaf_qeapp_request_uninstall(int index)
{
    g_uninstall_index = index;
}

int vqeaf_qeapp_take_uninstall_request(void)
{
    int index = g_uninstall_index;
    g_uninstall_index = -1;
    return index;
}

int vqeaf_qeapp_data_read(int index, const uint8_t **out, size_t *size)
{
#ifndef __linux__
    seed();
#endif
    if(index < 0 || index >= g_app_count || out == NULL || size == NULL)
        return -1;
    *out = g_apps[index].data;
    *size = g_apps[index].data_size;
    return g_apps[index].data != NULL ? 0 : -1;
}

int vqeaf_qeapp_data_write(int index, const void *buf, size_t size)
{
    uint8_t *copy = NULL;

#ifndef __linux__
    seed();
#endif
    if(index < 0 || index >= g_app_count) return -1;
    if(size > 64u * 1024u) return -1;
    if(size > 0) {
        copy = malloc(size);
        if(copy == NULL) return -1;
        memcpy(copy, buf, size);
    }
    free(g_apps[index].data);
    g_apps[index].data = copy;
    g_apps[index].data_size = size;
#ifdef __linux__
    persist_apps();
#endif
    return 0;
}

int vqeaf_qeapp_package(int index, uint8_t **out, size_t *size)
{
#ifdef __linux__
    if(index < 0 || index >= g_app_count) return -1;
    return appstore_read(g_apps[index].package_file, out, size);
#else
    (void)index; (void)out; (void)size;
    return -1;
#endif
}

int vqeaf_qeapp_count(void)
{
#ifndef __linux__
    seed();
#endif
    return g_app_count;
}

const vqeaf_qeapp_t *vqeaf_qeapp_at(int index)
{
#ifndef __linux__
    seed();
#endif
    if(index < 0 || index >= g_app_count) return NULL;
    return &g_apps[index];
}

int vqeaf_qeapp_uninstall(int index)
{
    if(index < 0 || index >= g_app_count) return -1;
    /* The catalog rewrite drops this app's record, and with it the data. */
    free(g_apps[index].data);
    g_apps[index].data = NULL;
    g_apps[index].data_size = 0;
    for(int i = index; i + 1 < g_app_count; i++) g_apps[i] = g_apps[i + 1];
    g_app_count--;
#ifdef __linux__
    persist_apps();
#endif
    if(g_selected >= g_app_count) g_selected = g_app_count - 1;
    if(g_selected < 0) g_selected = 0;
    return 0;
}


int vqeaf_qeapp_set_permission(int index, uint32_t bit, int granted)
{
    const uint32_t known = (1u << 5) - 1u;
    if(index < 0 || index >= g_app_count || bit == 0u ||
       (bit & (bit - 1u)) != 0u || (bit & known) == 0u)
        return -1;
    if((g_apps[index].permissions & bit) == 0u)
        return -1; /* never grant a capability absent from the signed manifest */
    if(granted)
        g_apps[index].granted_permissions |= bit;
    else
        g_apps[index].granted_permissions &= ~bit;
#ifdef __linux__
    persist_apps();
#endif
    return 0;
}

uint32_t vqeaf_qeapp_effective_permissions(int index)
{
    if(index < 0 || index >= g_app_count) return 0u;
    return g_apps[index].permissions & g_apps[index].granted_permissions;
}

int vqeaf_qeapp_selected(void)
{
    return g_selected;
}

void vqeaf_qeapp_select(int index)
{
    if(index >= 0 && index < g_app_count) g_selected = index;
}

const lv_image_dsc_t *vqeaf_qeapp_icon(int index)
{
#ifndef __linux__
    seed();
#endif
    if(index < 0 || index >= g_app_count) return NULL;
    if(!g_apps[index].has_icon) return NULL;
    g_apps[index].icon_dsc.data = g_apps[index].icon;
    return &g_apps[index].icon_dsc;
}
