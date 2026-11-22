/* SPDX-License-Identifier: MIT */
/* qvf_encode.c - QVF1 clip writer and LZ4 block compressor.
 *
 * Kept dependency-free on purpose (see qvf_encode.h): the device recorder and
 * the host gate build this exact source.
 */
#include "qvf_encode.h"

#include <string.h>

#define LZ4_HASH_BITS 14u
#define LZ4_HASH_SIZE (1u << LZ4_HASH_BITS)
#define LZ4_MIN_MATCH 4u
#define LZ4_LAST_LITERALS 5u
#define LZ4_END_OFFSET 12u

#define QVF_MAGIC 0x31565651u /* "QVF1" little-endian */
#define QVF_VERSION 1u
#define QVF_CODEC_LZ4_RGB565 1u

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t lz4_hash(uint32_t v)
{
    return (v * 2654435761u) >> (32u - (uint32_t)LZ4_HASH_BITS);
}

/* Emits a 15+n extension byte run for the length that did not fit the token. */
static uint32_t lz4_put_ext(uint8_t *dst, uint32_t op, uint32_t length)
{
    while (length >= 255u) {
        dst[op++] = 255u;
        length -= 255u;
    }
    dst[op++] = (uint8_t)length;
    return op;
}

/* Size of a sequence that carries `lit` literals and a `match`-byte match. */
static uint32_t lz4_sequence_need(uint32_t lit, uint32_t match)
{
    uint32_t need = 1u + 2u;                 /* token + 16-bit offset */
    need += lit + (lit >= 15u ? 1u + (lit - 15u) / 255u : 0u);
    if (match != 0u) {
        uint32_t rest = match - LZ4_MIN_MATCH;
        need += (rest >= 15u ? 1u + (rest - 15u) / 255u : 0u);
    }
    return need;
}

uint32_t vio_lz4_compress(const uint8_t *src, uint32_t src_len,
                          uint8_t *dst, uint32_t dst_cap)
{
    static uint32_t table[LZ4_HASH_SIZE];
    uint32_t ip = 0, anchor = 0, op = 0;
    uint32_t match_limit;

    if (src == NULL || dst == NULL || src_len == 0) return 0;
    memset(table, 0, sizeof(table));

    /* A match may only start this far from the end, so every block keeps the
     * trailing literal run the LZ4 specification requires. */
    match_limit = src_len > LZ4_END_OFFSET ? src_len - LZ4_END_OFFSET : 0u;

    while (ip < match_limit) {
        uint32_t seq = rd32(src + ip);
        uint32_t slot = lz4_hash(seq);
        uint32_t ref = table[slot];
        uint32_t match, lit, token, at, offset;

        table[slot] = ip + 1u;               /* 0 means "empty slot" */
        if (ref == 0u || rd32(src + ref - 1u) != seq) {
            ip++;
            continue;
        }
        ref -= 1u;
        offset = ip - ref;
        if (offset == 0u || offset > 65535u) {
            ip++;
            continue;
        }

        match = LZ4_MIN_MATCH;
        while (ip + match + LZ4_LAST_LITERALS < src_len &&
               src[ref + match] == src[ip + match])
            match++;

        lit = ip - anchor;
        if (op + lz4_sequence_need(lit, match) >= dst_cap) break;

        token = op++;
        dst[token] = (uint8_t)((lit >= 15u ? 15u : lit) << 4);
        if (lit >= 15u) op = lz4_put_ext(dst, op, lit - 15u);
        memcpy(dst + op, src + anchor, lit);
        op += lit;
        wr16(dst + op, (uint16_t)offset);
        op += 2u;
        {
            uint32_t extra = match - LZ4_MIN_MATCH;
            if (extra >= 15u) {
                dst[token] = (uint8_t)(dst[token] | 0x0Fu);
                op = lz4_put_ext(dst, op, extra - 15u);
            } else {
                dst[token] = (uint8_t)(dst[token] | (uint8_t)extra);
            }
        }

        ip += match;
        anchor = ip;
        if (ip < match_limit) {
            /* Index the position after the match so the next sequence can
             * reference it; skipping this costs compression, never validity. */
            at = lz4_hash(rd32(src + ip));
            table[at] = ip + 1u;
        }
    }

    /* Last sequence: literals only (LZ4 always ends this way). */
    {
        uint32_t lit = src_len - anchor;
        if (op + lz4_sequence_need(lit, 0u) > dst_cap) return 0;
        dst[op++] = (uint8_t)((lit >= 15u ? 15u : lit) << 4);
        if (lit >= 15u) op = lz4_put_ext(dst, op, lit - 15u);
        memcpy(dst + op, src + anchor, lit);
        op += lit;
    }
    return op;
}

/* Literals-only block: always a valid LZ4 stream, used when compression would
 * not pay off or does not fit the frame budget. */
static uint32_t lz4_store_literals(const uint8_t *src, uint32_t src_len,
                                   uint8_t *dst, uint32_t dst_cap)
{
    uint32_t op = 0;

    if (src_len + 1u + src_len / 255u + 2u > dst_cap) return 0;
    dst[op++] = (uint8_t)((src_len >= 15u ? 15u : src_len) << 4);
    if (src_len >= 15u) op = lz4_put_ext(dst, op, src_len - 15u);
    memcpy(dst + op, src, src_len);
    return op + src_len;
}

size_t vio_qvf_header(uint8_t *out, size_t cap,
                      uint32_t width, uint32_t height, uint16_t fps)
{
    if (out == NULL || cap < VIO_QVF_HEADER) return 0;
    if (width == 0u || height == 0u ||
        width > VIO_QVF_MAX_W || height > VIO_QVF_MAX_H) return 0;
    if (fps == 0u || fps > 30u) return 0;

    memset(out, 0, VIO_QVF_HEADER);
    wr32(out + 0, QVF_MAGIC);
    wr16(out + 4, QVF_VERSION);
    wr16(out + 6, QVF_CODEC_LZ4_RGB565);
    wr32(out + 8, width);
    wr32(out + 12, height);
    wr16(out + 16, fps);
    wr16(out + 18, 0);
    wr32(out + 20, 0);                        /* patched by set_frames */
    wr32(out + 24, 0);
    wr32(out + 28, 0);
    return VIO_QVF_HEADER;
}

int vio_qvf_append(uint8_t *out, size_t cap, size_t *used,
                   const uint32_t *xrgb, uint32_t width, uint32_t height)
{
    static uint8_t raw[VIO_QVF_MAX_W * VIO_QVF_MAX_H * 2u];
    static uint8_t comp[sizeof(raw) + sizeof(raw) / 255u + 32u];
    uint32_t raw_len, comp_len, need;

    if (out == NULL || used == NULL || xrgb == NULL) return -1;
    if (width == 0u || height == 0u ||
        width > VIO_QVF_MAX_W || height > VIO_QVF_MAX_H) return -1;

    raw_len = width * height * 2u;
    if (raw_len > sizeof(raw)) return -1;

    for (uint32_t i = 0; i < width * height; i++) {
        uint32_t p = xrgb[i];
        uint32_t r = (p >> 19) & 0x1Fu;
        uint32_t g = (p >> 10) & 0x3Fu;
        uint32_t b = (p >> 3) & 0x1Fu;
        uint16_t v = (uint16_t)((r << 11) | (g << 5) | b);
        raw[i * 2u] = (uint8_t)(v & 0xFFu);
        raw[i * 2u + 1u] = (uint8_t)(v >> 8);
    }

    comp_len = vio_lz4_compress(raw, raw_len, comp, (uint32_t)sizeof(comp));
    if (comp_len == 0u || comp_len >= raw_len)
        comp_len = lz4_store_literals(raw, raw_len, comp, (uint32_t)sizeof(comp));
    if (comp_len == 0u) return -1;

    need = VIO_QVF_ENTRY + comp_len;
    if (*used + need > cap) return -2;
    wr32(out + *used, raw_len);
    wr32(out + *used + 4u, comp_len);
    memcpy(out + *used + VIO_QVF_ENTRY, comp, comp_len);
    *used += need;
    return 0;
}

void vio_qvf_set_frames(uint8_t *out, size_t cap, uint32_t frames)
{
    if (out == NULL || cap < VIO_QVF_HEADER) return;
    wr32(out + 20, frames);
}

uint32_t vio_qvf_frames(const uint8_t *out, size_t cap)
{
    if (out == NULL || cap < VIO_QVF_HEADER) return 0;
    return rd32(out + 20);
}
