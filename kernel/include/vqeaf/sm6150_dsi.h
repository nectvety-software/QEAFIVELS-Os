#ifndef VQEAF_SM6150_DSI_H
#define VQEAF_SM6150_DSI_H
#include <stddef.h>
#include <stdint.h>
#include "vqeaf/sm6150_panel.h"

#define VQEAF_SM6150_DSI_6G_V2_3_1 0x20030001u

/* Bus offsets are byte offsets from the DSI controller base. */
typedef uint32_t (*vqeaf_sm6150_read32_fn)(void *context, uint32_t offset);
typedef void (*vqeaf_sm6150_write32_fn)(void *context, uint32_t offset,
                                        uint32_t value);
typedef void (*vqeaf_sm6150_delay_us_fn)(void *context, uint32_t microseconds);
typedef struct {
    vqeaf_sm6150_read32_fn read32;
    vqeaf_sm6150_write32_fn write32;
    vqeaf_sm6150_delay_us_fn delay_us;
    void *context;
    uint32_t poll_limit;
} vqeaf_sm6150_dsi_bus_t;

typedef struct {
    vqeaf_sm6150_panel_mode_t timing;
    uint32_t lane_count;
    uint8_t logical_to_physical[4];
} vqeaf_sm6150_dsi_mode_t;

int vqeaf_sm6150_dsi_probe(const vqeaf_sm6150_dsi_bus_t *bus,
                           uint32_t *version);
int vqeaf_sm6150_dsi_configure(const vqeaf_sm6150_dsi_bus_t *bus,
                               const vqeaf_sm6150_dsi_mode_t *mode);
int vqeaf_sm6150_dsi_enable(const vqeaf_sm6150_dsi_bus_t *bus);
int vqeaf_sm6150_dsi_wait_video_done(const vqeaf_sm6150_dsi_bus_t *bus);
int vqeaf_sm6150_dsi_disable(const vqeaf_sm6150_dsi_bus_t *bus);
#endif
