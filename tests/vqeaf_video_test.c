/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_video_test.c - execute the shell video core on real fixtures.
 *
 * Runs against the host fake tree (vqeaf_files.c non-__linux__ branch), which
 * embeds the generated clip.qvf and clip.mp4 fixtures, and compares decoded
 * frames byte-for-byte with Python-decoded reference dumps.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vqeaf_video.h"

static int g_failures;
static const char *g_dir;

#define CHECK(cond, msg) do { \
    if(cond) { printf("ok   %s\n", msg); } \
    else { printf("FAIL %s\n", msg); g_failures++; } \
} while(0)

static uint8_t *load(const char *name, size_t *size)
{
    char path[512];
    FILE *file;
    long length;
    uint8_t *buf;

    snprintf(path, sizeof(path), "%s/%s", g_dir, name);
    file = fopen(path, "rb");
    if(file == NULL) return NULL;
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);
    buf = malloc((size_t)length);
    if(buf == NULL || fread(buf, 1, (size_t)length, file) != (size_t)length) {
        fclose(file);
        free(buf);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return buf;
}

int main(int argc, char **argv)
{
    vqeaf_video_entry_t entries[8];
    static uint32_t frame[VQEAF_VIDEO_MAX_W * VQEAF_VIDEO_MAX_H];
    int count;

    if(argc < 2) { printf("usage: %s fixture-dir\n", argv[0]); return 2; }
    g_dir = argv[1];

    /* 1. Real scan of /media finds exactly the two video fixtures. */
    count = vqeaf_video_scan("/media", entries, 8);
    CHECK(count == 2, "scan finds 2 video files in /media");
    if(count == 2) {
        const vqeaf_video_entry_t *qvf = NULL, *mp4 = NULL;
        for(int i = 0; i < count; i++) {
            if(entries[i].kind == VQEAF_VIDEO_QVF) qvf = &entries[i];
            if(entries[i].kind == VQEAF_VIDEO_MP4) mp4 = &entries[i];
        }
        CHECK(qvf != NULL && strcmp(qvf->name, "clip.qvf") == 0,
              "clip.qvf classified as QVF");
        CHECK(mp4 != NULL && strcmp(mp4->name, "clip.mp4") == 0,
              "clip.mp4 classified as MP4");
        CHECK(qvf != NULL && qvf->width == 60u && qvf->height == 106u &&
              qvf->fps == 8u && qvf->frames == 8u && qvf->size == 1469u,
              "QVF header metadata matches the real fixture");
        CHECK(mp4 != NULL && mp4->width == 0u && mp4->frames == 0u,
              "MP4 carries no claimed QVF metadata");
        CHECK(qvf != NULL &&
              strncmp(qvf->path, "/media/clip.qvf", sizeof(qvf->path)) == 0,
              "QVF entry has its real absolute path");
    }

    /* 2. Frames decode and match the independent Python decode exactly. */
    for(int index = 0; index <= 3; index += 3) {
        char name[64];
        size_t expected_size = 0;
        uint8_t *expected;
        char label[96];

        snprintf(name, sizeof(name), "clip-expected-%d.raw", index);
        expected = load(name, &expected_size);
        if(expected == NULL) {
            printf("FAIL missing expected dump %s\n", name);
            g_failures++;
            continue;
        }
        int rc = vqeaf_video_qvf_frame("/media/clip.qvf", index, frame,
                                       VQEAF_VIDEO_MAX_W);
        /* The framebuffer has stride 240; compare 60 real pixels per row. */
        const uint32_t *exp_px = (const uint32_t *)(const void *)expected;
        int match = rc == VQEAF_VIDEO_OK &&
                    expected_size == sizeof(uint32_t) * 60u * 106u;
        for(uint32_t y = 0; match && y < 106u; y++)
            if(memcmp(frame + y * VQEAF_VIDEO_MAX_W,
                      exp_px + y * 60u, 60u * sizeof(uint32_t)) != 0)
                match = 0;
        snprintf(label, sizeof(label), "frame %d decodes to the reference "
                 "XRGB8888 dump", index);
        if(!match) {
            printf("     frame %d: rc=%d expected_size=%zu\n", index, rc,
                   expected_size);
            if(rc == VQEAF_VIDEO_OK &&
               expected_size == sizeof(uint32_t) * 60u * 106u) {
                for(uint32_t y = 0; y < 106u; y++)
                    for(uint32_t x = 0; x < 60u; x++)
                        if(frame[y * VQEAF_VIDEO_MAX_W + x] !=
                           exp_px[y * 60u + x]) {
                            printf("     first mismatch: (%u,%u) got=%08X "
                                   "exp=%08X\n", x, y,
                                   frame[y * VQEAF_VIDEO_MAX_W + x],
                                   exp_px[y * 60u + x]);
                            y = 106u;
                            break;
                        }
            }
        }
        CHECK(match, label);
        free(expected);
    }

    /* 3. Honest failures: range, wrong container, missing file. */
    CHECK(vqeaf_video_qvf_frame("/media/clip.qvf", 8, frame,
                                VQEAF_VIDEO_MAX_W) == VQEAF_VIDEO_ERR_RANGE,
          "frame 8 of 8 is out of range");
    CHECK(vqeaf_video_qvf_frame("/media/clip.qvf", -1, frame,
                                VQEAF_VIDEO_MAX_W) == VQEAF_VIDEO_ERR_RANGE,
          "negative frame index is out of range");
    CHECK(vqeaf_video_qvf_frame("/media/clip.mp4", 0, frame,
                                VQEAF_VIDEO_MAX_W) == VQEAF_VIDEO_ERR_NOT_QVF,
          "MP4 is reported as an unsupported container");
    CHECK(vqeaf_video_qvf_frame("/media/note.txt", 0, frame,
                                VQEAF_VIDEO_MAX_W) == VQEAF_VIDEO_ERR_NOT_QVF,
          "text file is reported as an unsupported container");
    CHECK(vqeaf_video_qvf_frame("/media/missing.qvf", 0, frame,
                                VQEAF_VIDEO_MAX_W) == VQEAF_VIDEO_ERR_NO_FILE,
          "missing file reports NO VIDEO FILE");
    CHECK(vqeaf_video_qvf_frame("/media/clip.qvf", 0, frame, 0) ==
          VQEAF_VIDEO_ERR_BAD_ARGS, "stride 0 is rejected");

    /* 4. Scan of a directory without videos stays empty, not fabricated. */
    CHECK(vqeaf_video_scan("/etc", entries, 8) == 0,
          "no video entries invented in /etc");
    CHECK(vqeaf_video_scan("/does-not-exist", entries, 8) < 0,
          "missing directory propagates the error");

    if(g_failures == 0) printf("vqeaf_video_test: ALL OK\n");
    return g_failures == 0 ? 0 : 1;
}
