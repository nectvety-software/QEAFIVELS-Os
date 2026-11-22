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
