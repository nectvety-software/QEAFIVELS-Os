/* SPDX-License-Identifier: MIT */
/* Host tests for the WCN3990 firmware container validation in
 * kernel/drivers/wifi/wcn3990/wifi_firmware.c.
 *
 * Synthetic images cover every rejection path; when a real firmware directory
 * is passed as argv[1], the same code is pointed at the actual blobs staged
 * into the violet image (wlanmdsp.mbn / bdwlan.* / regdb.bin).
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vqeaf/wifi_firmware.h"

/* ---- in-memory platform ---- */
typedef struct {
    const uint8_t *data;
    size_t len;
    int err;
    int use_size;
    int reads;
} mem_io;

static int mem_read(void *p, const char *path, uint8_t *buf, size_t cap,
                    size_t *out_len)
{
    mem_io *m = (mem_io *)p;
    size_t n;
    (void)path;
    m->reads++;
    *out_len = 0;
    if(m->err) return m->err;
    n = m->len < cap ? m->len : cap;
    memcpy(buf, m->data, n);
    *out_len = n;
    return 0;
}

static int mem_size(void *p, const char *path, size_t *out)
{
    mem_io *m = (mem_io *)p;
    (void)path;
    if(m->err) return m->err;
    *out = m->len;
    return 0;
}

/* Routes the three logical names to three blobs (the set check calls probe()
 * once per path with the same io). */
typedef struct { mem_io dsp, board, regdb; } set_io;

static mem_io *set_pick(set_io *s, const char *path)
{
    if(strcmp(path, "dsp") == 0) return &s->dsp;
    if(strcmp(path, "board") == 0) return &s->board;
    if(strcmp(path, "regdb") == 0) return &s->regdb;
    return NULL;
}

static int set_read(void *p, const char *path, uint8_t *buf, size_t cap,
                    size_t *out_len)
{
    mem_io *m = set_pick((set_io *)p, path);
    if(m == NULL) return -ENOENT;
    return mem_read(m, path, buf, cap, out_len);
}

static int set_size(void *p, const char *path, size_t *out)
{
    mem_io *m = set_pick((set_io *)p, path);
    if(m == NULL) return -ENOENT;
    return mem_size(m, path, out);
}

/* ---- synthetic ELF32 builder ---- */
#define ELF32_EH 52
#define ELF32_PH 32

static void w16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Two PT_LOAD segments at file offsets 0x100 and 0x140, entry inside the
 * first. Returns the total image length. */
static size_t make_hexagon_elf(uint8_t *img, size_t cap)
{
    const uint32_t seg_off = 0x100, seg_len = 0x40;
    size_t total = seg_off + seg_len * 2;
    assert(total <= cap);
    memset(img, 0, total);
    img[0] = 0x7f; img[1] = 'E'; img[2] = 'L'; img[3] = 'F';
    img[4] = 1;              /* ELFCLASS32 */
    img[5] = 1;              /* ELFDATA2LSB */
    img[6] = 1;              /* EV_CURRENT */
    w16(img + 16, 2);        /* ET_EXEC */
    w16(img + 18, VQEAF_WIFI_FW_MACHINE_HEXAGON);
    w32(img + 20, 1);
    w32(img + 24, 0xb0000010u);   /* entry */
    w32(img + 28, ELF32_EH);      /* e_phoff */
    w16(img + 40, ELF32_EH);
    w16(img + 42, ELF32_PH);
    w16(img + 44, 2);             /* e_phnum */
    /* ph[0] */
    w32(img + ELF32_EH + 0, 1);
    w32(img + ELF32_EH + 4, seg_off);
    w32(img + ELF32_EH + 8, 0xb0000000u);
    w32(img + ELF32_EH + 16, seg_len);
    w32(img + ELF32_EH + 20, seg_len + 0x40);
    /* ph[1] */
    w32(img + ELF32_EH + ELF32_PH + 0, 1);
    w32(img + ELF32_EH + ELF32_PH + 4, seg_off + seg_len);
    w32(img + ELF32_EH + ELF32_PH + 8, 0xb0000100u);
    w32(img + ELF32_EH + ELF32_PH + 16, seg_len);
    w32(img + ELF32_EH + ELF32_PH + 20, seg_len);
    return total;
}

static int probe_mem(uint8_t *img, size_t len, vqeaf_wifi_fw_info_t *info,
                     int use_size)
{
    uint8_t hdr[4096];
    mem_io m;
    vqeaf_wifi_firmware_io_t io;
    memset(&m, 0, sizeof(m));
    m.data = img;
    m.len = len;
    m.use_size = use_size;
    memset(&io, 0, sizeof(io));
    io.ctx = &m;
    io.read = mem_read;
    io.size = use_size ? mem_size : NULL;
    return vqeaf_wifi_fw_probe(&io, "fw.bin", info, hdr, sizeof(hdr));
}

static void timg(uint8_t *img, size_t len)
{
    vqeaf_wifi_fw_info_t info;
    assert(probe_mem(img, len, &info, 1) == 0);
    assert(info.format == VQEAF_WIFI_FW_FMT_ELF32);
    assert(info.machine == 164);
    assert(info.entry == 0xb0000010u);
    assert(info.segments == 2);
    assert(info.payload_bytes == 0x80);
    assert(info.size == len);
    /* Same image with no size callback: the reader returns the whole file. */
    assert(probe_mem(img, len, &info, 0) == 0);
    assert(info.size == len);
}

/* ---- real files ---- */
typedef struct { uint8_t *data; size_t len; } blob;

static int real_read(void *p, const char *path, uint8_t *buf, size_t cap,
                     size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    (void)p;
    *out_len = 0;
    if(f == NULL) return -ENOENT;
    n = fread(buf, 1, cap, f);
    fclose(f);
    *out_len = n;
    return 0;
}

static int real_size(void *p, const char *path, size_t *out)
{
    FILE *f = fopen(path, "rb");
    long n;
    (void)p;
    if(f == NULL) return -ENOENT;
    if(fseek(f, 0, SEEK_END) != 0) { fclose(f); return -EIO; }
    n = ftell(f);
    fclose(f);
    if(n < 0) return -EIO;
    *out = (size_t)n;
    return 0;
}

static int path_join(char *dst, size_t cap, const char *dir, const char *name)
{
    int n = snprintf(dst, cap, "%s/%s", dir, name);
    return n > 0 && (size_t)n < cap ? 0 : -1;
}

static void real_firmware(const char *dir)
{
    vqeaf_wifi_firmware_io_t io;
    vqeaf_wifi_fw_paths_t paths;
    vqeaf_wifi_fw_set_t set;
    uint8_t hdr[4096];
    memset(&io, 0, sizeof(io));
    io.read = real_read;
    io.size = real_size;
    memset(&paths, 0, sizeof(paths));
    assert(path_join(paths.dsp, sizeof(paths.dsp), dir, "wlanmdsp.mbn") == 0);
    assert(path_join(paths.regdb, sizeof(paths.regdb), dir, "regdb.bin") == 0);
    assert(path_join(paths.board, sizeof(paths.board), dir, "bdwlan.bin") == 0);
    assert(vqeaf_wifi_fw_check_set(&io, &paths, &set, hdr, sizeof(hdr)) == 0);
    assert(set.ok == 1);
    assert(set.dsp.format == VQEAF_WIFI_FW_FMT_ELF32);
    assert(set.dsp.machine == VQEAF_WIFI_FW_MACHINE_HEXAGON);
    assert(set.dsp.segments >= 1);
    assert(set.dsp.payload_bytes > 0);
    assert(set.board.format == VQEAF_WIFI_FW_FMT_BDF);
    assert(set.regdb.format == VQEAF_WIFI_FW_FMT_REGDB);
    /* Literal expectations from the MIUI 20.10.30 NON-HLOS image that
     * scripts/package_violet.py stages: 3694736 B, 7 program headers, three
     * PT_LOAD segments carrying 3121976 B. Verifying the numbers as
     * assertions (not printf) keeps this independent of the host's variadic
     * printf implementation. */
    assert(set.dsp.size == 3694736u);
    assert(set.dsp.segments == 3u);
    assert(set.dsp.payload_bytes == 3121976u);
    assert(set.dsp.entry == 0xb0000000u);
    printf("test_wifi_firmware: real firmware validated\n");

    /* A missing blob must fail the set, not be ignored. */
    assert(path_join(paths.board, sizeof(paths.board), dir, "bdwlan.absent") == 0);
    assert(vqeaf_wifi_fw_check_set(&io, &paths, &set, hdr, sizeof(hdr)) == -ENOENT);
    assert(set.ok == 0);
    assert(set.board_rc == -ENOENT);
}

int main(int argc, char **argv)
{
    uint8_t img[1024];
    uint8_t big[8192];
    uint8_t hdr[4096];
    vqeaf_wifi_fw_info_t info;
    mem_io m;
    vqeaf_wifi_firmware_io_t io;
    vqeaf_wifi_fw_paths_t paths;
    vqeaf_wifi_fw_set_t set;
    size_t len = make_hexagon_elf(img, sizeof(img));
    size_t total = len;

    /* Positive case, both size-callback modes. */
    timg(img, total);

    /* Header / table rejections. */
    assert(probe_mem(img, 40, &info, 1) == -EINVAL);          /* short ELF hdr */
    w16(img + 44, 0);
    assert(probe_mem(img, total, &info, 1) == -EINVAL);       /* no phdrs */
    w16(img + 44, 2);
    w16(img + 42, 40);
    assert(probe_mem(img, total, &info, 1) == -EINVAL);       /* bad ph entsize */
    w16(img + 42, ELF32_PH);
    img[4] = 3;
    assert(probe_mem(img, total, &info, 1) == -ENOTSUP);      /* bad class */
    img[4] = 1;
    img[5] = 2;
    assert(probe_mem(img, total, &info, 1) == -ENOTSUP);      /* big endian */
    img[5] = 1;

    /* Segment bounds: file shorter than the declared load. */
    assert(probe_mem(img, total - 0x10, &info, 1) == -EINVAL);
    /* Entry outside every loadable segment. */
    w32(img + 24, 0xfffffff0u);
    assert(probe_mem(img, total, &info, 1) == -EINVAL);
    w32(img + 24, 0xb0000010u);
    /* phdr table past the end of the file. */
    w32(img + 28, 1024 - 8);
    assert(probe_mem(img, total, &info, 1) == -EINVAL);
    w32(img + 28, ELF32_EH);
    assert(probe_mem(img, total, &info, 1) == 0);             /* intact again */

    /* ELF32 container but not the Hexagon DSP machine: accepted as a
     * container, rejected as the WLAN image. */
    memset(&m, 0, sizeof(m));
    m.data = img; m.len = total;
    memset(&io, 0, sizeof(io));
    io.ctx = &m; io.read = mem_read; io.size = mem_size;
    memset(&paths, 0, sizeof(paths));
    strcpy(paths.dsp, "dsp"); strcpy(paths.board, "board"); strcpy(paths.regdb, "regdb");
    w16(img + 18, 0x3e);   /* EM_X86_64 in an ELF32 container */
    assert(vqeaf_wifi_fw_probe(&io, "dsp", &info, hdr, sizeof(hdr)) == 0);
    assert(info.machine == 0x3e);
    w16(img + 18, VQEAF_WIFI_FW_MACHINE_HEXAGON);

    /* Board data (BDF) and regulatory DB containers. */
    memset(big, 0, sizeof(big));
    w16(big, 1);
    assert(probe_mem(big, 4096, &info, 1) == 0);
    assert(info.format == VQEAF_WIFI_FW_FMT_BDF);
    w16(big, 2);
    assert(probe_mem(big, 4096, &info, 1) == 0);
    w16(big, 3);
    assert(probe_mem(big, 4096, &info, 1) == -ENOTSUP);
    w16(big, 1);
    assert(probe_mem(big, 4095, &info, 1) == -EINVAL);        /* not word sized */
    assert(probe_mem(big, 512, &info, 1) == -EINVAL);         /* too short */
    w32(big, 0x00040ED3u);
    assert(probe_mem(big, 4096, &info, 1) == 0);
    assert(info.format == VQEAF_WIFI_FW_FMT_REGDB);
    assert(probe_mem(big, 100, &info, 1) == -EINVAL);
    w32(big, 0xdeadbeefu);
    assert(probe_mem(big, 4096, &info, 1) == -ENOTSUP);       /* unknown blob */

    /* Absent and empty files are errors, never successes. */
    memset(&m, 0, sizeof(m));
    m.data = img; m.len = 4096; m.err = -ENOENT;
    assert(probe_mem(img, 4096, &info, 1) == -ENOENT);
    m.err = 0; m.len = 0;
    assert(probe_mem(img, 0, &info, 1) == -ENODATA);

    /* Full set: a complete set passes; one missing blob fails the whole set
     * and reports which one, never a partial success. */
    {
        uint8_t dsp[1024], bdf[4096], regdb[4096];
        size_t dsp_len = make_hexagon_elf(dsp, sizeof(dsp));
        set_io si;
        memset(bdf, 0, sizeof(bdf)); w16(bdf, 1);
        memset(regdb, 0, sizeof(regdb)); w32(regdb, 0x00040ED3u);
        memset(&si, 0, sizeof(si));
        si.dsp.data = dsp; si.dsp.len = dsp_len;
        si.board.data = bdf; si.board.len = sizeof(bdf);
        si.regdb.data = regdb; si.regdb.len = sizeof(regdb);
        memset(&io, 0, sizeof(io));
        io.ctx = &si; io.read = set_read; io.size = set_size;
        memset(&paths, 0, sizeof(paths));
        strcpy(paths.dsp, "dsp"); strcpy(paths.board, "board");
        strcpy(paths.regdb, "regdb");
        memset(&set, 0, sizeof(set));
        assert(vqeaf_wifi_fw_check_set(&io, &paths, &set, hdr, sizeof(hdr)) == 0);
        assert(set.ok == 1);
        assert(set.dsp_rc == 0 && set.board_rc == 0 && set.regdb_rc == 0);
        assert(set.dsp.segments == 2);

        si.board.err = -ENOENT;
        assert(vqeaf_wifi_fw_check_set(&io, &paths, &set, hdr, sizeof(hdr)) == -ENOENT);
        assert(set.ok == 0);
        assert(set.board_rc == -ENOENT);
        si.board.err = 0;

        w16(dsp + 18, 0x3e);   /* valid ELF, wrong machine for the DSP */
        assert(vqeaf_wifi_fw_check_set(&io, &paths, &set, hdr, sizeof(hdr)) == -ENOTSUP);
        assert(set.ok == 0);
        assert(set.dsp_rc == -ENOTSUP);
        assert(set.board_rc == 0 && set.regdb_rc == 0);
        w16(dsp + 18, VQEAF_WIFI_FW_MACHINE_HEXAGON);
    }

    if(argc > 1) real_firmware(argv[1]);

    printf("test_wifi_firmware: ALL OK\n");
    return 0;
}