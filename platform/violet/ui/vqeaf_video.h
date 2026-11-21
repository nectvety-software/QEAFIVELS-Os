/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_video.h - shell video page core.
 *
 * Scans a directory for real video files and classifies them by magic
 * bytes, not extension:
 *
 *   QVF1  (docs/QEAF_VIDEO_FORMAT.md) - the container the OS actually
 *         decodes: RGB565 frames, LZ4 block compression. The shell can
 *         parse the header and decode single frames for a preview.
 *   MP4   - real container, but the OS ships no H.264/AAC decoder, so the
 *         page reports metadata only and says the codec is unsupported.
 *
 * This core is LVGL-free; the page wires the results into widgets. Every
 * failure is reported through vqeaf_video_strerror, never papered over.
 */
#ifndef VQEAF_VIDEO_H
#define VQEAF_VIDEO_H

#include <stddef.h>
#include <stdint.h>

#include "vqeaf_files.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Preview budget: the shell decodes small QVF clips only. Larger files
 * (e.g. full converted videos) play in a native .qeapp player, which
 * streams them through framework API v3 read_file_part. */
#define VQEAF_VIDEO_PREVIEW_BYTES (256u * 1024u)
#define VQEAF_VIDEO_MAX_W 240
#define VQEAF_VIDEO_MAX_H 426

typedef enum {
    VQEAF_VIDEO_QVF = 0,   /* QVF1: header parsed, frames decodable */
    VQEAF_VIDEO_MP4,       /* real MP4 container; codecs not shipped */
    VQEAF_VIDEO_OTHER,     /* video extension but unrecognised magic */
} vqeaf_video_kind_t;

typedef struct {
    char name[VQEAF_FILE_NAME];
    char path[288];
    vqeaf_video_kind_t kind;
    uint32_t size;
    /* QVF header fields, valid when kind == VQEAF_VIDEO_QVF. */
    uint32_t width, height, fps, frames;
} vqeaf_video_entry_t;

/* Lists video files in an absolute directory (real tree on the device,
 * deterministic fake tree in the host render harness). Returns the entry
 * count, or a negative errno from vqeaf_files_list. */
int vqeaf_video_scan(const char *dir, vqeaf_video_entry_t *out, int max);

/* Decodes QVF frame `index` into an XRGB8888 buffer with row pitch
 * `stride` pixels. Returns 0, or a negative vqeaf_video_error code. */
typedef enum {
    VQEAF_VIDEO_OK = 0,
    VQEAF_VIDEO_ERR_NO_FILE = -1,
    VQEAF_VIDEO_ERR_NOT_QVF = -2,
    VQEAF_VIDEO_ERR_VERSION = -3,
    VQEAF_VIDEO_ERR_CODEC = -4,
    VQEAF_VIDEO_ERR_FLAGS = -5,
    VQEAF_VIDEO_ERR_TOO_LARGE = -6,
    VQEAF_VIDEO_ERR_BAD_FRAME = -7,
    VQEAF_VIDEO_ERR_RANGE = -8,
    VQEAF_VIDEO_ERR_DECODE = -9,
    VQEAF_VIDEO_ERR_BAD_ARGS = -10,
} vqeaf_video_error_t;

int vqeaf_video_qvf_frame(const char *path, int index,
                          uint32_t *fb, int stride);

const char *vqeaf_video_strerror(int code);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_VIDEO_H */
