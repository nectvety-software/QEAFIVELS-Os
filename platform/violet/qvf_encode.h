/* SPDX-License-Identifier: MIT */
/* qvf_encode.h - QVF1 clip writer for the screen recorder.
 *
 * The OS decodes exactly one container (docs/QEAF_VIDEO_FORMAT.md): a 32-byte
 * header followed by {u32 raw_size, u32 comp_size, LZ4 block} per frame, where
 * the raw frame is width*height RGB565 little-endian pixels.
 *
 * This file is plain C with no LVGL, DRM or filesystem dependency, so the
 * host gate in tests/vqeaf_qvf_encode.c compiles and checks the same code the
 * phone runs - the recorder is never "verified" by a parallel implementation.
 */
#ifndef VQEAF_QVF_ENCODE_H
#define VQEAF_QVF_ENCODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VIO_QVF_HEADER 32u
#define VIO_QVF_ENTRY 8u
/* The reference players accept up to 240x426; the recorder uses 240x416. */
#define VIO_QVF_MAX_W 240u
#define VIO_QVF_MAX_H 426u

/* Writes the 32-byte header with frame_count 0. Returns the header size, or 0
 * when `cap` is too small or the geometry is outside the documented range. */
size_t vio_qvf_header(uint8_t *out, size_t cap,
                      uint32_t width, uint32_t height, uint16_t fps);

/* Converts one XRGB8888 frame (width*height pixels, the recorder's downscaled
 * scanout) to RGB565, compresses it and appends the frame entry at *used.
 * Returns 0, -1 on bad arguments, -2 when the clip budget is exhausted. */
int vio_qvf_append(uint8_t *out, size_t cap, size_t *used,
                   const uint32_t *xrgb, uint32_t width, uint32_t height);

/* Frame count helpers. The header field is patched once, when the clip is
 * complete, so a truncated file can never claim frames it does not hold. */
void vio_qvf_set_frames(uint8_t *out, size_t cap, uint32_t frames);
uint32_t vio_qvf_frames(const uint8_t *out, size_t cap);

/* LZ4 block compressor (the format QVF codec 1 requires). Returns the
 * compressed length, or 0 when it does not fit in dst_cap. The output is a
 * well-formed LZ4 block: last sequence literals only, matches at least 12
 * bytes before the end, 16-bit offsets. */
uint32_t vio_lz4_compress(const uint8_t *src, uint32_t src_len,
                          uint8_t *dst, uint32_t dst_cap);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_QVF_ENCODE_H */
