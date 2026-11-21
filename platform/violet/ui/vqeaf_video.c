/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_video.c - shell video page core.
 *
 * Everything here reads real bytes through vqeaf_files_read/list. A file
 * that is not a QVF1 container is reported as such; no frame is ever
 * synthesised. The LZ4 block decoder below is the same format the native
 * .qeapp video player uses, kept in sync with docs/QEAF_VIDEO_FORMAT.md.
 */
#include "vqeaf_video.h"

#include <stdio.h>
#include <string.h>

/* ---- QVF container (docs/QEAF_VIDEO_FORMAT.md) ---------------------------- */

#define QVF_MAGIC "QVF1"
#define QVF_VERSION 1u
#define QVF_CODEC_LZ4_RGB565 1u
#define QVF_HEADER_SIZE 32u
#define QVF_FRAME_ENTRY 8u    /* u32 raw_size + u32 comp_size */

/* Working buffers for one decode at a time (bounded by the preview budget). */
static uint8_t qvf_file[VQEAF_VIDEO_PREVIEW_BYTES];
static uint8_t qvf_comp[VQEAF_VIDEO_PREVIEW_BYTES - QVF_HEADER_SIZE];
static uint8_t qvf_raw[VQEAF_VIDEO_MAX_W * VQEAF_VIDEO_MAX_H * 2u];

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/* ---- LZ4 block decoder (the one codec in the shell) ------------------------ */

static int lz4_decode(const uint8_t *src, size_t src_len,
                      uint8_t *dst, size_t dst_cap)
{
    size_t ip = 0, op = 0;
    while(ip < src_len) {
        uint8_t token = src[ip++];
        size_t lit = (size_t)(token >> 4);
        if(lit == 15u) {
            uint8_t addend;
            do {
                if(ip >= src_len) return -1;
                addend = src[ip++];
                lit += addend;
            } while(addend == 255u);
        }
        if(lit > src_len - ip || lit > dst_cap - op) return -1;
        for(size_t k = 0; k < lit; k++) dst[op + k] = src[ip + k];
        ip += lit;
        op += lit;
        if(ip >= src_len) break;             /* last sequence: literals only */
        if(src_len - ip < 2u) return -1;
        uint16_t offset = (uint16_t)(src[ip] | ((uint16_t)src[ip + 1] << 8));
        ip += 2;
        if(offset == 0u || (size_t)offset > op) return -1;
        size_t mlen = (size_t)(token & 0x0Fu);
        if(mlen == 15u) {
            uint8_t addend;
            do {
                if(ip >= src_len) return -1;
                addend = src[ip++];
                mlen += addend;
            } while(addend == 255u);
        }
        mlen += 4u;
        if(mlen > dst_cap - op) return -1;
        size_t from = op - (size_t)offset;   /* overlapping copy */
        for(size_t k = 0; k < mlen; k++) dst[op + k] = dst[from + k];
        op += mlen;
    }
    return op == dst_cap ? 0 : -1;
}

/* ---- scanning -------------------------------------------------------------- */

static int classify(const char *name, vqeaf_video_entry_t *entry)
{
    uint8_t head[QVF_HEADER_SIZE];
    int read;

    read = vqeaf_files_read(entry->path, head, sizeof(head));
    if(read < 4) return 0;
    if(memcmp(head, QVF_MAGIC, 4) == 0) {
        if(read < (int)QVF_HEADER_SIZE) return 0;
        entry->kind = VQEAF_VIDEO_QVF;
        entry->width = rd32(head + 8);
        entry->height = rd32(head + 12);
        entry->fps = rd16(head + 16);
        entry->frames = rd32(head + 20);
        /* Only claim playability for sane headers; anything else stays
         * VQEAF_VIDEO_OTHER and the page shows the raw size. */
        if(rd16(head + 4) != QVF_VERSION ||
           rd16(head + 6) != QVF_CODEC_LZ4_RGB565 ||
           rd32(head + 24) != 0u ||
           entry->width == 0u || entry->width > VQEAF_VIDEO_MAX_W ||
           entry->height == 0u || entry->height > VQEAF_VIDEO_MAX_H ||
           entry->frames == 0u) {
            entry->kind = VQEAF_VIDEO_OTHER;
            entry->width = entry->height = entry->fps = entry->frames = 0u;
        }
        (void)name;
        return 1;
    }
    if(read >= 8 && memcmp(head + 4, "ftyp", 4) == 0) {
        entry->kind = VQEAF_VIDEO_MP4;
        return 1;
    }
    return 0;
}

int vqeaf_video_scan(const char *dir, vqeaf_video_entry_t *out, int max)
{
    vqeaf_file_t files[64];
    int count, found = 0;

    if(dir == NULL || out == NULL || max <= 0) return -22; /* -EINVAL */
    count = vqeaf_files_list(dir, files, (int)(sizeof(files) /
                                               sizeof(files[0])));
    if(count < 0) return count;
    for(int i = 0; i < count && found < max; i++) {
        vqeaf_video_entry_t *entry = &out[found];

        if(files[i].is_dir) continue;
        memset(entry, 0, sizeof(*entry));
        snprintf(entry->name, sizeof(entry->name), "%s", files[i].name);
        if(vqeaf_files_join(dir, files[i].name, entry->path,
                            sizeof(entry->path)) != 0)
            continue;
        entry->size = files[i].size;
        if(!classify(files[i].name, entry)) continue;
        found++;
    }
    return found;
}

/* ---- frame decode ---------------------------------------------------------- */

int vqeaf_video_qvf_frame(const char *path, int index,
                          uint32_t *fb, int stride)
{
    uint32_t width, height, frames;
    uint32_t offset, raw_size, comp_size;
    int read;

    if(path == NULL || fb == NULL || stride <= 0) return VQEAF_VIDEO_ERR_BAD_ARGS;

    read = vqeaf_files_read(path, qvf_file, sizeof(qvf_file));
    if(read < (int)QVF_HEADER_SIZE) return VQEAF_VIDEO_ERR_NO_FILE;
    if(memcmp(qvf_file, QVF_MAGIC, 4) != 0) return VQEAF_VIDEO_ERR_NOT_QVF;
    if(rd16(qvf_file + 4) != QVF_VERSION) return VQEAF_VIDEO_ERR_VERSION;
    if(rd16(qvf_file + 6) != QVF_CODEC_LZ4_RGB565)
        return VQEAF_VIDEO_ERR_CODEC;
    if(rd32(qvf_file + 24) != 0u) return VQEAF_VIDEO_ERR_FLAGS;
    width = rd32(qvf_file + 8);
    height = rd32(qvf_file + 12);
    frames = rd32(qvf_file + 20);
    if(width == 0u || width > VQEAF_VIDEO_MAX_W ||
       height == 0u || height > VQEAF_VIDEO_MAX_H ||
       frames == 0u || read < (int)(QVF_HEADER_SIZE + frames * QVF_FRAME_ENTRY))
        return VQEAF_VIDEO_ERR_TOO_LARGE;
    if(index < 0 || (uint32_t)index >= frames) return VQEAF_VIDEO_ERR_RANGE;

    offset = QVF_HEADER_SIZE;
    for(uint32_t i = 0; i < (uint32_t)index; i++)
        offset += QVF_FRAME_ENTRY + rd32(qvf_file + offset + 4);
    raw_size = rd32(qvf_file + offset);
    comp_size = rd32(qvf_file + offset + 4);
    if(raw_size != width * height * 2u || comp_size == 0u ||
       comp_size > sizeof(qvf_comp) ||
       offset + QVF_FRAME_ENTRY + comp_size > (uint32_t)read)
        return VQEAF_VIDEO_ERR_BAD_FRAME;
    memcpy(qvf_comp, qvf_file + offset + QVF_FRAME_ENTRY, comp_size);
    if(lz4_decode(qvf_comp, comp_size, qvf_raw, raw_size) != 0)
        return VQEAF_VIDEO_ERR_DECODE;

    /* RGB565 -> XRGB8888, one row at a time. */
    for(uint32_t y = 0; y < height; y++) {
        const uint8_t *row = qvf_raw + y * width * 2u;
        uint32_t *dst = fb + (size_t)y * (size_t)stride;
        for(uint32_t x = 0; x < width; x++) {
            uint16_t c = (uint16_t)(row[x * 2u] | (row[x * 2u + 1u] << 8));
            uint32_t r = ((c >> 11) & 0x1Fu) * 255u / 31u;
            uint32_t g = ((c >> 5) & 0x3Fu) * 255u / 63u;
            uint32_t b = (c & 0x1Fu) * 255u / 31u;
            dst[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
    return VQEAF_VIDEO_OK;
}

const char *vqeaf_video_strerror(int code)
{
    switch(code) {
    case VQEAF_VIDEO_OK:            return "ok";
    case VQEAF_VIDEO_ERR_NO_FILE:   return "NO VIDEO FILE";
    case VQEAF_VIDEO_ERR_NOT_QVF:   return "UNSUPPORTED FORMAT";
    case VQEAF_VIDEO_ERR_VERSION:   return "UNSUPPORTED VERSION";
    case VQEAF_VIDEO_ERR_CODEC:     return "CODEC NOT SUPPORTED";
    case VQEAF_VIDEO_ERR_FLAGS:     return "BAD QVF FLAGS";
    case VQEAF_VIDEO_ERR_TOO_LARGE: return "VIDEO TOO LARGE";
    case VQEAF_VIDEO_ERR_BAD_FRAME: return "BAD FRAME ENTRY";
    case VQEAF_VIDEO_ERR_RANGE:     return "FRAME OUT OF RANGE";
    case VQEAF_VIDEO_ERR_DECODE:    return "FRAME DECODE FAILED";
    default:                        return "BAD ARGUMENTS";
    }
}
