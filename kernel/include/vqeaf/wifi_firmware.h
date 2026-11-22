/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_WIFI_FIRMWARE_H
#define VQEAF_WIFI_FIRMWARE_H
#include <stdint.h>
#include <stddef.h>

/* Portable firmware lifecycle: reads through a platform callback so the same
 * code runs in the freestanding kernel (VFS backend) and in host tests (musl
 * backend). The caller owns the buffer (no allocator in this path), so this
 * never allocates dynamically and never writes to storage. */
typedef struct {
    void *ctx;
    /* Must fill buf up to cap, set *out_len, return 0 or negative errno. */
    int (*read)(void *ctx, const char *path,
                uint8_t *buf, size_t cap, size_t *out_len);
    /* Optional: real file size, so a probe can bound segment offsets without
     * reading the whole image. NULL means "the bytes read are the file". */
    int (*size)(void *ctx, const char *path, size_t *out_size);
} vqeaf_wifi_firmware_io_t;

typedef struct {
    const char *path;        /* real file path supplied by the platform */
    uint8_t *data;           /* caller buffer; NULL when not loaded */
    size_t len;
    uint32_t hash;           /* FNV-1a over data; 0 only when not loaded */
} vqeaf_wifi_firmware_t;

void vqeaf_wifi_firmware_init(vqeaf_wifi_firmware_t *fw);
/* Opens and validates a firmware blob (non-empty, bounded size).
 * Returns 0 or negative errno; -ENODATA when the file is empty/absent. */
int  vqeaf_wifi_firmware_load(const vqeaf_wifi_firmware_io_t *io,
                              vqeaf_wifi_firmware_t *fw,
                              uint8_t *buf, size_t buf_cap,
                              const char *path, size_t max_bytes);
void vqeaf_wifi_firmware_release(vqeaf_wifi_firmware_t *fw);
/* FNV-1a over the blob; 0 means not loaded. */
uint32_t vqeaf_wifi_firmware_hash(const vqeaf_wifi_firmware_t *fw);

/* ---- Structural validation, before any blob reaches the hardware ----
 * The WLAN DSP image is what WCN3990 executes, so a truncated or corrupt file
 * must be refused here instead of being handed to icnss. Only the container
 * (ELF program headers, BDF/regdb header) is checked: no signature is
 * invented and TZ authentication is still the hardware's job. */

typedef enum {
    VQEAF_WIFI_FW_FMT_UNKNOWN = 0,
    VQEAF_WIFI_FW_FMT_ELF32,   /* 32-bit little-endian ELF */
    VQEAF_WIFI_FW_FMT_ELF64,   /* 64-bit little-endian ELF */
    VQEAF_WIFI_FW_FMT_BDF,     /* WCN3990 board data (version 1/2 header) */
    VQEAF_WIFI_FW_FMT_REGDB    /* regulatory database (magic 0x00040ed3) */
} vqeaf_wifi_fw_format_t;

typedef struct {
    vqeaf_wifi_fw_format_t format;
    uint16_t machine;        /* ELF e_machine (164 = EM_QDSP6/Hexagon) */
    uint32_t entry;          /* ELF e_entry, low 32 bits */
    uint32_t segments;       /* PT_LOAD segments */
    uint64_t payload_bytes;  /* sum of PT_LOAD p_filesz */
    size_t size;             /* real file size in bytes */
} vqeaf_wifi_fw_info_t;

#define VQEAF_WIFI_FW_MACHINE_HEXAGON 164u

const char *vqeaf_wifi_fw_format_name(vqeaf_wifi_fw_format_t f);

/* Reads a bounded header prefix and validates it against the real file size.
 * Returns 0 when self-consistent, -ENOENT absent, -ENODATA empty,
 * -EINVAL malformed/truncated, -ENOTSUP unknown container. */
int vqeaf_wifi_fw_probe(const vqeaf_wifi_firmware_io_t *io, const char *path,
                        vqeaf_wifi_fw_info_t *out,
                        uint8_t *hdr, size_t hdr_cap);

/* The three blobs qcacld/icnss ask for on violet. */
#define VQEAF_WIFI_FW_PATH_MAX 96
typedef struct {
    char dsp[VQEAF_WIFI_FW_PATH_MAX];
    char board[VQEAF_WIFI_FW_PATH_MAX];
    char regdb[VQEAF_WIFI_FW_PATH_MAX];
} vqeaf_wifi_fw_paths_t;

typedef struct {
    int dsp_rc, board_rc, regdb_rc;   /* per-blob result, 0 = accepted */
    vqeaf_wifi_fw_info_t dsp, board, regdb;
    int ok;   /* 1 only when all three were read and validated and the DSP
               * image is a Hexagon ELF with a loadable segment */
} vqeaf_wifi_fw_set_t;

/* Returns 0 only when set->ok == 1; otherwise the first failing errno. */
int vqeaf_wifi_fw_check_set(const vqeaf_wifi_firmware_io_t *io,
                            const vqeaf_wifi_fw_paths_t *paths,
                            vqeaf_wifi_fw_set_t *set,
                            uint8_t *hdr, size_t hdr_cap);

#endif
