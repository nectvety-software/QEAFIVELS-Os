/* SPDX-License-Identifier: MIT */
/* Host gate for the screen recorder encoder (platform/violet/qvf_encode.c).
 *
 * The recorder writes QVF1, the only container the OS decodes
 * (docs/QEAF_VIDEO_FORMAT.md). A clip that decodes to different pixels than
 * the scanout it was made from is worse than no clip, so this test decodes
 * every encoded frame back and compares it with the RGB565 conversion of the
 * original XRGB frame. The decoder below is the specification's decoder
 * (same rules as platform/violet/ui/vqeaf_video.c); the optional output file
 * lets the Python gate check the same bytes with the reference implementation.
 *
 *   vqeaf_qvf_encode [out.qvf]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qvf_encode.h"

#define TEST_W 240u
#define TEST_H 416u
#define MAX_FRAMES 6u

static int g_failures;

#define CHECK(cond, msg) do { \
    if(cond) printf("ok   %s\n", (msg)); \
    else { printf("FAIL %s\n", (msg)); g_failures++; } \
} while (0)

/* LZ4 block decoder: token nibbles, 15+n extension runs, 16-bit offsets,
 * overlapping matches, literals-only last sequence. */
static int lz4_decode(const uint8_t *src, size_t src_len,
                      uint8_t *dst, size_t dst_cap, size_t *out_len)
{
    size_t ip = 0, op = 0;

    while (ip < src_len) {
        uint8_t token = src[ip++];
        size_t lit = (size_t)(token >> 4);
        if (lit == 15u) {
            uint8_t add;
            do {
                if (ip >= src_len) return -1;
                add = src[ip++];
                lit += add;
            } while (add == 255u);
        }
        if (lit > src_len - ip || lit > dst_cap - op) return -1;
        memcpy(dst + op, src + ip, lit);
        ip += lit;
        op += lit;
        if (ip >= src_len) break;                  /* last sequence */
        if (src_len - ip < 2u) return -1;
        {
            size_t offset = (size_t)src[ip] | ((size_t)src[ip + 1] << 8);
            size_t match;
            ip += 2;
            if (offset == 0u || offset > op) return -1;
            match = (size_t)(token & 0x0Fu);
            if (match == 15u) {
                uint8_t add;
                do {
                    if (ip >= src_len) return -1;
                    add = src[ip++];
                    match += add;
                } while (add == 255u);
            }
            match += 4u;
            if (match > dst_cap - op) return -1;
            for (size_t i = 0; i < match; i++, op++) dst[op] = dst[op - offset];
        }
    }
    *out_len = op;
    return 0;
}

static uint32_t pixel(uint32_t frame, uint32_t x, uint32_t y)
{
    switch (frame) {
    case 0:
        return 0x00204060u;                        /* flat */
    case 1:
        return 0x00000000u | ((x * 255u / TEST_W) << 16) |
               ((y * 255u / TEST_H) << 8) | 0x40u; /* gradient */
    default:
        /* Repeating 8x8 blocks: exercise LZ4 matches with short offsets. */
        return ((x / 8u + y / 8u) & 1u) ? 0x00F0F0F0u : 0x00101010u;
    }
}

int main(int argc, char **argv)
{
    const char *out_path = argc > 1 ? argv[1] : "build/qvf-test/rec-test.qvf";
    static uint8_t clip[1u << 20];
    static uint32_t xrgb[TEST_W * TEST_H];
    static uint8_t expect[3][TEST_W * TEST_H * 2u];
    static uint8_t decoded[TEST_W * TEST_H * 2u];
    size_t used, header, comp;
    uint32_t frames = 0;
    FILE *out;

    header = vio_qvf_header(clip, sizeof(clip), TEST_W, TEST_H, 8);
    CHECK(header == 32u, "header is 32 bytes");
    CHECK(memcmp(clip, "QVF1", 4) == 0, "magic is QVF1");
    CHECK(clip[4] == 1 && clip[5] == 0, "version 1");
    CHECK(clip[6] == 1 && clip[7] == 0, "codec 1 (LZ4 + RGB565)");
    {
        uint32_t w = (uint32_t)clip[8] | ((uint32_t)clip[9] << 8) |
                     ((uint32_t)clip[10] << 16) | ((uint32_t)clip[11] << 24);
        uint32_t h = (uint32_t)clip[12] | ((uint32_t)clip[13] << 8) |
                     ((uint32_t)clip[14] << 16) | ((uint32_t)clip[15] << 24);
        CHECK(w == TEST_W && h == TEST_H, "geometry matches the scanout sample");
        CHECK(clip[16] == 8 && clip[17] == 0, "fps field is 8");
    }
    CHECK(vio_qvf_frames(clip, sizeof(clip)) == 0, "frame count starts at zero");

    used = header;
    for (uint32_t f = 0; f < 3; f++) {
        int rc;
        for (uint32_t y = 0; y < TEST_H; y++) {
            for (uint32_t x = 0; x < TEST_W; x++) {
                uint32_t p = pixel(f, x, y);
                uint16_t v = (uint16_t)((((p >> 19) & 0x1Fu) << 11) |
                                        (((p >> 10) & 0x3Fu) << 5) |
                                        ((p >> 3) & 0x1Fu));
                xrgb[y * TEST_W + x] = p;
                expect[f][(y * TEST_W + x) * 2u] = (uint8_t)(v & 0xFFu);
                expect[f][(y * TEST_W + x) * 2u + 1u] = (uint8_t)(v >> 8);
            }
        }
        rc = vio_qvf_append(clip, sizeof(clip), &used, xrgb, TEST_W, TEST_H);
        CHECK(rc == 0, "frame appends within the clip budget");
        frames++;
    }
    vio_qvf_set_frames(clip, used, frames);
    CHECK(vio_qvf_frames(clip, used) == frames, "frame count is patched once");

    /* Walk the frames exactly as a player does and compare the pixels. */
    {
        size_t off = header;
        int all_ok = 1;
        for (uint32_t f = 0; f < frames; f++) {
            uint32_t raw_size = (uint32_t)clip[off] |
                                ((uint32_t)clip[off + 1] << 8) |
                                ((uint32_t)clip[off + 2] << 16) |
                                ((uint32_t)clip[off + 3] << 24);
            uint32_t comp_size = (uint32_t)clip[off + 4] |
                                 ((uint32_t)clip[off + 5] << 8) |
                                 ((uint32_t)clip[off + 6] << 16) |
                                 ((uint32_t)clip[off + 7] << 24);
            size_t got = 0;
            if (raw_size != TEST_W * TEST_H * 2u) { all_ok = 0; break; }
            if (comp_size == 0u || comp_size >= raw_size) { all_ok = 0; break; }
            if (lz4_decode(clip + off + 8u, comp_size, decoded,
                           sizeof(decoded), &got) != 0 ||
                got != raw_size ||
                memcmp(decoded, expect[f], raw_size) != 0) {
                all_ok = 0;
                break;
            }
            off += 8u + comp_size;
        }
        CHECK(all_ok, "every frame decodes back to the captured pixels");
        CHECK(off == used, "frame walk ends exactly at the clip length");
    }

    /* Closed block (< 13 bytes) must still produce a decodable stream. */
    {
        uint8_t tiny[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        uint8_t tiny_out[64];
        comp = vio_lz4_compress(tiny, sizeof(tiny), tiny_out, sizeof(tiny_out));
        CHECK(comp > 0, "tiny block compresses");
        {
            size_t got = 0;
            CHECK(lz4_decode(tiny_out, comp, decoded, sizeof(decoded), &got) == 0 &&
                  got == sizeof(tiny) && memcmp(decoded, tiny, sizeof(tiny)) == 0,
                  "tiny block decodes back");
        }
    }

    out = fopen(out_path, "wb");
    if (out != NULL) {
        fwrite(clip, 1, used, out);
        fclose(out);
        printf("wrote %s (%u bytes, %u frames)\n", out_path, (unsigned)used,
               frames);
    }

    printf("%s\n", g_failures == 0 ? "ALL OK" : "FAILURES");
    return g_failures == 0 ? 0 : 1;
}
