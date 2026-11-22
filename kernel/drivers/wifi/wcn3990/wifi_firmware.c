/* SPDX-License-Identifier: MIT */
/*
 * wifi_firmware.c — portable firmware lifecycle for WCN3990.
 * Reads through a platform callback into a caller-owned buffer; validates
 * size; computes a real hash. Never synthesizes a blob, never writes storage,
 * never logs file contents.
 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "vqeaf/wifi_firmware.h"

void vqeaf_wifi_firmware_init(vqeaf_wifi_firmware_t *fw)
{
    if(fw == NULL) return;
    memset(fw, 0, sizeof(*fw));
}

static uint32_t fnv1a(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    size_t i;
    for(i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h ? h : 1u;
}

int vqeaf_wifi_firmware_load(const vqeaf_wifi_firmware_io_t *io,
                             vqeaf_wifi_firmware_t *fw,
                             uint8_t *buf, size_t buf_cap,
                             const char *path, size_t max_bytes)
{
    size_t len = 0;
    size_t cap;
    int rc;
    if(io == NULL || fw == NULL || io->read == NULL || path == NULL ||
       buf == NULL || max_bytes == 0) return -EINVAL;
    vqeaf_wifi_firmware_init(fw);
    cap = buf_cap < max_bytes ? buf_cap : max_bytes;
    rc = io->read(io->ctx, path, buf, cap, &len);
    if(rc) return rc;
    if(len == 0 || len > cap) return -ENODATA;
    fw->data = buf;
    fw->len = len;
    fw->path = path;
    fw->hash = fnv1a(buf, len);
    return 0;
}

void vqeaf_wifi_firmware_release(vqeaf_wifi_firmware_t *fw)
{
    if(fw != NULL) vqeaf_wifi_firmware_init(fw);
}

uint32_t vqeaf_wifi_firmware_hash(const vqeaf_wifi_firmware_t *fw)
{
    if(fw == NULL || fw->data == NULL) return 0;
    return fw->hash;
}

/* ---- Structural validation ----
 * Little-endian byte accessors: the firmware container is defined LE and the
 * freestanding kernel build has no unaligned-load helpers. */
static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

const char *vqeaf_wifi_fw_format_name(vqeaf_wifi_fw_format_t f)
{
    switch(f) {
    case VQEAF_WIFI_FW_FMT_ELF32: return "elf32";
    case VQEAF_WIFI_FW_FMT_ELF64: return "elf64";
    case VQEAF_WIFI_FW_FMT_BDF:   return "bdf";
    case VQEAF_WIFI_FW_FMT_REGDB: return "regdb";
    default:                      return "unknown";
    }
}

/* Real size of the blob: the explicit callback when the platform has one,
 * otherwise the bytes the reader returned (caller must pass a buffer big
 * enough for the whole file in that case). */
static int fw_size(const vqeaf_wifi_firmware_io_t *io, const char *path,
                   size_t *out)
{
    if(io->size != NULL) return io->size(io->ctx, path, out);
    return -ENOTSUP;
}

/* ELF program-header walk. Returns 0 when the header is self-consistent and at
 * least one PT_LOAD segment carries bytes inside the file. */
static int elf_probe(const uint8_t *h, size_t got, size_t file_size,
                     int is64, vqeaf_wifi_fw_info_t *out)
{
    const size_t ehsize = is64 ? 64u : 52u;
    size_t phoff, phentsize, phnum, i, table_end;
    uint64_t payload = 0;
    uint64_t entry;
    uint32_t loads = 0;
    int entry_ok = 0;

    if(got < ehsize) return -EINVAL;
    if(h[5] != 1u) return -ENOTSUP;              /* little-endian only */
    if(is64) {
        phoff = (size_t)rd64(h + 32);
        entry = rd64(h + 24);
        phentsize = rd16(h + 54);
        phnum = rd16(h + 56);
    } else {
        phoff = rd32(h + 28);
        entry = rd32(h + 24);
        phentsize = rd16(h + 42);
        phnum = rd16(h + 44);
    }
    if(phnum == 0 || phnum > 64) return -EINVAL;
    if(phentsize != (is64 ? 56u : 32u)) return -EINVAL;
    if(phoff < ehsize) return -EINVAL;
    if(phnum > (SIZE_MAX / phentsize)) return -EINVAL;
    table_end = phoff + phnum * phentsize;
    if(table_end < phoff || table_end > file_size) return -EINVAL;
    if(table_end > got) return -EINVAL;          /* header prefix too short */

    for(i = 0; i < phnum; i++) {
        const uint8_t *ph = h + phoff + i * phentsize;
        uint32_t type = rd32(ph);
        uint64_t off = is64 ? rd64(ph + 8) : rd32(ph + 4);
        uint64_t vaddr = is64 ? rd64(ph + 16) : rd32(ph + 8);
        uint64_t filesz = is64 ? rd64(ph + 32) : rd32(ph + 16);
        uint64_t memsz = is64 ? rd64(ph + 40) : rd32(ph + 20);
        if(type != 1u) continue;                 /* PT_LOAD only */
        if(filesz == 0) continue;
        if(off > file_size || filesz > file_size - off) return -EINVAL;
        if(payload > UINT64_MAX - filesz) return -EINVAL;
        payload += filesz;
        loads++;
        if(entry >= vaddr && entry - vaddr < memsz) entry_ok = 1;
    }
    if(loads == 0 || payload == 0) return -EINVAL;
    /* An entry point outside every loadable segment means the image cannot
     * start where its header says it will. */
    if(!entry_ok) return -EINVAL;
    out->format = is64 ? VQEAF_WIFI_FW_FMT_ELF64 : VQEAF_WIFI_FW_FMT_ELF32;
    out->machine = rd16(h + 18);
    out->entry = (uint32_t)entry;
    out->segments = loads;
    out->payload_bytes = payload;
    return 0;
}

int vqeaf_wifi_fw_probe(const vqeaf_wifi_firmware_io_t *io, const char *path,
                        vqeaf_wifi_fw_info_t *out,
                        uint8_t *hdr, size_t hdr_cap)
{
    size_t size = 0, got = 0, want;
    int rc;
    if(io == NULL || io->read == NULL || path == NULL || out == NULL ||
       hdr == NULL || hdr_cap < 64) return -EINVAL;
    memset(out, 0, sizeof(*out));
    rc = fw_size(io, path, &size);
    if(rc == -ENOTSUP) {
        /* No size callback: the reader must return the whole file here. */
        rc = io->read(io->ctx, path, hdr, hdr_cap, &got);
        if(rc) return rc;
        if(got == 0) return -ENODATA;
        size = got;
    } else if(rc) {
        return rc;
    } else {
        if(size == 0) return -ENODATA;
        want = size < hdr_cap ? size : hdr_cap;
        rc = io->read(io->ctx, path, hdr, want, &got);
        if(rc) return rc;
        if(got == 0) return -ENODATA;
        if(got > size) got = size;
    }
    want = got < hdr_cap ? got : hdr_cap;
    out->size = size;

    if(want >= 4u && hdr[0] == 0x7fu && hdr[1] == 'E' &&
       hdr[2] == 'L' && hdr[3] == 'F') {
        if(hdr[4] != 1u && hdr[4] != 2u) return -ENOTSUP;
        rc = elf_probe(hdr, want, size, hdr[4] == 2u, out);
        if(rc) return rc;
        /* Silently reclassify: probe reports the container, the caller
         * decides whether the machine must be Hexagon. */
        return 0;
    }
    if(want >= 4u && rd32(hdr) == 0x00040ED3u) {
        if(size < 4096u) return -EINVAL;         /* regdb has a real body */
        out->format = VQEAF_WIFI_FW_FMT_REGDB;
        return 0;
    }
    if(want >= 2u && (rd16(hdr) == 1u || rd16(hdr) == 2u)) {
        /* WCN3990 board data: u16 version, whole-file length in bytes. */
        if(size < 4096u || (size % 4u) != 0) return -EINVAL;
        out->format = VQEAF_WIFI_FW_FMT_BDF;
        return 0;
    }
    return -ENOTSUP;
}

int vqeaf_wifi_fw_check_set(const vqeaf_wifi_firmware_io_t *io,
                            const vqeaf_wifi_fw_paths_t *paths,
                            vqeaf_wifi_fw_set_t *set,
                            uint8_t *hdr, size_t hdr_cap)
{
    int rc = 0;
    if(io == NULL || paths == NULL || set == NULL) return -EINVAL;
    memset(set, 0, sizeof(*set));
    set->dsp_rc = vqeaf_wifi_fw_probe(io, paths->dsp, &set->dsp, hdr, hdr_cap);
    set->board_rc = vqeaf_wifi_fw_probe(io, paths->board, &set->board, hdr, hdr_cap);
    set->regdb_rc = vqeaf_wifi_fw_probe(io, paths->regdb, &set->regdb, hdr, hdr_cap);

    /* The DSP image is executable WCN3990 code: a real container is not
     * enough, it must be a Hexagon (EM_QDSP6) ELF with loadable content. */
    if(set->dsp_rc == 0) {
        if(set->dsp.format != VQEAF_WIFI_FW_FMT_ELF32 ||
           set->dsp.machine != VQEAF_WIFI_FW_MACHINE_HEXAGON)
            set->dsp_rc = -ENOTSUP;
    }
    set->ok = set->dsp_rc == 0 && set->board_rc == 0 && set->regdb_rc == 0;
    if(set->dsp_rc) rc = set->dsp_rc;
    else if(set->board_rc) rc = set->board_rc;
    else if(set->regdb_rc) rc = set->regdb_rc;
    return rc;
}
