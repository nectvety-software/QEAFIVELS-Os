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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "vqeaf_qeapp.h"

#include <vqeaf/qeapp_package.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
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
static lv_image_dsc_t g_icon_dsc[QEAPP_MAX];
static int g_app_count;
static int g_selected;
static int g_last_status = VQEAF_QEAPP_PACKAGE_OK;
static int g_last_persisted;
static void refresh_icon_descriptor(int index);

int vqeaf_qeapp_last_status(void)
{
#ifdef __linux__
    return g_last_status;
#else
    return VQEAF_QEAPP_PACKAGE_UNSUPPORTED;
#endif
}

int vqeaf_qeapp_persisted(void)
{
    return g_last_persisted;
}

static int package_error(int status, char *error, size_t cap,
                         const char *message)
{
    g_last_status = status;
    if(error != NULL && cap > 0) snprintf(error, cap, "%s", message);
    return -1;
}

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

static void package_sha256(const uint8_t *data, size_t length, uint8_t out[32])
{
    sha256_t ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, data, length);
    sha256_final(&ctx, out);
}

static int hash_equal(const uint8_t *a, const uint8_t *b)
{
    uint32_t difference = 0u;

    for(size_t i = 0; i < 32u; i++) difference |= a[i] ^ b[i];
    return difference == 0u;
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
    uint32_t local_offset;
    uint32_t crc;
    uint16_t flags;
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

static uint32_t zip_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xffffffffu;

    for(size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for(int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return crc ^ 0xffffffffu;
}

static int zip_name_safe(const uint8_t *name, size_t length)
{
    size_t start = 0;

    if(length == 0 || name[0] == '/' || name[0] == '\\' ||
       name[length - 1] == '\\')
        return 0;
    for(size_t i = 0; i < length; i++) {
        if(name[i] < 0x20u || name[i] == 0x7fu || name[i] == '\\' ||
           name[i] == ':') return 0;
        if(name[i] != '/') continue;
        size_t part = i - start;
        if(part == 0 || (part == 1 && name[start] == '.') ||
           (part == 2 && name[start] == '.' && name[start + 1] == '.'))
            return 0;
        start = i + 1;
    }
    {
        size_t part = length - start;
        return part != 0 &&
               !(part == 1 && name[start] == '.') &&
               !(part == 2 && name[start] == '.' && name[start + 1] == '.');
    }
}

static int zip_central_directory(const uint8_t *zip, size_t size,
                                 const zip_entry_t *entries, int count,
                                 size_t offset, char *error, size_t cap)
{
    size_t central_start = offset;

    if(offset + 4 > size || le32(zip + offset) != 0x02014b50u) {
        snprintf(error, cap, "Thieu central directory ZIP.");
        return -1;
    }
    for(int i = 0; i < count; i++) {
        uint16_t flags;
        uint16_t method;
        uint32_t csize;
        uint32_t usize;
        uint32_t crc;
        uint32_t local_offset;
        uint16_t nlen;
        uint16_t elen;
        uint16_t clen;
        size_t data_offset;
        int found = 0;

        if(offset + 46 > size || le32(zip + offset) != 0x02014b50u) {
            snprintf(error, cap, "Central directory ZIP bi hung.");
            return -1;
        }
        flags = le16(zip + offset + 8);
        method = le16(zip + offset + 10);
        crc = le32(zip + offset + 16);
        csize = le32(zip + offset + 20);
        usize = le32(zip + offset + 24);
        nlen = le16(zip + offset + 28);
        elen = le16(zip + offset + 30);
        clen = le16(zip + offset + 32);
        local_offset = le32(zip + offset + 42);
        data_offset = offset + 46 + nlen + elen + clen;
        if(data_offset > size || (flags & (1u | 8u | 0x40u | 0x2000u)) != 0u ||
           method != 0 || csize != usize ||
           ((le32(zip + offset + 38) >> 16) & 0170000u) == 0120000u ||
           !zip_name_safe(zip + offset + 46, nlen)) {
            snprintf(error, cap, "Central directory ZIP khong hop le.");
            return -1;
        }
        for(int j = 0; j < count; j++) {
            if(entries[j].local_offset != local_offset ||
               entries[j].name_len != nlen ||
               memcmp(entries[j].name, zip + offset + 46, nlen) != 0)
                continue;
            if(entries[j].flags != flags || entries[j].size != csize ||
               entries[j].crc != crc) {
                snprintf(error, cap, "Local/central ZIP khong khop.");
                return -1;
            }
            found = 1;
            break;
        }
        if(!found) {
            snprintf(error, cap, "Central directory ZIP thieu local entry.");
            return -1;
        }
        offset = data_offset;
    }
    if(offset + 22 > size || le32(zip + offset) != 0x06054b50u) {
        snprintf(error, cap, "Thieu EOCD ZIP.");
        return -1;
    }
    if(le16(zip + offset + 4) != 0u || le16(zip + offset + 6) != 0u ||
       le16(zip + offset + 8) != (uint16_t)count ||
       le16(zip + offset + 10) != (uint16_t)count ||
       le32(zip + offset + 12) != (uint32_t)(offset - central_start) ||
       le32(zip + offset + 16) != (uint32_t)central_start ||
       offset + 22u + le16(zip + offset + 20) != size) {
        snprintf(error, cap, "EOCD ZIP khong khop hoac co du lieu thua.");
        return -1;
    }
    return 0;
}

static int zip_entries(const uint8_t *zip, size_t size, zip_entry_t *out,
                       int max, char *error, size_t cap)
{
    size_t offset = 0;
    int count = 0;

    while(offset + 30 <= size && le32(zip + offset) == 0x04034b50u) {
        uint16_t flags = le16(zip + offset + 6);
        uint16_t method = le16(zip + offset + 8);
        uint32_t crc = le32(zip + offset + 14);
        uint32_t csize = le32(zip + offset + 18);
        uint32_t usize = le32(zip + offset + 22);
        uint16_t nlen = le16(zip + offset + 26);
        uint16_t elen = le16(zip + offset + 28);
        const uint8_t *name;
        size_t data_offset;

        if(count >= max) {
            snprintf(error, cap, "QEAPP vuot qua %d entry.", max);
            return -1;
        }
        if(offset + 30 > size || (size_t)nlen > size - offset - 30) {
            snprintf(error, cap, "Local header ZIP bi hang.");
            return -1;
        }
        name = zip + offset + 30;
        data_offset = offset + 30 + nlen;
        if((size_t)elen > size - data_offset) {
            snprintf(error, cap, "Local header ZIP bi hang.");
            return -1;
        }
        data_offset += elen;
        if(!zip_name_safe(name, nlen)) {
            snprintf(error, cap, "Entry ZIP co duong dan khong an toan.");
            return -1;
        }
        for(int i = 0; i < count; i++) {
            if(out[i].name_len == nlen &&
               memcmp(out[i].name, name, nlen) == 0) {
                snprintf(error, cap, "Entry ZIP trung ten.");
                return -1;
            }
        }
        if((flags & (1u | 8u | 0x40u | 0x2000u)) != 0u || method != 0) {
            snprintf(error, cap, "QEAPP phải là ZIP stored (không mã hóa).");
            return -1;
        }
        if((size_t)csize > size - data_offset || csize != usize ||
           zip_crc32(zip + data_offset, csize) != crc) {
            snprintf(error, cap, "Entry ZIP hỏng hoặc vượt biên.");
            return -1;
        }
        out[count].name = (const char *)name;
        out[count].name_len = nlen;
        out[count].data = zip + data_offset;
        out[count].size = csize;
        out[count].local_offset = (uint32_t)offset;
        out[count].crc = crc;
        out[count].flags = flags;
        count++;
        offset = data_offset + csize;
    }
    if(zip_central_directory(zip, size, out, count, offset, error, cap) != 0)
        return -1;
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

    if(signature == NULL || signature->size != 64u) return -1;
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
    for(int i = 0; i < 64; i++)
        if(signature->data[i] != (uint8_t)hex[i]) return -1;
    return 0;
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

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
} manifest_parser_t;

enum {
    MANIFEST_FORMAT = 1u << 0,
    MANIFEST_FORMAT_VERSION = 1u << 1,
    MANIFEST_ID = 1u << 2,
    MANIFEST_NAME = 1u << 3,
    MANIFEST_VERSION = 1u << 4,
    MANIFEST_MIN_OS = 1u << 5,
    MANIFEST_ENTRY = 1u << 6,
    MANIFEST_ARCHITECTURE = 1u << 7,
    MANIFEST_PERMISSIONS = 1u << 8,
    MANIFEST_DEPENDENCIES = 1u << 9,
    MANIFEST_RUNTIME = 1u << 10,
    MANIFEST_ABI_VERSION = 1u << 11,
    MANIFEST_ICON = 1u << 12,
    MANIFEST_REQUIRED = (1u << 10) - 1u
};

typedef struct {
    char format[16];
    char id[64];
    char name[64];
    char version[24];
    char min_os[24];
    char entry[128];
    char architecture[16];
    char runtime[16];
    char icon[128];
    int format_version;
    int abi_version;
    int architecture_count;
    int dependency_count;
    uint32_t permissions;
    unsigned seen;
} manifest_values_t;

static void manifest_ws(manifest_parser_t *parser)
{
    while(parser->pos < parser->size &&
          (parser->data[parser->pos] == ' ' ||
           parser->data[parser->pos] == '\t' ||
           parser->data[parser->pos] == '\r' ||
           parser->data[parser->pos] == '\n'))
        parser->pos++;
}

static int utf8_valid(const uint8_t *text, size_t length)
{
    size_t i = 0;

    while(i < length) {
        uint8_t c = text[i++];
        size_t extra;
        uint32_t code;
        uint32_t minimum;
        if(c < 0x80u) continue;
        if(c >= 0xc2u && c <= 0xdfu) {
            extra = 1; code = c & 0x1fu; minimum = 0x80u;
        }
        else if(c >= 0xe0u && c <= 0xefu) {
            extra = 2; code = c & 0x0fu; minimum = 0x800u;
        }
        else if(c >= 0xf0u && c <= 0xf4u) {
            extra = 3; code = c & 0x07u; minimum = 0x10000u;
        }
        else return 0;
        if(extra > length - i) return 0;
        for(size_t j = 0; j < extra; j++) {
            uint8_t next = text[i++];
            if((next & 0xc0u) != 0x80u) return 0;
            code = (code << 6) | (next & 0x3fu);
        }
        if(code < minimum || code > 0x10ffffu ||
           (code >= 0xd800u && code <= 0xdfffu))
            return 0;
    }
    return 1;
}

static int manifest_put(char *out, size_t cap, size_t *used,
                        const uint8_t *bytes, size_t length)
{
    if(out != NULL) {
        if(*used > cap || length >= cap - *used) return -1;
        memcpy(out + *used, bytes, length);
    }
    *used += length;
    return 0;
}

static int manifest_put_codepoint(char *out, size_t cap, size_t *used,
                                  uint32_t code)
{
    uint8_t bytes[4];
    size_t count;

    if(code <= 0x7fu) {
        bytes[0] = (uint8_t)code; count = 1;
    }
    else if(code <= 0x7ffu) {
        bytes[0] = (uint8_t)(0xc0u | (code >> 6));
        bytes[1] = (uint8_t)(0x80u | (code & 0x3fu)); count = 2;
    }
    else if(code <= 0xffffu) {
        bytes[0] = (uint8_t)(0xe0u | (code >> 12));
        bytes[1] = (uint8_t)(0x80u | ((code >> 6) & 0x3fu));
        bytes[2] = (uint8_t)(0x80u | (code & 0x3fu)); count = 3;
    }
    else {
        bytes[0] = (uint8_t)(0xf0u | (code >> 18));
        bytes[1] = (uint8_t)(0x80u | ((code >> 12) & 0x3fu));
        bytes[2] = (uint8_t)(0x80u | ((code >> 6) & 0x3fu));
        bytes[3] = (uint8_t)(0x80u | (code & 0x3fu)); count = 4;
    }
    return manifest_put(out, cap, used, bytes, count);
}

static int manifest_hex4(manifest_parser_t *parser, uint32_t *out)
{
    uint32_t value = 0;

    if(parser->size - parser->pos < 4u) return -1;
    for(int i = 0; i < 4; i++) {
        uint8_t c = parser->data[parser->pos++];
        value <<= 4;
        if(c >= '0' && c <= '9') value |= c - '0';
        else if(c >= 'a' && c <= 'f') value |= c - 'a' + 10u;
        else if(c >= 'A' && c <= 'F') value |= c - 'A' + 10u;
        else return -1;
    }
    *out = value;
    return 0;
}

static int manifest_string(manifest_parser_t *parser, char *out, size_t cap)
{
    size_t used = 0;
    size_t segment;

    if(parser->pos >= parser->size || parser->data[parser->pos++] != '"')
        return -1;
    segment = parser->pos;
    while(parser->pos < parser->size) {
        uint8_t c = parser->data[parser->pos];
        if(c == '"') {
            if(!utf8_valid(parser->data + segment, parser->pos - segment))
                return -1;
            if(manifest_put(out, cap, &used, parser->data + segment,
                            parser->pos - segment) != 0)
                return -1;
            parser->pos++;
            if(out != NULL) out[used] = '\0';
            return 0;
        }
        if(c < 0x20u) return -1;
        if(c != '\\') {
            parser->pos++;
            continue;
        }
        if(!utf8_valid(parser->data + segment, parser->pos - segment) ||
           manifest_put(out, cap, &used, parser->data + segment,
                        parser->pos - segment) != 0)
            return -1;
        parser->pos++;
        if(parser->pos >= parser->size) return -1;
        c = parser->data[parser->pos++];
        switch(c) {
        case '"': case '\\': case '/':
            if(manifest_put(out, cap, &used, &c, 1) != 0) return -1;
            break;
        case 'b': c = '\b'; if(manifest_put(out, cap, &used, &c, 1) != 0) return -1; break;
        case 'f': c = '\f'; if(manifest_put(out, cap, &used, &c, 1) != 0) return -1; break;
        case 'n': c = '\n'; if(manifest_put(out, cap, &used, &c, 1) != 0) return -1; break;
        case 'r': c = '\r'; if(manifest_put(out, cap, &used, &c, 1) != 0) return -1; break;
        case 't': c = '\t'; if(manifest_put(out, cap, &used, &c, 1) != 0) return -1; break;
        case 'u': {
            uint32_t code;
            if(manifest_hex4(parser, &code) != 0) return -1;
            if(code >= 0xd800u && code <= 0xdbffu) {
                uint32_t low;
                if(parser->size - parser->pos < 6u ||
                   parser->data[parser->pos] != '\\' ||
                   parser->data[parser->pos + 1] != 'u') return -1;
                parser->pos += 2;
                if(manifest_hex4(parser, &low) != 0 || low < 0xdc00u ||
                   low > 0xdfffu) return -1;
                code = 0x10000u + ((code - 0xd800u) << 10) + (low - 0xdc00u);
            }
            else if((code >= 0xdc00u && code <= 0xdfffu) || code == 0u) return -1;
            if(manifest_put_codepoint(out, cap, &used, code) != 0) return -1;
            break;
        }
        default: return -1;
        }
        segment = parser->pos;
    }
    return -1;
}

static int manifest_number(manifest_parser_t *parser)
{
    if(parser->pos < parser->size && parser->data[parser->pos] == '-')
        parser->pos++;
    if(parser->pos >= parser->size) return -1;
    if(parser->data[parser->pos] == '0') parser->pos++;
    else {
        if(parser->data[parser->pos] < '1' ||
           parser->data[parser->pos] > '9') return -1;
        while(parser->pos < parser->size && parser->data[parser->pos] >= '0' &&
              parser->data[parser->pos] <= '9') parser->pos++;
    }
    if(parser->pos < parser->size && parser->data[parser->pos] == '.') {
        parser->pos++;
        if(parser->pos >= parser->size || parser->data[parser->pos] < '0' ||
           parser->data[parser->pos] > '9') return -1;
        while(parser->pos < parser->size && parser->data[parser->pos] >= '0' &&
              parser->data[parser->pos] <= '9') parser->pos++;
    }
    if(parser->pos < parser->size &&
       (parser->data[parser->pos] == 'e' || parser->data[parser->pos] == 'E')) {
        parser->pos++;
        if(parser->pos < parser->size &&
           (parser->data[parser->pos] == '+' || parser->data[parser->pos] == '-'))
            parser->pos++;
        if(parser->pos >= parser->size || parser->data[parser->pos] < '0' ||
           parser->data[parser->pos] > '9') return -1;
        while(parser->pos < parser->size && parser->data[parser->pos] >= '0' &&
              parser->data[parser->pos] <= '9') parser->pos++;
    }
    return 0;
}

static int manifest_skip_value(manifest_parser_t *parser, int depth)
{
    if(depth > 16) return -1;
    manifest_ws(parser);
    if(parser->pos >= parser->size) return -1;
    if(parser->data[parser->pos] == '"')
        return manifest_string(parser, NULL, 0);
    if(parser->data[parser->pos] == '{') {
        parser->pos++;
        manifest_ws(parser);
        if(parser->pos < parser->size && parser->data[parser->pos] == '}') {
            parser->pos++;
            return 0;
        }
        for(;;) {
            if(manifest_string(parser, NULL, 0) != 0) return -1;
            manifest_ws(parser);
            if(parser->pos >= parser->size || parser->data[parser->pos++] != ':')
                return -1;
            if(manifest_skip_value(parser, depth + 1) != 0) return -1;
            manifest_ws(parser);
            if(parser->pos >= parser->size) return -1;
            if(parser->data[parser->pos] == '}') {
                parser->pos++;
                return 0;
            }
            if(parser->data[parser->pos++] != ',') return -1;
            manifest_ws(parser);
        }
    }
    if(parser->data[parser->pos] == '[') {
        parser->pos++;
        manifest_ws(parser);
        if(parser->pos < parser->size && parser->data[parser->pos] == ']') {
            parser->pos++;
            return 0;
        }
        for(;;) {
            if(manifest_skip_value(parser, depth + 1) != 0) return -1;
            manifest_ws(parser);
            if(parser->pos >= parser->size) return -1;
            if(parser->data[parser->pos] == ']') {
                parser->pos++;
                return 0;
            }
            if(parser->data[parser->pos++] != ',') return -1;
        }
    }
    if(parser->size - parser->pos >= 4u &&
       memcmp(parser->data + parser->pos, "true", 4) == 0) {
        parser->pos += 4; return 0;
    }
    if(parser->size - parser->pos >= 5u &&
       memcmp(parser->data + parser->pos, "false", 5) == 0) {
        parser->pos += 5; return 0;
    }
    if(parser->size - parser->pos >= 4u &&
       memcmp(parser->data + parser->pos, "null", 4) == 0) {
        parser->pos += 4; return 0;
    }
    if(parser->data[parser->pos] == '-' ||
       (parser->data[parser->pos] >= '0' && parser->data[parser->pos] <= '9'))
        return manifest_number(parser);
    return -1;
}

static int manifest_integer(manifest_parser_t *parser, int *out)
{
    int value = 0;

    manifest_ws(parser);
    if(parser->pos >= parser->size || parser->data[parser->pos] < '0' ||
       parser->data[parser->pos] > '9')
        return -1;
    while(parser->pos < parser->size && parser->data[parser->pos] >= '0' &&
           parser->data[parser->pos] <= '9') {
        int digit = parser->data[parser->pos++] - '0';
        if(value > (INT_MAX - digit) / 10) return -1;
        value = value * 10 + digit;
    }
    *out = value;
    return 0;
}

static int manifest_string_array(manifest_parser_t *parser, int kind,
                                 manifest_values_t *values)
{
    int count = 0;

    manifest_ws(parser);
    if(parser->pos >= parser->size || parser->data[parser->pos++] != '[')
        return -1;
    manifest_ws(parser);
    if(parser->pos < parser->size && parser->data[parser->pos] == ']') {
        parser->pos++;
        return kind == 1 ? -1 : 0;
    }
    for(;;) {
        char text[128];
        if(manifest_string(parser, text, sizeof(text)) != 0) return -1;
        if(kind == 1) {
            if(strcmp(text, "arm64") != 0 && strcmp(text, "x86_64") != 0)
                return -1;
            if(count == 0) {
                if(snprintf(values->architecture, sizeof(values->architecture),
                            "%s", text) < 0) return -1;
            }
            else if(strcmp(text, values->architecture) == 0)
                return -1;
        }
        else if(kind == 2) {
            uint32_t bit = permission_bit(text);
            if(bit == 0u) return -1;
            values->permissions |= bit;
        }
        else if(text[0] == '\0') return -1;
        count++;
        manifest_ws(parser);
        if(parser->pos >= parser->size) return -1;
        if(parser->data[parser->pos] == ']') {
            parser->pos++;
            if(kind == 1) values->architecture_count = count;
            if(kind == 3) values->dependency_count = count;
            return 0;
        }
        if(parser->data[parser->pos++] != ',') return -1;
        manifest_ws(parser);
    }
}

static int manifest_parse(const zip_entry_t *entry, manifest_values_t *values,
                          char *error, size_t cap)
{
    manifest_parser_t parser = { entry->data, entry->size, 0 };

    memset(values, 0, sizeof(*values));
    manifest_ws(&parser);
    if(parser.pos >= parser.size || parser.data[parser.pos++] != '{')
        goto invalid;
    manifest_ws(&parser);
    if(parser.pos < parser.size && parser.data[parser.pos] == '}') {
        parser.pos++;
        goto missing;
    }
    for(;;) {
        char key[128];
        char *text = NULL;
        size_t text_cap = 0;
        unsigned field = 0u;

        if(manifest_string(&parser, key, sizeof(key)) != 0) goto invalid;
        manifest_ws(&parser);
        if(parser.pos >= parser.size || parser.data[parser.pos++] != ':')
            goto invalid;
        manifest_ws(&parser);

        if(strcmp(key, "format") == 0) {
            field = MANIFEST_FORMAT; text = values->format;
            text_cap = sizeof(values->format);
        }
        else if(strcmp(key, "id") == 0) {
            field = MANIFEST_ID; text = values->id; text_cap = sizeof(values->id);
        }
        else if(strcmp(key, "name") == 0) {
            field = MANIFEST_NAME; text = values->name; text_cap = sizeof(values->name);
        }
        else if(strcmp(key, "version") == 0) {
            field = MANIFEST_VERSION; text = values->version;
            text_cap = sizeof(values->version);
        }
        else if(strcmp(key, "min_os") == 0) {
            field = MANIFEST_MIN_OS; text = values->min_os;
            text_cap = sizeof(values->min_os);
        }
        else if(strcmp(key, "entry") == 0) {
            field = MANIFEST_ENTRY; text = values->entry;
            text_cap = sizeof(values->entry);
        }
        else if(strcmp(key, "runtime") == 0) {
            field = MANIFEST_RUNTIME; text = values->runtime;
            text_cap = sizeof(values->runtime);
        }
        else if(strcmp(key, "icon") == 0) {
            field = MANIFEST_ICON; text = values->icon;
            text_cap = sizeof(values->icon);
        }

        if(field != 0u) {
            if((values->seen & field) != 0u ||
               manifest_string(&parser, text, text_cap) != 0)
                goto invalid;
            values->seen |= field;
        }
        else if(strcmp(key, "format_version") == 0) {
            if((values->seen & MANIFEST_FORMAT_VERSION) != 0u ||
               manifest_integer(&parser, &values->format_version) != 0)
                goto invalid;
            values->seen |= MANIFEST_FORMAT_VERSION;
        }
        else if(strcmp(key, "abi_version") == 0) {
            if((values->seen & MANIFEST_ABI_VERSION) != 0u ||
               manifest_integer(&parser, &values->abi_version) != 0)
                goto invalid;
            values->seen |= MANIFEST_ABI_VERSION;
        }
        else if(strcmp(key, "architecture") == 0) {
            if((values->seen & MANIFEST_ARCHITECTURE) != 0u ||
               manifest_string_array(&parser, 1, values) != 0)
                goto invalid;
            values->seen |= MANIFEST_ARCHITECTURE;
        }
        else if(strcmp(key, "permissions") == 0) {
            if((values->seen & MANIFEST_PERMISSIONS) != 0u ||
               manifest_string_array(&parser, 2, values) != 0)
                goto invalid;
            values->seen |= MANIFEST_PERMISSIONS;
        }
        else if(strcmp(key, "dependencies") == 0) {
            if((values->seen & MANIFEST_DEPENDENCIES) != 0u ||
               manifest_string_array(&parser, 3, values) != 0)
                goto invalid;
            values->seen |= MANIFEST_DEPENDENCIES;
        }
        else if(manifest_skip_value(&parser, 1) != 0) goto invalid;

        manifest_ws(&parser);
        if(parser.pos >= parser.size) goto invalid;
        if(parser.data[parser.pos] == '}') {
            parser.pos++;
            break;
        }
        if(parser.data[parser.pos++] != ',') goto invalid;
        manifest_ws(&parser);
    }
    manifest_ws(&parser);
    if(parser.pos != parser.size) goto invalid;
    if((values->seen & MANIFEST_REQUIRED) != MANIFEST_REQUIRED) goto missing;
    return 0;

invalid:
    return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                         "Manifest JSON khong hop le.");
missing:
    return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                         "Manifest thieu truong bat buoc.");
}

static int valid_package_id(const char *id)
{
    size_t length = strlen(id);
    int separator = 0;
    int segment = 0;

    if(length == 0 || length >= 64u) return 0;
    for(size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)id[i];
        if(islower(c) || isdigit(c)) {
            segment = 1;
            continue;
        }
        if((c == '.' || c == '-') && segment) {
            separator = 1;
            segment = 0;
            continue;
        }
        return 0;
    }
    return segment && separator;
}

static int valid_version(const char *version)
{
    int dots = 0;
    int digits = 0;
    size_t length = strlen(version);

    if(length == 0 || length >= 24u) return 0;
    for(size_t i = 0; i < length; i++) {
        if(isdigit((unsigned char)version[i])) {
            digits = 1;
            continue;
        }
        if(version[i] == '.' && digits) {
            dots++;
            digits = 0;
            continue;
        }
        return 0;
    }
    return dots == 2 && digits;
}

static int safe_entry_path(const char *path, const char *extension)
{
    size_t length = strlen(path);
    size_t ext_length = strlen(extension);
    size_t start = 0;

    if(length == 0 || length >= 128u || path[0] == '/' ||
       length <= ext_length || strcmp(path + length - ext_length, extension) != 0)
        return 0;
    for(size_t i = 0; i <= length; i++) {
        if(i != length && path[i] != '/') {
            unsigned char c = (unsigned char)path[i];
            if(!(isalnum(c) || c == '_' || c == '-' || c == '.')) return 0;
            continue;
        }
        size_t part = i - start;
        if(part == 0 || (part == 2 && path[start] == '.' &&
                         path[start + 1] == '.'))
            return 0;
        start = i + 1;
    }
    return 1;
}

static int valid_display_name(const char *name)
{
    size_t length = strlen(name);
    int visible = 0;
    if(length == 0 || length >= 64u) return 0;
    for(size_t i = 0; i < length; i++) {
        if((unsigned char)name[i] < 0x20u) return 0;
        if(name[i] != ' ') visible = 1;
    }
    return visible;
}

static int script_valid(const uint8_t *script, size_t length)
{
    size_t start = 0;

    if(!utf8_valid(script, length)) return 0;
    for(size_t i = 0; i <= length; i++) {
        if(i != length && script[i] != '\n') continue;
        size_t end = i;
        while(end > start && (script[end - 1] == '\r' ||
                              script[end - 1] == ' ' || script[end - 1] == '\t'))
            end--;
        size_t line = start;
        while(line < end && (script[line] == ' ' || script[line] == '\t'))
            line++;
        if(line == end || script[line] == '#') {
            start = i + 1;
            continue;
        }
        if(end - line > 4 && memcmp(script + line, "app ", 4) == 0) {
            size_t p = line + 4;
            if(p == end || !(isalpha(script[p]) || script[p] == '_')) return 0;
            for(p++; p < end; p++)
                if(!(isalnum(script[p]) || script[p] == '_')) return 0;
        }
        else if(end - line > 6 && memcmp(script + line, "print ", 6) == 0) {
            size_t p = line + 6;
            int closed = 0;
            if(p == end || script[p++] != '"') return 0;
            while(p < end) {
                if(script[p] == '\\') {
                    p++;
                    if(p == end) return 0;
                }
                else if(script[p] == '"') {
                    closed = 1;
                    p++;
                    break;
                }
                p++;
            }
            if(!closed) return 0;
            while(p < end && (script[p] == ' ' || script[p] == '\t')) p++;
            if(p != end) return 0;
        }
        else return 0;
        start = i + 1;
    }
    return 1;
}

static int manifest_common(const manifest_values_t *manifest,
                           char *id, size_t id_cap, char *name, size_t name_cap,
                           char *version, size_t version_cap, char *entry_name,
                           size_t entry_cap, char *arch, size_t arch_cap,
                           int *format_version, uint32_t *permissions,
                           char *error, size_t cap)
{
    *permissions = manifest->permissions;
    if(strcmp(manifest->format, "qeapp") != 0)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Manifest format phai la qeapp.");
    if(manifest->format_version != 1 && manifest->format_version != 2)
        return package_error(VQEAF_QEAPP_PACKAGE_UNSUPPORTED, error, cap,
                             "Manifest format_version chua ho tro.");
    if(!valid_package_id(manifest->id))
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Manifest id khong hop le.");
    if(!valid_display_name(manifest->name))
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Manifest name khong hop le.");
    if(!valid_version(manifest->version))
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Manifest version phai la x.y.z.");
    if(!valid_version(manifest->min_os))
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Manifest min_os phai la x.y.z.");
    if(manifest->entry[0] == '\0')
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Manifest thieu entry.");
    if((manifest->seen & MANIFEST_ICON) != 0u &&
       (!zip_name_safe((const uint8_t *)manifest->icon,
                       strlen(manifest->icon)) ||
        manifest->icon[strlen(manifest->icon) - 1] == '/' ||
        strcmp(manifest->icon, "assets/icon.rgba") != 0))
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Manifest icon phai la assets/icon.rgba.");
    if(manifest->format_version == 2 && manifest->architecture_count != 1)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Format_version 2 can dung mot architecture.");
    if(manifest->architecture_count == 0)
        return package_error(VQEAF_QEAPP_PACKAGE_UNSUPPORTED, error, cap,
                             "Manifest architecture khong ho tro.");
    if(snprintf(id, id_cap, "%s", manifest->id) < 0 ||
       snprintf(name, name_cap, "%s", manifest->name) < 0 ||
       snprintf(version, version_cap, "%s", manifest->version) < 0 ||
       snprintf(entry_name, entry_cap, "%s", manifest->entry) < 0 ||
       snprintf(arch, arch_cap, "%s", manifest->architecture) < 0)
        return package_error(VQEAF_QEAPP_PACKAGE_INTERNAL_ERROR, error, cap,
                             "Manifest vuot qua bo dem dau ra.");
    *format_version = manifest->format_version;
    return 0;
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
    manifest_values_t manifest;
    char id[64], name[64], version[24];
    char entry_name[128];
    char arch[16];
    uint32_t permissions;
    int format_version;
    int abi_version;
    int arch_id;
    int count;

    if(zip == NULL || size == 0 || info == NULL || module == NULL ||
       module_size == NULL)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT, error, cap,
                             "Native QEAPP input khong hop le.");
    if(size > QEAPP_FILE_CAP)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "QEAPP vuot qua 512 KiB.");
    *module = NULL;
    *module_size = 0;
    memset(info, 0, sizeof(*info));
    g_last_status = VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE;

    count = zip_entries(zip, size, entries, QEAPP_ZIP_MAX_ENTRIES, error, cap);
    if(count < 0) return -1;
    manifest_entry = zip_find(entries, count, "manifest.json");
    signature = zip_find(entries, count, "SIGNATURE");
    if(manifest_entry == NULL)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Native QEAPP thieu manifest.json.");
    if(signature == NULL)
        return package_error(VQEAF_QEAPP_PACKAGE_SIGNATURE_ERROR, error, cap,
                             "Native QEAPP thieu SIGNATURE.");
    if(verify_signature(entries, count, signature) != 0)
        return package_error(VQEAF_QEAPP_PACKAGE_SIGNATURE_ERROR, error, cap,
                             "Native QEAPP chu ky khong hop le.");
    if(manifest_parse(manifest_entry, &manifest, error, cap) != 0 ||
       manifest_common(&manifest, id, sizeof(id), name, sizeof(name),
                       version, sizeof(version), entry_name, sizeof(entry_name),
                       arch, sizeof(arch), &format_version, &permissions,
                       error, cap) != 0)
        return -1;
    if(format_version != 2)
        return package_error(VQEAF_QEAPP_PACKAGE_UNSUPPORTED, error, cap,
                             "Native QEAPP can format_version 2.");
    abi_version = manifest.abi_version;
    if((manifest.seen & MANIFEST_ABI_VERSION) == 0u ||
       (abi_version != 2 && abi_version != 3 && abi_version != 4 &&
        abi_version != 5))
        return package_error(VQEAF_QEAPP_PACKAGE_UNSUPPORTED, error, cap,
                             "Native QEAPP can abi_version 2, 3, 4 hoac 5.");
    if((manifest.seen & MANIFEST_RUNTIME) == 0u ||
       strcmp(manifest.runtime, "native") != 0)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Native QEAPP can runtime native.");
    if(!safe_entry_path(entry_name, ".qen"))
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Native QEAPP entry khong an toan.");
    arch_id = vqeaf_qeapp_native_arch_id(arch);
    if(arch_id == 0)
        return package_error(VQEAF_QEAPP_PACKAGE_UNSUPPORTED, error, cap,
                             "Native QEAPP architecture khong ho tro.");
    entry = zip_find(entries, count, entry_name);
    if(entry == NULL)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Native QEAPP thieu module .qen.");
    if(vqeaf_qeapp_native_validate_module(entry->data, entry->size,
                                          arch_id) != 0)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Native QEAPP module khong hop le.");
    if(entry->data[10] != (uint8_t)abi_version ||
       (uint16_t)(entry->data[53] | (entry->data[54] << 8)) !=
           (uint16_t)abi_version)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Manifest/module ABI hoac min_api khong khop.");

    info->format_version = format_version;
    info->abi_version = abi_version;
    info->arch_id = arch_id;
    info->permissions = permissions;
    snprintf(info->entry, sizeof(info->entry), "%s", entry_name);
    snprintf(info->arch, sizeof(info->arch), "%s", arch);
    *module = entry->data;
    *module_size = entry->size;
    g_last_status = VQEAF_QEAPP_PACKAGE_OK;
    return 0;
}

#ifdef __linux__
static int read_metadata(const uint8_t *buffer, size_t size,
                         vqeaf_qeapp_preview_t *meta, char *error, size_t cap);

/* New records are keyed by manifest.id. Legacy filename-keyed records remain
 * readable only after their signed metadata proves the same identity. */
static int load_stored_package(const char *id, const char *legacy_name,
                               uint8_t **out, size_t *size,
                               char key[APPSTORE_NAME])
{
    const char *names[2];
    int count = 1;

    *out = NULL;
    *size = 0;
    names[0] = id;
    if(legacy_name != NULL && strcmp(legacy_name, id) != 0) {
        names[1] = legacy_name;
        count = 2;
    }
    for(int i = 0; i < count; i++) {
        uint8_t *bytes = NULL;
        size_t bytes_size = 0;
        vqeaf_qeapp_preview_t meta;
        char error[128];

        if(appstore_read(names[i], &bytes, &bytes_size) != 0 ||
           bytes == NULL)
            continue;
        if(read_metadata(bytes, bytes_size, &meta, error, sizeof(error)) != 0 ||
           strcmp(meta.id, id) != 0) {
            free(bytes);
            return -1;
        }
        snprintf(key, APPSTORE_NAME, "%s", names[i]);
        *out = bytes;
        *size = bytes_size;
        return 0;
    }
    return -1;
}

/* Mirrors the exact verified package bytes retained by each installed record
 * into the p77 catalog; a later FAT file replacement cannot change the save. */
static void persist_apps(void)
{
    appstore_entry_t entries[QEAPP_MAX];
    int n = 0;
    int rc;

    g_last_persisted = 0;
    if(appstore_is_restoring()) {
        g_last_persisted = 1;
        return;
    }
    for(int i = 0; i < g_app_count && n < QEAPP_MAX; i++) {
        snprintf(entries[n].name, sizeof(entries[n].name), "%s",
                 g_apps[i].id);
        entries[n].size = (uint32_t)g_apps[i].package_size;
        entries[n].path = NULL;
        entries[n].data = g_apps[i].package_data;
        entries[n].data_size = g_apps[i].package_size;
        entries[n].app_data = g_apps[i].data;
        entries[n].app_data_size = g_apps[i].data_size;
        entries[n].granted_permissions = g_apps[i].granted_permissions;
        n++;
    }
    rc = appstore_save(entries, n);
    g_last_persisted = rc == 0;
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
    manifest_values_t manifest;
    char entry_name[128], arch[16];
    uint32_t permissions;
    int format_version;
    int count;

    memset(meta, 0, sizeof(*meta));
    if(buffer == NULL || size == 0 || meta == NULL)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT, error, cap,
                             "Gói QEAPP rỗng.");
    if(size > QEAPP_FILE_CAP)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Gói QEAPP vượt quá 512 KiB.");
    g_last_status = VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE;
    count = zip_entries(buffer, size, entries, QEAPP_ZIP_MAX_ENTRIES,
                        error, cap);
    if(count < 0) return -1;
    manifest_entry = zip_find(entries, count, "manifest.json");
    signature = zip_find(entries, count, "SIGNATURE");
    if(manifest_entry == NULL)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Thiếu manifest.json.");
    if(signature == NULL)
        return package_error(VQEAF_QEAPP_PACKAGE_SIGNATURE_ERROR, error, cap,
                             "Thiếu SIGNATURE: chưa ký.");
    if(verify_signature(entries, count, signature) != 0)
        return package_error(VQEAF_QEAPP_PACKAGE_SIGNATURE_ERROR, error, cap,
                             "Chữ ký không hợp lệ hoặc tệp đã bị sửa.");
    if(manifest_parse(manifest_entry, &manifest, error, cap) != 0 ||
       manifest_common(&manifest, meta->id, sizeof(meta->id), meta->name,
                       sizeof(meta->name), meta->version, sizeof(meta->version),
                       entry_name, sizeof(entry_name), arch, sizeof(arch),
                       &format_version, &permissions, error, cap) != 0)
        return -1;
    if((manifest.seen & MANIFEST_RUNTIME) != 0u &&
       strcmp(manifest.runtime, "script") != 0 &&
       strcmp(manifest.runtime, "native") != 0)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Manifest runtime khong ho tro.");
    meta->native = (manifest.seen & MANIFEST_RUNTIME) != 0u &&
                   strcmp(manifest.runtime, "native") == 0;
    if(meta->native && format_version != 2)
        return package_error(VQEAF_QEAPP_PACKAGE_UNSUPPORTED, error, cap,
                             "Runtime native yêu cầu format_version 2.");
    if(format_version == 2 && !meta->native)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                             "Format_version 2 yêu cầu runtime native.");
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
        if(info.arch_id != VQEAF_QEN_ARCH_ARM64)
            return package_error(VQEAF_QEAPP_PACKAGE_UNSUPPORTED, error, cap,
                                 "Ứng dụng native phải là arm64 để chạy trên violet.");
        meta->abi_version = info.abi_version;
        meta->permissions = info.permissions;
        snprintf(meta->message, sizeof(meta->message),
                 "Ứng dụng native arm64 · %s", meta->id);
    }
    else {
        if(!safe_entry_path(entry_name, ".qe"))
            return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                                 "Manifest entry .qe khong an toan.");
        entry = zip_find(entries, count, entry_name);
        if(entry == NULL)
            return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                                 "Thiếu entry .qe khai báo trong manifest.");
        if(!script_valid(entry->data, entry->size))
            return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE, error, cap,
                                 "Entry .qe chua cau lenh hop le.");
        meta->permissions = permissions;
        parse_script(entry->data, entry->size, meta->message,
                     sizeof(meta->message));
    }

    /* Embedded launcher icon: raw 64x64 BGRA from assets/icon.rgba. */
    {
        const zip_entry_t *icon_entry =
            zip_find(entries, count, "assets/icon.rgba");
        if(icon_entry != NULL) {
            if(icon_entry->size != 64u * 64u * 4u)
                return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE,
                                     error, cap,
                                     "Icon runtime phải đúng 64x64 x 4 byte.");
            memcpy(meta->icon, icon_entry->data, 64u * 64u * 4u);
            meta->has_icon = 1;
        }
        else if((manifest.seen & MANIFEST_ICON) != 0u) {
            return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE,
                                 error, cap,
                                 "Thiếu icon runtime khai báo trong manifest.");
        }
    }
    g_last_status = VQEAF_QEAPP_PACKAGE_OK;
    return 0;
}

/* Verifies and registers one package. On success the record retains `buffer`
 * so later catalog saves use the exact verified bytes, not a mutable FAT path. */
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
            free(buffer);
            return package_error(VQEAF_QEAPP_PACKAGE_CAPACITY, error, cap,
                                 "Bộ nhớ ứng dụng đã đầy.");
        }
        g_app_count++;
    }
    if(is_new) {
        /* New install: take over any data blob already stored for this app
         * (boot restore). An update keeps the in-RAM blob untouched. */
        const char *slash = strrchr(file, '/');
        const char *leaf = slash ? slash + 1 : file;

        g_apps[slot].data = NULL;
        g_apps[slot].data_size = 0;
        g_apps[slot].granted_permissions = 0u;
        {
            uint8_t *old_package = NULL;
            size_t old_size = 0;
            char stored_key[APPSTORE_NAME];

            if(load_stored_package(meta.id, leaf, &old_package, &old_size,
                                   stored_key) == 0) {
                (void)appstore_read_grants(stored_key,
                                           &g_apps[slot].granted_permissions);
                (void)appstore_read_data(stored_key, &g_apps[slot].data,
                                         &g_apps[slot].data_size);
                free(old_package);
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
    free(g_apps[slot].package_data);
    g_apps[slot].package_data = buffer;
    g_apps[slot].package_size = size;
    g_apps[slot].native = meta.native;
    /* Kept so the app page reports the ABI the package actually declares
     * instead of one build-wide constant. Boot restore re-reads the package,
     * so the value is recovered at every boot without a catalog change. */
    g_apps[slot].abi_version = meta.abi_version;
    g_apps[slot].permissions = meta.permissions;
    /* Updates never silently grant newly requested capabilities. A fresh
     * install also starts with no grants; the user grants ui.display explicitly. */
    g_apps[slot].granted_permissions &= meta.permissions;

    g_apps[slot].has_icon = meta.has_icon;
    memcpy(g_apps[slot].icon, meta.icon, sizeof(meta.icon));
    memcpy(g_apps[slot].icon_render, meta.icon, sizeof(meta.icon));
    if(meta.has_icon)
        (void)vqeaf_qeapp_icon_display_copy(g_apps[slot].icon_render,
                                            sizeof(g_apps[slot].icon_render),
                                            g_apps[slot].icon,
                                            sizeof(g_apps[slot].icon));
    refresh_icon_descriptor(slot);

    g_selected = slot;
#ifdef __linux__
    persist_apps();
    g_last_status = g_last_persisted ? VQEAF_QEAPP_PACKAGE_OK :
                   VQEAF_QEAPP_PACKAGE_OK_SESSION_ONLY;
#else
    g_last_status = VQEAF_QEAPP_PACKAGE_OK;
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

static int install_file(const char *file, const uint8_t *expected_hash,
                        char *error, size_t cap)
{
#ifdef __linux__
    uint8_t *buffer = NULL;
    size_t size = 0;
    uint8_t actual_hash[32];

    if(file == NULL || file[0] == '\0')
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT, error, cap,
                             "Đường dẫn gói QEAPP không hợp lệ.");
    if(vqeaf_ui_read_file(file, &buffer, &size) != 0 || buffer == NULL)
        return package_error(VQEAF_QEAPP_PACKAGE_IO_ERROR, error, cap,
                             "Không đọc được tệp từ QEAFRESCUE.");
    if(expected_hash != NULL) {
        package_sha256(buffer, size, actual_hash);
        if(!hash_equal(actual_hash, expected_hash)) {
            free(buffer);
            return package_error(VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE,
                                 error, cap,
                                 "Gói đã thay đổi sau bước xác nhận; hãy xem trước lại.");
        }
    }
    return install_buffer(file, buffer, size, error, cap);
#else
    (void)expected_hash;
    seed();
    if(error != NULL && cap > 0) snprintf(error, cap, "Host render demo");
    return 0;
#endif
}

int vqeaf_qeapp_install(const char *file, char *error, size_t cap)
{
    return install_file(file, NULL, error, cap);
}

int vqeaf_qeapp_install_verified(const char *file,
                                 const uint8_t *expected_hash,
                                 char *error, size_t cap)
{
    if(expected_hash == NULL)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT, error, cap,
                             "Cần SHA-256 của gói đã xem trước.");
    return install_file(file, expected_hash, error, cap);
}

int vqeaf_qeapp_preview(const char *file, vqeaf_qeapp_preview_t *out,
                        char *error, size_t cap)
{
#ifdef __linux__
    uint8_t *buffer = NULL;
    size_t size = 0;
    int rc;

    if(file == NULL || file[0] == '\0' || out == NULL)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT, error, cap,
                             "Đầu vào xem trước QEAPP không hợp lệ.");
    if(vqeaf_ui_read_file(file, &buffer, &size) != 0 || buffer == NULL)
        return package_error(VQEAF_QEAPP_PACKAGE_IO_ERROR, error, cap,
                             "Không đọc được tệp từ QEAFRESCUE.");
    rc = read_metadata(buffer, size, out, error, cap);
    if(rc == 0) package_sha256(buffer, size, out->package_hash);
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

    if(name == NULL || name[0] == '\0')
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT, error, cap,
                             "Tên gói trong kho p77 không hợp lệ.");
    if(appstore_read(name, &buffer, &size) != 0 || buffer == NULL)
        return package_error(VQEAF_QEAPP_PACKAGE_IO_ERROR, error, cap,
                             "Không đọc được gói từ kho p77.");
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
    if(index < 0 || index >= g_app_count || (size > 0 && buf == NULL))
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT, NULL, 0,
                             "Invalid app-data write.");
    if(size > 64u * 1024u)
        return package_error(VQEAF_QEAPP_PACKAGE_CAPACITY, NULL, 0,
                             "App data exceeds 64 KiB.");
    if(size > 0) {
        copy = malloc(size);
        if(copy == NULL)
            return package_error(VQEAF_QEAPP_PACKAGE_INTERNAL_ERROR, NULL, 0,
                                 "App-data allocation failed.");
        memcpy(copy, buf, size);
    }
    free(g_apps[index].data);
    g_apps[index].data = copy;
    g_apps[index].data_size = size;
#ifdef __linux__
    persist_apps();
    g_last_status = g_last_persisted ? VQEAF_QEAPP_PACKAGE_OK :
                   VQEAF_QEAPP_PACKAGE_OK_SESSION_ONLY;
#else
    g_last_status = VQEAF_QEAPP_PACKAGE_UNSUPPORTED;
#endif
    return 0;
}

int vqeaf_qeapp_package(int index, uint8_t **out, size_t *size)
{
#ifdef __linux__
    if(index < 0 || index >= g_app_count || out == NULL || size == NULL) return -1;
    if(g_apps[index].package_data != NULL && g_apps[index].package_size > 0) {
        uint8_t *bytes = malloc(g_apps[index].package_size);
        if(bytes == NULL) return -1;
        memcpy(bytes, g_apps[index].package_data, g_apps[index].package_size);
        *out = bytes;
        *size = g_apps[index].package_size;
        return 0;
    }
    {
        const char *path = g_apps[index].package_file;
        const char *leaf = strrchr(path, '/');
        uint8_t *bytes = NULL;
        size_t bytes_size = 0;
        char stored_key[APPSTORE_NAME];

        leaf = leaf ? leaf + 1 : path;
        if(load_stored_package(g_apps[index].id, leaf, &bytes, &bytes_size,
                               stored_key) != 0)
            return -1;
        *out = bytes;
        *size = bytes_size;
        return 0;
    }
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
    if(index < 0 || index >= g_app_count)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT, NULL, 0,
                             "Invalid installed package index.");
    /* The catalog rewrite drops this app's record, and with it the data. */
    free(g_apps[index].data);
    free(g_apps[index].package_data);
    g_apps[index].data = NULL;
    g_apps[index].data_size = 0;
    g_apps[index].package_data = NULL;
    g_apps[index].package_size = 0;
    for(int i = index; i + 1 < g_app_count; i++) g_apps[i] = g_apps[i + 1];
    g_app_count--;
    memset(&g_apps[g_app_count], 0, sizeof(g_apps[g_app_count]));
    for(int i = 0; i < g_app_count; i++) refresh_icon_descriptor(i);
    memset(&g_icon_dsc[g_app_count], 0, sizeof(g_icon_dsc[g_app_count]));
#ifdef __linux__
    persist_apps();
    g_last_status = g_last_persisted ? VQEAF_QEAPP_PACKAGE_OK :
                   VQEAF_QEAPP_PACKAGE_OK_SESSION_ONLY;
#else
    g_last_status = VQEAF_QEAPP_PACKAGE_UNSUPPORTED;
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
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT, NULL, 0,
                             "Invalid permission grant.");
    if((g_apps[index].permissions & bit) == 0u)
        return package_error(VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT, NULL, 0,
                             "Permission is absent from the signed manifest.");
    if(granted)
        g_apps[index].granted_permissions |= bit;
    else
        g_apps[index].granted_permissions &= ~bit;
#ifdef __linux__
    persist_apps();
    g_last_status = g_last_persisted ? VQEAF_QEAPP_PACKAGE_OK :
                   VQEAF_QEAPP_PACKAGE_OK_SESSION_ONLY;
#else
    g_last_status = VQEAF_QEAPP_PACKAGE_UNSUPPORTED;
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

int vqeaf_qeapp_icon_display_copy(uint8_t *out, size_t out_size,
                                  const uint8_t *icon, size_t icon_size)
{
    const size_t bytes = 64u * 64u * 4u;
    uint8_t max_alpha = 0;
    uint8_t *workspace;
    uint16_t *queue;
    uint8_t *seen;
    size_t head = 0;
    size_t tail = 0;

    if(out == NULL || icon == NULL || out_size < bytes || icon_size < bytes)
        return -1;
    memcpy(out, icon, bytes);
    for(size_t i = 3; i < bytes; i += 4)
        if(icon[i] > max_alpha) max_alpha = icon[i];
    if(max_alpha == 0) return 0;

    workspace = malloc(64u * 64u * (sizeof(uint16_t) + 1u));
    if(workspace == NULL) return -1;
    queue = (uint16_t *)workspace;
    seen = workspace + 64u * 64u * sizeof(uint16_t);
    memset(seen, 0, 64u * 64u);
    for(size_t i = 0; i < 64u * 64u; i++) {
        if(icon[i * 4u + 3u] != max_alpha) continue;
        seen[i] = 1;
        queue[tail++] = (uint16_t)i;
    }
    while(head < tail) {
        uint16_t i = queue[head++];
        unsigned x = i & 63u;
        unsigned y = i >> 6;
        static const int dx[4] = { -1, 1, 0, 0 };
        static const int dy[4] = { 0, 0, -1, 1 };
        for(unsigned n = 0; n < 4; n++) {
            int nx = (int)x + dx[n];
            int ny = (int)y + dy[n];
            size_t next;
            if(nx < 0 || nx >= 64 || ny < 0 || ny >= 64) continue;
            next = (size_t)ny * 64u + (unsigned)nx;
            if(seen[next]) continue;
            seen[next] = 1;
            memcpy(out + next * 4u, out + i * 4u, 3);
            queue[tail++] = (uint16_t)next;
        }
    }
    free(workspace);
    return 0;
}

static void refresh_icon_descriptor(int index)
{
    lv_image_dsc_t *dsc = &g_icon_dsc[index];
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_ARGB8888;
    dsc->header.w = 64;
    dsc->header.h = 64;
    dsc->header.stride = 64 * 4;
    dsc->data_size = 64u * 64u * 4u;
    dsc->data = g_apps[index].icon_render;
}

const lv_image_dsc_t *vqeaf_qeapp_icon(int index)
{
#ifndef __linux__
    seed();
#endif
    if(index < 0 || index >= g_app_count) return NULL;
    if(!g_apps[index].has_icon) return NULL;
    refresh_icon_descriptor(index);
    return &g_icon_dsc[index];
}
