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

#endif
