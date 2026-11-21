/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_media_test.c - execute the media inspection core on fixtures. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vqeaf_media.h"

static int g_failures;
static const char *g_dir;

#define CHECK(cond, msg) do { \
    if(cond) { printf("ok   %s\n", msg); } \
    else { printf("FAIL %s\n", msg); g_failures++; } \
} while(0)

static uint8_t *load(const char *name, size_t *size)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", g_dir, name);
    FILE *file = fopen(path, "rb");
    if(file == NULL) { printf("FAIL missing fixture %s\n", name); g_failures++; return NULL; }
    fseek(file, 0, SEEK_END);
    long n = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if(fread(buf, 1, (size_t)n, file) != (size_t)n) { fclose(file); free(buf); return NULL; }
    fclose(file);
    *size = (size_t)n;
    return buf;
}

static int inspect(const char *name, vqeaf_media_info_t *info)
{
    size_t size = 0;
    uint8_t *buf = load(name, &size);
    if(buf == NULL) return -1;
    int rc = vqeaf_media_inspect(buf, size, info);
    free(buf);
    return rc;
}

int main(int argc, char **argv)
{
    vqeaf_media_info_t info;
    if(argc < 2) { printf("usage: %s fixture-dir\n", argv[0]); return 2; }
    g_dir = argv[1];

    CHECK(inspect("sample.png", &info) == 0 && info.type == VQEAF_MEDIA_PNG &&
          info.width == 512 && info.height == 512, "PNG real icon 512x512");
    CHECK(inspect("sample.bmp", &info) == 0 && info.type == VQEAF_MEDIA_BMP &&
          info.width == 10 && info.height == 8, "BMP 10x8");
    CHECK(inspect("sample.gif", &info) == 0 && info.type == VQEAF_MEDIA_GIF &&
          info.width == 4 && info.height == 4 && info.frame_count == 2,
          "GIF 4x4 2 frames");
    CHECK(inspect("sample.jpg", &info) == 0 && info.type == VQEAF_MEDIA_JPEG &&
          info.width == 16 && info.height == 12, "JPEG 16x12");
    CHECK(inspect("sample.wav", &info) == 0 && info.type == VQEAF_MEDIA_WAV &&
          info.sample_rate == 8000 && info.channels == 1 && info.bits == 16 &&
          info.duration_ms == 1000, "WAV 8kHz mono 16-bit 1s");
    CHECK(inspect("sample.mp3", &info) == 0 && info.type == VQEAF_MEDIA_MP3 &&
          strcmp(info.title, "Test Title") == 0 &&
          strcmp(info.artist, "Test Artist") == 0 &&
          info.bitrate_kbps == 128 && info.sample_rate == 44100,
          "MP3 ID3 title/artist 128kbps 44.1kHz");
    CHECK(inspect("sample.mp4", &info) == 0 && info.type == VQEAF_MEDIA_MP4 &&
          strcmp(info.brand, "isom") == 0 && info.duration_ms == 2000 &&
          info.width == 320 && info.height == 240,
          "MP4 isom 320x240 2s");
    CHECK(inspect("sample.pdf", &info) == 0 && info.type == VQEAF_MEDIA_PDF &&
          info.page_count == 3, "PDF 3 pages");
    CHECK(inspect("sample.txt", &info) == 0 && info.type == VQEAF_MEDIA_TEXT,
          "plain text sniffed");
    CHECK(inspect("sample.zip", &info) == 0 && info.type == VQEAF_MEDIA_ZIP,
          "ZIP sniffed");
    CHECK(inspect("sample.png", &info) == 0 && strstr(info.summary, "512x512") != NULL,
          "summary carries dimensions");

    /* a renamed file is still identified by content, not extension */
    {
        size_t size = 0;
        uint8_t *buf = load("sample.wav", &size);
        if(buf != NULL) {
            CHECK(vqeaf_media_inspect(buf, size, &info) == 0 &&
                  info.type == VQEAF_MEDIA_WAV, "magic beats extension");
            free(buf);
        }
    }

    printf(g_failures ? "%d FAILURES\n" : "ALL OK\n", g_failures);
    return g_failures ? 1 : 0;
}