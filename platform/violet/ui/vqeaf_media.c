/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_media.c - magic-byte sniffing + metadata parsing. */
#include "vqeaf_media.h"

#include <stdio.h>
#include <string.h>

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int has(const uint8_t *data, size_t len, size_t off, const char *sig,
               size_t n)
{
    return off + n <= len && memcmp(data + off, sig, n) == 0;
}

const char *vqeaf_media_type_name(vqeaf_media_type_t type)
{
    switch(type) {
    case VQEAF_MEDIA_PNG:  return "PNG";
    case VQEAF_MEDIA_JPEG: return "JPEG";
    case VQEAF_MEDIA_GIF:  return "GIF";
    case VQEAF_MEDIA_BMP:  return "BMP";
    case VQEAF_MEDIA_WAV:  return "WAV";
    case VQEAF_MEDIA_MP3:  return "MP3";
    case VQEAF_MEDIA_MP4:  return "MP4";
    case VQEAF_MEDIA_PDF:  return "PDF";
    case VQEAF_MEDIA_ZIP:  return "ZIP";
    case VQEAF_MEDIA_TEXT: return "Text";
    default:               return "?";
    }
}

/* ---- images -------------------------------------------------------------- */

static int parse_png(const uint8_t *d, size_t n, vqeaf_media_info_t *info)
{
    static const uint8_t sig[8] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
    if(n < 26 || memcmp(d, sig, 8) != 0 || !has(d, n, 12, "IHDR", 4))
        return -1;
    info->type = VQEAF_MEDIA_PNG;
    info->width = (int)be32(d + 16);
    info->height = (int)be32(d + 20);
    snprintf(info->summary, sizeof(info->summary),
             "PNG %dx%d, %d-bit, color type %d", info->width, info->height,
             d[24], d[25]);
    return 0;
}

static int parse_jpeg(const uint8_t *d, size_t n, vqeaf_media_info_t *info)
{
    if(n < 4 || d[0] != 0xFF || d[1] != 0xD8 || d[2] != 0xFF) return -1;
    info->type = VQEAF_MEDIA_JPEG;
    size_t i = 2;
    while(i + 4 <= n) {
        if(d[i] != 0xFF) break;
        uint8_t marker = d[i + 1];
        if(marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            i += 2;
            continue;
        }
        if(marker == 0xDA) break;            /* start of scan: no SOF found */
        if(i + 4 > n) break;
        uint16_t seg = be16(d + i + 2);
        if(marker >= 0xC0 && marker <= 0xC3 && i + 9 <= n) {
            info->height = be16(d + i + 5);
            info->width = be16(d + i + 7);
            break;
        }
        if(seg < 2) break;
        i += 2 + seg;
    }
    snprintf(info->summary, sizeof(info->summary), "JPEG %dx%d",
             info->width, info->height);
    return 0;
}

/* Skip a GIF sub-block chain (length-prefixed blocks, terminated by 0). */
static size_t gif_skip_sub(const uint8_t *d, size_t n, size_t pos)
{
    while(pos < n) {
        uint8_t block = d[pos++];
        if(block == 0) break;
        pos += block;
    }
    return pos;
}

static int parse_gif(const uint8_t *d, size_t n, vqeaf_media_info_t *info)
{
    if(n < 13 || !has(d, n, 0, "GIF8", 4)) return -1;
    info->type = VQEAF_MEDIA_GIF;
    info->width = le16(d + 6);
    info->height = le16(d + 8);
    size_t pos = 13;
    if(d[10] & 0x80) pos += 3 * (2 << (d[10] & 0x07));   /* global colour table */
    int frames = 0;
    while(pos < n) {
        uint8_t tag = d[pos++];
        if(tag == 0x3B) break;                            /* trailer */
        if(tag == 0x21) {                                 /* extension */
            if(pos < n) pos++;                            /* label */
            pos = gif_skip_sub(d, n, pos);
        }
        else if(tag == 0x2C) {                            /* image descriptor */
            frames++;
            if(pos + 9 > n) break;
            uint8_t packed = d[pos + 8];
            pos += 9;
            if(packed & 0x80) pos += 3 * (2 << (packed & 0x07));
            if(pos < n) pos++;                            /* LZW min code size */
            pos = gif_skip_sub(d, n, pos);
        }
        else break;
    }
    info->frame_count = frames;
    snprintf(info->summary, sizeof(info->summary), "GIF %dx%d, %d khung",
             info->width, info->height, frames);
    return 0;
}

static int parse_bmp(const uint8_t *d, size_t n, vqeaf_media_info_t *info)
{
    if(n < 30 || !has(d, n, 0, "BM", 2)) return -1;
    info->type = VQEAF_MEDIA_BMP;
    info->width = (int)le32(d + 18);
    int h = (int)le32(d + 22);
    info->height = h < 0 ? -h : h;
    snprintf(info->summary, sizeof(info->summary), "BMP %dx%d, %d-bit",
             info->width, info->height, le16(d + 28));
    return 0;
}

/* ---- audio --------------------------------------------------------------- */

static int parse_wav(const uint8_t *d, size_t n, vqeaf_media_info_t *info)
{
    if(n < 44 || !has(d, n, 0, "RIFF", 4) || !has(d, n, 8, "WAVE", 4))
        return -1;
    info->type = VQEAF_MEDIA_WAV;
    uint32_t data_size = 0;
    size_t pos = 12;
    while(pos + 8 <= n) {
        uint32_t size = le32(d + pos + 4);
        if(has(d, n, pos, "fmt ", 4) && pos + 24 <= n) {
            info->channels = le16(d + pos + 10);
            info->sample_rate = (int)le32(d + pos + 12);
            info->bits = le16(d + pos + 22);
        }
        else if(has(d, n, pos, "data", 4)) {
            data_size = size;
        }
        pos += 8 + size + (size & 1);
    }
    int byte_rate = info->sample_rate * info->channels * (info->bits / 8);
    if(byte_rate > 0)
        info->duration_ms = (int)((uint64_t)data_size * 1000 / byte_rate);
    snprintf(info->summary, sizeof(info->summary),
             "WAV %d Hz, %d kênh, %d-bit, %.1f s", info->sample_rate,
             info->channels, info->bits, info->duration_ms / 1000.0);
    return 0;
}

static uint32_t synchsafe(const uint8_t *p)
{
    return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) |
           ((uint32_t)(p[2] & 0x7F) << 7) | (uint32_t)(p[3] & 0x7F);
}

static void id3_text(const uint8_t *p, size_t n, char *out, size_t cap)
{
    if(n == 0) { out[0] = '\0'; return; }
    uint8_t enc = p[0];
    const uint8_t *text = p + 1;
    size_t len = n - 1;
    size_t used = 0;
    if(enc == 1 || enc == 2) {                 /* UTF-16: keep ASCII bytes */
        for(size_t i = 0; i < len && used + 1 < cap; i++)
            if(text[i] >= 0x20 && text[i] < 0x7F) out[used++] = (char)text[i];
    }
    else {
        for(size_t i = 0; i < len && used + 1 < cap; i++)
            if(text[i] >= 0x20 || text[i] == 0x09) out[used++] = (char)text[i];
    }
    out[used] = '\0';
}

static int parse_mp3(const uint8_t *d, size_t n, vqeaf_media_info_t *info)
{
    static const int bitrate[16] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
    static const int srate[4] = {44100,48000,32000,0};
    size_t audio = 0;
    if(has(d, n, 0, "ID3", 3) && n >= 10) {
        uint32_t tag = synchsafe(d + 6);
        size_t pos = 10;
        size_t end = 10 + tag;
        if(end > n) end = n;
        uint8_t ver = d[3];
        while(pos + 10 <= end) {
            char id[5] = {0};
            memcpy(id, d + pos, 4);
            if(id[0] == 0) break;
            uint32_t fs = ver == 4 ? synchsafe(d + pos + 4) : be32(d + pos + 4);
            size_t body = pos + 10;
            if(fs == 0 || body + fs > end) break;
            if(strcmp(id, "TIT2") == 0) id3_text(d + body, fs, info->title, sizeof(info->title));
            else if(strcmp(id, "TPE1") == 0) id3_text(d + body, fs, info->artist, sizeof(info->artist));
            else if(strcmp(id, "TALB") == 0) id3_text(d + body, fs, info->album, sizeof(info->album));
            pos = body + fs;
        }
        audio = end;
    }
    /* first frame sync */
    int found = 0;
    for(size_t i = audio; i + 4 <= n; i++) {
        if(d[i] == 0xFF && (d[i + 1] & 0xE0) == 0xE0) {
            int br = (d[i + 2] >> 4) & 0x0F;
            int sr = (d[i + 2] >> 2) & 0x03;
            if(bitrate[br] > 0 && srate[sr] > 0) {
                info->bitrate_kbps = bitrate[br];
                info->sample_rate = srate[sr];
                info->channels = ((d[i + 3] >> 6) & 0x03) == 3 ? 1 : 2;
                uint64_t bytes = n > i ? n - i : 0;
                info->duration_ms = (int)(bytes * 8 / (uint32_t)bitrate[br]);
                found = 1;
            }
            break;
        }
    }
    if(!found && info->title[0] == '\0') return -1;
    info->type = VQEAF_MEDIA_MP3;
    if(info->title[0] != '\0')
        snprintf(info->summary, sizeof(info->summary),
                 "MP3 \"%s\" - %s, %d kbps, %.1f s", info->title,
                 info->artist[0] ? info->artist : "?", info->bitrate_kbps,
                 info->duration_ms / 1000.0);
    else
        snprintf(info->summary, sizeof(info->summary),
                 "MP3 %d kbps, %d Hz, %.1f s", info->bitrate_kbps,
                 info->sample_rate, info->duration_ms / 1000.0);
    return 0;
}

/* ---- video / document ---------------------------------------------------- */

static int parse_mp4(const uint8_t *d, size_t n, vqeaf_media_info_t *info)
{
    if(n < 12 || !has(d, n, 4, "ftyp", 4)) return -1;
    info->type = VQEAF_MEDIA_MP4;
    memcpy(info->brand, d + 8, 4);
    info->brand[4] = '\0';
    size_t pos = 0;
    while(pos + 8 <= n) {
        uint32_t size = be32(d + pos);
        const uint8_t *type = d + pos + 4;
        if(size < 8) break;
        if(size > n - pos) size = (uint32_t)(n - pos);
        if(memcmp(type, "moov", 4) == 0) {
            size_t m = pos + 8;
            size_t mend = pos + size;
            while(m + 8 <= mend) {
                uint32_t ms = be32(d + m);
                const uint8_t *mt = d + m + 4;
                if(ms < 8) break;
                if(ms > mend - m) ms = (uint32_t)(mend - m);
                if(memcmp(mt, "mvhd", 4) == 0 && m + 8 + 4 <= mend) {
                    uint8_t ver = d[m + 8];
                    uint32_t timescale, duration;
                    if(ver == 1 && m + 36 <= mend) {
                        timescale = be32(d + m + 28);
                        duration = be32(d + m + 32);   /* low 32 bits */
                    }
                    else if(m + 28 <= mend) {
                        timescale = be32(d + m + 20);
                        duration = be32(d + m + 24);
                    }
                    else { m += ms; continue; }
                    if(timescale > 0)
                        info->duration_ms = (int)((uint64_t)duration * 1000 / timescale);
                }
                else if(memcmp(mt, "trak", 4) == 0) {
                    size_t t = m + 8;
                    size_t tend = m + ms;
                    while(t + 8 <= tend) {
                        uint32_t ts = be32(d + t);
                        if(ts < 8) break;
                        if(ts > tend - t) ts = (uint32_t)(tend - t);
                        if(memcmp(d + t + 4, "tkhd", 4) == 0 && t + 8 + 84 <= tend) {
                            uint8_t ver = d[t + 8];
                            size_t wh = t + 8 + (ver == 1 ? 96 : 84) - 8;
                            if(wh + 8 <= tend) {
                                int w = (int)(be32(d + wh) >> 16);
                                int h = (int)(be32(d + wh + 4) >> 16);
                                if(w > info->width) info->width = w;
                                if(h > info->height) info->height = h;
                            }
                        }
                        t += ts;
                    }
                }
                m += ms;
            }
        }
        pos += size;
    }
    snprintf(info->summary, sizeof(info->summary),
             "MP4 brand %s, %dx%d, %.1f s", info->brand, info->width,
             info->height, info->duration_ms / 1000.0);
    return 0;
}

static int parse_pdf(const uint8_t *d, size_t n, vqeaf_media_info_t *info)
{
    if(n < 8 || !has(d, n, 0, "%PDF-", 5)) return -1;
    info->type = VQEAF_MEDIA_PDF;
    char ver[8] = {0};
    memcpy(ver, d + 5, 3);
    int pages = 0;
    for(size_t i = 0; i + 5 <= n; i++) {
        if(memcmp(d + i, "/Type", 5) != 0) continue;
        size_t j = i + 5;
        while(j < n && (d[j] == ' ' || d[j] == '\t' || d[j] == '\r' || d[j] == '\n')) j++;
        if(j + 5 <= n && memcmp(d + j, "/Page", 5) == 0 &&
           !(j + 6 <= n && d[j + 5] == 's'))
            pages++;
    }
    info->page_count = pages;
    snprintf(info->summary, sizeof(info->summary), "PDF %s, %d trang",
             ver, pages);
    return 0;
}

static int looks_text(const uint8_t *d, size_t n)
{
    if(n == 0) return 0;
    size_t check = n < 4096 ? n : 4096;
    size_t good = 0;
    for(size_t i = 0; i < check; i++) {
        uint8_t c = d[i];
        if(c == '\t' || c == '\n' || c == '\r' || (c >= 0x20 && c < 0x7F) || c >= 0x80)
            good++;
    }
    return good * 100 >= check * 95;
}

int vqeaf_media_inspect(const uint8_t *data, size_t len,
                        vqeaf_media_info_t *info)
{
    if(data == NULL || info == NULL || len == 0) return -1;
    memset(info, 0, sizeof(*info));

    if(parse_png(data, len, info) == 0) return 0;
    if(parse_jpeg(data, len, info) == 0) return 0;
    if(parse_gif(data, len, info) == 0) return 0;
    if(parse_bmp(data, len, info) == 0) return 0;
    if(parse_wav(data, len, info) == 0) return 0;
    if(has(data, len, 4, "ftyp", 4) && parse_mp4(data, len, info) == 0) return 0;
    if(parse_pdf(data, len, info) == 0) return 0;
    if(parse_mp3(data, len, info) == 0) return 0;
    if(has(data, len, 0, "PK\x03\x04", 4)) {
        info->type = VQEAF_MEDIA_ZIP;
        snprintf(info->summary, sizeof(info->summary), "ZIP (kho lưu trữ)");
        return 0;
    }
    if(looks_text(data, len)) {
        info->type = VQEAF_MEDIA_TEXT;
        snprintf(info->summary, sizeof(info->summary), "Văn bản / UTF-8");
        return 0;
    }
    return -1;
}