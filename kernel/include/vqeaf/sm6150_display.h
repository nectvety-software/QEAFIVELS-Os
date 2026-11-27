#ifndef VQEAF_SM6150_DISPLAY_H
#define VQEAF_SM6150_DISPLAY_H
#include <stdint.h>
#include "vqeaf/driver.h"
#include "vqeaf/sm6150_dsi.h"
#include "vqeaf/violet_boot.h"

typedef struct {
    int (*clocks_set)(void *context, int enabled);
    int (*regulators_set)(void *context, int enabled);
    int (*gpio_set)(void *context, int high);
    void (*delay_us)(void *context, uint32_t microseconds);
    int (*panel_initialize)(void *context);
    int (*panel_deinitialize)(void *context);
    int (*backlight_set)(void *context, int enabled);
    int (*dpu_start_verified)(void *context,
                              const vqeaf_sm6150_dsi_mode_t *mode,
                              uint64_t framebuffer_pa, uint32_t stride);
    int (*dpu_stop)(void *context);
} vqeaf_sm6150_display_ops_t;

typedef struct {
    const vqeaf_violet_display_info_t *info;
    const vqeaf_sm6150_display_ops_t *ops;
    void *ops_context;
    const vqeaf_sm6150_dsi_bus_t *dsi_bus;
    uint32_t boot_handoff_verified;
    uint32_t mmu_enabled;
    uint32_t panel_commands_verified;
    const vqeaf_sm6150_panel_mode_t *panel_mode;
    uint64_t framebuffer_pa;
    uint32_t framebuffer_stride;
    uint32_t active;
} vqeaf_sm6150_display_device_t;

const VqeafDriver *vqeaf_sm6150_display_driver(void);
#endif
