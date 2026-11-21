/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_media.h - multimedia format inspection core.
 *
 * Identifies a file by its magic bytes (not its extension) and extracts real
 * metadata: image dimensions, audio sample rate / duration, MP3 ID3 tags, MP4
 * brand / duration, PDF page count. This is inspection, not playback: the
 * initramfs ships no audio/video codec, so the viewer reports what is actually
 * parsed and never claims a stream can be played.
 */
#ifndef VQEAF_MEDIA_H
#define VQEAF_MEDIA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VQEAF_MEDIA_UNKNOWN = 0,
    VQEAF_MEDIA_PNG,
    VQEAF_MEDIA_JPEG,
    VQEAF_MEDIA_GIF,
    VQEAF_MEDIA_BMP,
    VQEAF_MEDIA_WAV,
    VQEAF_MEDIA_MP3,
    VQEAF_MEDIA_MP4,
    VQEAF_MEDIA_PDF,
    VQEAF_MEDIA_ZIP,
    VQEAF_MEDIA_TEXT,
} vqeaf_media_type_t;

typedef struct {
    vqeaf_media_type_t type;
    int width, height;                 /* image / video pixels */
    int channels, sample_rate, bits;   /* audio */
    int duration_ms;                   /* audio / video */
    int bitrate_kbps;                  /* audio */
    int page_count;                    /* pdf */
    int frame_count;                   /* gif */
    char title[128];                   /* mp3 id3 */
    char artist[128];
    char album[128];
    char brand[16];                    /* mp4 ftyp major brand */
    char summary[256];                 /* one-line human description */
} vqeaf_media_info_t;

/* Sniffs magic bytes and parses metadata from the file head. `len` should be
 * the whole file when available (duration estimates need it); a few KiB is
 * enough for dimensions and tags. Returns 0 and fills `info`, or -1 when the
 * format is not recognised. Never reads past `len`. */
int vqeaf_media_inspect(const uint8_t *data, size_t len,
                        vqeaf_media_info_t *info);

const char *vqeaf_media_type_name(vqeaf_media_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_MEDIA_H */