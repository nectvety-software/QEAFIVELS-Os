/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_WIFI_BUS_H
#define VQEAF_WIFI_BUS_H
#include <stdint.h>
typedef struct {
    void *ctx;
    int (*map_mmio)(void *, uint64_t base, uint64_t size);
    void (*unmap_mmio)(void *);
    int (*set_master)(void *, int on);
} vqeaf_wifi_bus_ops_t;
#endif
