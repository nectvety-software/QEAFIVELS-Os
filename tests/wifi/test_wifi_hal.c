/* SPDX-License-Identifier: MIT */
/* Host-side C runner for the Violet Wi-Fi HAL. Uses FIXTURE inputs only. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vqeaf/wifi_device.h"
#include "vqeaf/wifi_firmware.h"

static const uint8_t FW_BLOB[] = {0x51, 0x45, 0x41, 0x46, 0x01, 0x02, 0x03};

typedef struct { const uint8_t *data; size_t len; int err; int calls; } io_ctx;

static int io_read(void *p, const char *path, uint8_t *buf, size_t cap,
                   size_t *out_len)
{
    io_ctx *c = (io_ctx *)p;
    (void)path;
    c->calls++;
    if(c->err) return c->err;
    if(c->len > cap) return -ENOSPC;
    memcpy(buf, c->data, c->len);
    *out_len = c->len;
    return 0;
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long n;
    if(!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(n <= 0) { fclose(f); return NULL; }
    buf = (uint8_t *)malloc((size_t)n);
    if(!buf) { fclose(f); return NULL; }
    if(fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

int main(int argc, char **argv)
{
    size_t dtb_len = 0;
    uint8_t *dtb;
    vqeaf_wifi_device_t dev;
    vqeaf_wifi_firmware_t fw;
    vqeaf_wifi_firmware_io_t io;
    io_ctx ctx;
    uint8_t buf[64];

    if(argc < 2) { fprintf(stderr, "usage: %s FIXTURE.dtb\n", argv[0]); return 2; }
    dtb = read_file(argv[1], &dtb_len);
    assert(dtb != NULL);
    assert(vqeaf_wifi_fdt_valid(dtb, dtb_len) == 0);
    assert(vqeaf_wifi_fdt_valid(dtb, 8) < 0);          /* truncated */
    assert(vqeaf_wifi_fdt_valid("BADBADBADBAD", 12) < 0); /* bad magic */

    memset(&dev, 0, sizeof(dev));
    assert(vqeaf_wifi_fdt_discover(dtb, dtb_len, &dev) == 0);
    assert(dev.bus == VQEAF_WIFI_BUS_SNOC);
    assert(strcmp(vqeaf_wifi_bus_name(dev.bus), "snoc") == 0);
    assert(dev.mmio_base == 0x18800000ull);
    assert(dev.mmio_size == 0x800000ull);
    assert(dev.msa_found == 1);
    assert(dev.msa_base == 0x93900000ull);
    assert(dev.msa_size == 0x200000ull);
    assert(dev.irq_count == 12);
    for(size_t i = 0; i < dev.irq_count; i++)
        assert(dev.irqs[i] == 130u + (uint32_t)i);
    assert(strstr(dev.path, "wifi@18800000") != NULL);
    vqeaf_wifi_device_release(&dev);
    assert(dev.irq_count == 0);

    /* No node with the supported compatible -> honest ENODEV, no values. */
    assert(vqeaf_wifi_fdt_discover("BADBADBADBAD", 12, &dev) < 0);

    memset(&ctx, 0, sizeof(ctx));
    ctx.data = FW_BLOB;
    ctx.len = sizeof(FW_BLOB);
    io.ctx = &ctx;
    io.read = io_read;
    assert(vqeaf_wifi_firmware_load(&io, &fw, buf, sizeof(buf),
                                    "wlanmdsp.mbn", 32) == 0);
    assert(fw.len == sizeof(FW_BLOB));
    assert(fw.hash == vqeaf_wifi_firmware_hash(&fw));
    assert(fw.hash != 0);
    vqeaf_wifi_firmware_release(&fw);
    assert(vqeaf_wifi_firmware_hash(&fw) == 0);

    ctx.len = 0; /* empty file must not be accepted as a real firmware */
    assert(vqeaf_wifi_firmware_load(&io, &fw, buf, sizeof(buf),
                                    "wlanmdsp.mbn", 32) == -ENODATA);
    ctx.len = sizeof(FW_BLOB);
    ctx.err = -EIO; /* platform read failure propagates */
    assert(vqeaf_wifi_firmware_load(&io, &fw, buf, sizeof(buf),
                                    "wlanmdsp.mbn", 32) == -EIO);

    free(dtb);
    printf("test_wifi_hal: ALL OK\n");
    return 0;
}
