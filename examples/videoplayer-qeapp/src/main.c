/* videoplayer-qeapp/src/main.c - QVF video player for framework API v3.
 *
 * Demonstrates the two ABI v3 additions a media app needs:
 *   - read_file_part: streams a QVF frame by frame instead of loading the
 *     whole file (stays inside the 1 MiB default memory quota);
 *   - blit_pixels: puts a decoded RGB565 frame on the app framebuffer.
 *
 * The QVF container (docs/QEAF_VIDEO_FORMAT.md) holds RGB565 frames
 * compressed with the LZ4 block format; the tiny decoder below is the only
 * codec in the app. Everything shown is read from the real file: when the
 * file is missing, malformed or uses an unknown codec the player draws an
 * honest failure state instead of a placeholder picture.
 */
#include "qeaf_native_abi.h"

#include <stddef.h>
#include <stdint.h>

/* ---- QVF container (docs/QEAF_VIDEO_FORMAT.md) --------------------------- */

#define QVF_MAGIC_0 'Q'
#define QVF_MAGIC_1 'V'
#define QVF_MAGIC_2 'F'
#define QVF_MAGIC_3 '1'
#define QVF_VERSION 1u
#define QVF_CODEC_LZ4_RGB565 1u
#define QVF_HEADER_SIZE 32u
#define QVF_FRAME_ENTRY 8u   /* u32 raw_size + u32 comp_size */

#define PLAYER_CANVAS_W 720
#define PLAYER_CANVAS_H 1560
#define PLAYER_MAX_FRAMES 256
#define PLAYER_MAX_W 240
#define PLAYER_MAX_H 426     /* source frames may be up to 240x426 */
#define PLAYER_COMP_MAX (128u * 1024u)
#define PLAYER_VIDEO_PATH "apps/org.vqeaf.videoplayer/@media/VIDEO1.QVF"

typedef struct {
    const qeaf_app_api_v3 *api;
    int open;              /* header parsed and accepted */
    int failed;            /* terminal failure state */
    int w, h, fps, frames;
    int frame;             /* currently displayed index */
    uint32_t entry_off[PLAYER_MAX_FRAMES];
    uint16_t raw[PLAYER_MAX_W * PLAYER_MAX_H];
    uint16_t row[2 * PLAYER_MAX_W * 2];
    uint8_t comp[PLAYER_COMP_MAX];
    char note[40];
} player_t;

static player_t g_player;

/* ---- freestanding helpers ------------------------------------------------ */

static void player_log(const player_t *p, const char *message)
{
    if(p != NULL && p->api != NULL && p->api->log_utf8 != NULL)
        p->api->log_utf8(message);
}

static void player_draw_text(const player_t *p, int x, int y, const char *s)
{
    if(p->api != NULL && p->api->draw_text != NULL)
        p->api->draw_text(x, y, s);
}

static void player_clear(const player_t *p)
{
    /* blit a black row repeatedly: one 720x1 black line per row. */
    static uint16_t black[720];
    for(int y = 0; y < PLAYER_CANVAS_H; y++)
        p->api->blit_pixels(0, y, PLAYER_CANVAS_W, 1, PLAYER_CANVAS_W, black);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/* ---- LZ4 block decoder (the one codec in the app) ------------------------- */

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

/* ---- drawing --------------------------------------------------------------- */

static void player_draw_chrome(const player_t *p, const char *status)
{
    char info[40];
    int i = 0;
    const char *src;

    player_draw_text(p, 8, 8, "VIDEO PLAYER");
    src = "QVF LZ4-RGB565";
    for(; *src != '\0'; src++) info[i++] = *src;
    info[i++] = ' ';
    info[i++] = '0' + (p->w / 100) % 10;
    info[i++] = '0' + (p->w / 10) % 10;
    info[i++] = '0' + p->w % 10;
    info[i++] = 'x';
    info[i++] = '0' + (p->h / 100) % 10;
    info[i++] = '0' + (p->h / 10) % 10;
    info[i++] = '0' + p->h % 10;
    info[i] = '\0';
    player_draw_text(p, 8, 20, info);

    player_draw_text(p, 8, PLAYER_CANVAS_H - 36, status);
    player_draw_text(p, 8, PLAYER_CANVAS_H - 24, "OK NEXT  LEFT PREV");
}

static void player_fail(player_t *p, const char *line1, const char *line2)
{
    p->failed = 1;
    p->open = 0;
    player_clear(p);
    player_draw_text(p, 8, 8, "VIDEO PLAYER");
    player_draw_text(p, 8, 24, line1);
    if(line2 != NULL) player_draw_text(p, 8, 36, line2);
    player_log(p, "videoplayer: fail");
}

/* Decodes frame `index` from the QVF and blits it at 2x. Returns 0, or -1
 * with a drawn failure state. */
static int player_show_frame(player_t *p, int index)
{
    uint8_t entry[QVF_FRAME_ENTRY];
    uint32_t raw_size, comp_size;
    int scaled_w, scaled_h, x, y;
    const char *status;

    if(p->api->read_file_part(PLAYER_VIDEO_PATH, p->entry_off[index],
                              QVF_FRAME_ENTRY, entry) != 0) {
        player_fail(p, "FRAME READ FAILED", NULL);
        return -1;
    }
    raw_size = rd32(entry);
    comp_size = rd32(entry + 4);
    if(raw_size != (uint32_t)p->w * (uint32_t)p->h * 2u ||
       comp_size == 0u || comp_size > PLAYER_COMP_MAX) {
        player_fail(p, "BAD FRAME ENTRY", NULL);
        return -1;
    }
    if(p->api->read_file_part(PLAYER_VIDEO_PATH,
                              p->entry_off[index] + QVF_FRAME_ENTRY,
                              comp_size, p->comp) != 0) {
        player_fail(p, "FRAME READ FAILED", NULL);
        return -1;
    }
    if(lz4_decode(p->comp, comp_size, (uint8_t *)p->raw,
                  (size_t)p->w * (size_t)p->h * 2u) != 0) {
        player_fail(p, "FRAME DECODE FAILED", NULL);
        return -1;
    }

    /* Nearest-neighbour 2x, row by row, centred on the canvas. */
    scaled_w = p->w * 2;
    scaled_h = p->h * 2;
    x = (PLAYER_CANVAS_W - scaled_w) / 2;
    y = 64 + (PLAYER_CANVAS_H - 128 - scaled_h) / 2;
    for(int row = 0; row < scaled_h; row++) {
        const uint16_t *src = &p->raw[(size_t)(row / 2) * (size_t)p->w];
        uint16_t *dst = p->row;
        for(int col = 0; col < p->w; col++) {
            dst[col * 2] = src[col];
            dst[col * 2 + 1] = src[col];
        }
        if(p->api->blit_pixels(x, y + row, scaled_w, 1, scaled_w, p->row) != 0)
            return -1;
    }

    p->frame = index;
    status = "FRAME ";
    {
        char line[40];
        int i = 0;
        for(const char *s = status; *s != '\0'; s++) line[i++] = *s;
        line[i++] = '0' + ((index + 1) / 100) % 10;
        line[i++] = '0' + ((index + 1) / 10) % 10;
        line[i++] = '0' + (index + 1) % 10;
        line[i++] = '/';
        line[i++] = '0' + (p->frames / 100) % 10;
        line[i++] = '0' + (p->frames / 10) % 10;
        line[i++] = '0' + p->frames % 10;
        line[i] = '\0';
        player_draw_chrome(p, line);
    }
    return 0;
}

/* ---- lifecycle ------------------------------------------------------------ */

void *create(const qeaf_app_api_v2 *api)
{
    player_t *p = &g_player;
    if(api == NULL || api->abi_version != VQEAF_APP_API_V3_VERSION)
        return NULL;
    p->api = (const qeaf_app_api_v3 *)api;
    p->open = 0;
    p->failed = 0;
    p->w = p->h = p->fps = p->frames = p->frame = 0;
    p->note[0] = '\0';
    player_log(p, "videoplayer: create (api v3)");
    return p;
}

int start(void *handle)
{
    player_t *p = (player_t *)handle;
    uint8_t header[QVF_HEADER_SIZE];
    uint32_t version, codec, width, height, count, flags;
    uint32_t offset;

    if(p == NULL || p->api == NULL) return -1;

    if(p->api->read_file_part(PLAYER_VIDEO_PATH, 0, QVF_HEADER_SIZE,
                              header) != 0) {
        player_fail(p, "NO VIDEO FILE", PLAYER_VIDEO_PATH);
        return 0;
    }
    if(header[0] != QVF_MAGIC_0 || header[1] != QVF_MAGIC_1 ||
       header[2] != QVF_MAGIC_2 || header[3] != QVF_MAGIC_3) {
        player_fail(p, "UNSUPPORTED FORMAT", "not a QVF1 file");
        return 0;
    }
    version = rd16(header + 4);
    codec = rd16(header + 6);
    width = rd32(header + 8);
    height = rd32(header + 12);
    p->fps = (int)rd16(header + 16);
    count = rd32(header + 20);
    flags = rd32(header + 24);
    if(version != QVF_VERSION) {
        player_fail(p, "UNSUPPORTED VERSION", NULL);
        return 0;
    }
    if(codec != QVF_CODEC_LZ4_RGB565) {
        player_fail(p, "CODEC NOT SUPPORTED", NULL);
        return 0;
    }
    if(flags != 0u) {
        player_fail(p, "BAD QVF FLAGS", NULL);
        return 0;
    }
    if(width == 0u || height == 0u || width > PLAYER_MAX_W ||
       height > PLAYER_MAX_H || count == 0u || count > PLAYER_MAX_FRAMES) {
        player_fail(p, "VIDEO TOO LARGE", NULL);
        return 0;
    }
    p->w = (int)width;
    p->h = (int)height;
    p->frames = (int)count;

    /* Frame index table: each entry lives at a running offset. */
    offset = QVF_HEADER_SIZE;
    for(uint32_t i = 0; i < count; i++) {
        uint8_t entry[QVF_FRAME_ENTRY];
        uint32_t raw_size, comp_size;
        if(p->api->read_file_part(PLAYER_VIDEO_PATH, offset,
                                  QVF_FRAME_ENTRY, entry) != 0) {
            player_fail(p, "FRAME TABLE READ FAILED", NULL);
            return 0;
        }
        raw_size = rd32(entry);
        comp_size = rd32(entry + 4);
        if(raw_size != width * height * 2u || comp_size == 0u ||
           comp_size > PLAYER_COMP_MAX) {
            player_fail(p, "BAD FRAME ENTRY", NULL);
            return 0;
        }
        p->entry_off[i] = offset;
        offset += QVF_FRAME_ENTRY + comp_size;
    }
    p->open = 1;
    player_log(p, "videoplayer: qvf open");
    player_show_frame(p, 0);
    return 0;
}

int pause(void *handle)
{
    (void)handle;
    return 0;
}

int resume(void *handle)
{
    (void)handle;
    return 0;
}

int stop(void *handle)
{
    (void)handle;
    return 0;
}

void on_input(void *handle, const qeaf_input_event_t *event)
{
    player_t *p = (player_t *)handle;
    if(p == NULL || !p->open || event == NULL) return;
    if(event->type != QEAF_INPUT_KEY || !event->pressed) return;

    switch(event->code) {
    case 5u:                       /* OK: step forward */
    case 4u:                       /* RIGHT */
        if(p->frame + 1 < p->frames) player_show_frame(p, p->frame + 1);
        break;
    case 3u:                       /* LEFT */
        if(p->frame > 0) player_show_frame(p, p->frame - 1);
        break;
    default:
        break;
    }
}

void destroy(void *handle)
{
    player_t *p = (player_t *)handle;
    if(p == NULL) return;
    player_log(p, "videoplayer: destroy");
    p->open = 0;
    p->api = NULL;
}
