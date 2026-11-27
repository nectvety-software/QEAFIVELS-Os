#include <stddef.h>
#include <string.h>
#include "vqeaf/sm6150_display.h"
#include "vqeaf/sm6150_panel.h"
#include "vqeaf/capability.h"

static int display_supply_present(const vqeaf_violet_display_info_t *d,
                                  const char *name)
{
    for (uint32_t i = 0; i < d->supply_count && i < VQEAF_VIOLET_MAX_DISPLAY_SUPPLIES;
         ++i) {
        if (strcmp(d->supplies[i].name, name) == 0) return 1;
    }
    return 0;
}

static int display_nodes_complete(const vqeaf_violet_display_info_t *d)
{
    uint32_t lanes = 0u;
    if (d == NULL || !d->found || !d->mdss_found || !d->dpu_found ||
        !d->dsi_found || !d->phy_found || !d->dispcc_found || !d->panel_found ||
        d->mdss_base == 0u || d->dpu_base == 0u || d->dsi_base == 0u ||
        d->phy_base == 0u || d->dispcc_base == 0u ||
        d->dsi_clock_count != 6u || d->lane_count != 4u ||
        d->reset_phandle == 0u || d->backlight_phandle == 0u ||
        d->supply_count < 5u)
        return 0;
    for (uint32_t i = 0; i < 4u; ++i) {
        if (d->lanes[i] > 3u) return 0;
        lanes |= 1u << d->lanes[i];
    }
    return lanes == 0xFu && display_supply_present(d, "vdda") &&
           display_supply_present(d, "vcca") &&
           display_supply_present(d, "vddi") &&
           display_supply_present(d, "avdd") &&
           display_supply_present(d, "avee");
}

static int display_match(void *device)
{
    const vqeaf_sm6150_display_device_t *d =
        (const vqeaf_sm6150_display_device_t *)device;
    return d != NULL && d->info != NULL && d->info->found ? 1 : 0;
}

static void display_defer(vqeaf_sm6150_display_device_t *d, int32_t detail)
{
    vqeaf_cap_set("display", VQEAF_CAP_PRESENT, 0u, detail);
    (void)d;
}

static int display_probe(void *device)
{
    vqeaf_sm6150_display_device_t *d = (vqeaf_sm6150_display_device_t *)device;
    vqeaf_sm6150_dsi_mode_t mode;
    uint32_t version = 0u;
    int clocks_on = 0, regulators_on = 0, panel_on = 0;
    int dsi_on = 0, scanout_on = 0;
    int rc;

    if (d == NULL || d->info == NULL) return -1;
    if (!display_nodes_complete(d->info)) {
        display_defer(d, -1);
        return VQEAF_DRIVER_DEFER;
    }
    if (!d->boot_handoff_verified || !d->mmu_enabled) {
        display_defer(d, -2);
        return VQEAF_DRIVER_DEFER;
    }
    if (!d->info->panel_timing_present ||
        !d->info->panel_command_data_present || !d->panel_commands_verified) {
        vqeaf_cap_set("display", VQEAF_CAP_BLOCKED_FIRMWARE, 0u, -3);
        return VQEAF_DRIVER_DEFER;
    }
    if (d->ops == NULL || d->dsi_bus == NULL || d->panel_mode == NULL ||
        d->dsi_bus->read32 == NULL || d->dsi_bus->write32 == NULL ||
        d->dsi_bus->delay_us == NULL || d->dsi_bus->poll_limit == 0u ||
        d->ops->clocks_set == NULL || d->ops->regulators_set == NULL ||
        d->ops->gpio_set == NULL || d->ops->delay_us == NULL ||
        d->ops->panel_initialize == NULL || d->ops->panel_deinitialize == NULL ||
        d->ops->backlight_set == NULL ||
        d->ops->dpu_start_verified == NULL ||
        d->ops->dpu_stop == NULL || d->framebuffer_pa == 0u ||
        (d->framebuffer_pa & 0xFFFu) != 0u ||
        d->framebuffer_stride < d->panel_mode->h_active * 4u ||
        (d->framebuffer_stride & 3u) != 0u) {
        display_defer(d, -4);
        return VQEAF_DRIVER_DEFER;
    }

    mode.timing = *d->panel_mode;
    mode.lane_count = d->info->lane_count;
    for (uint32_t i = 0; i < 4u; ++i)
        mode.logical_to_physical[i] = d->info->lanes[i];

    rc = d->ops->clocks_set(d->ops_context, 1);
    if (rc != 0) goto fail;
    clocks_on = 1;
    rc = d->ops->regulators_set(d->ops_context, 1);
    if (rc != 0) goto fail;
    regulators_on = 1;
    rc = vqeaf_sm6150_dsi_probe(d->dsi_bus, &version);
    if (rc != 0 || version != VQEAF_SM6150_DSI_6G_V2_3_1) {
        rc = -5;
        goto fail;
    }
    rc = vqeaf_sm6150_panel_reset(d->ops_context, d->ops->gpio_set,
                                  d->ops->delay_us);
    if (rc != 0) goto fail;
    rc = vqeaf_sm6150_dsi_configure(d->dsi_bus, &mode);
    if (rc != 0) goto fail;
    rc = d->ops->panel_initialize(d->ops_context);
    if (rc != 0) goto fail;
    panel_on = 1;
    rc = vqeaf_sm6150_dsi_enable(d->dsi_bus);
    if (rc != 0) goto fail;
    dsi_on = 1;
    rc = d->ops->dpu_start_verified(d->ops_context, &mode,
                                    d->framebuffer_pa, d->framebuffer_stride);
    if (rc != 0) goto fail;
    scanout_on = 1;
    rc = vqeaf_sm6150_dsi_wait_video_done(d->dsi_bus);
    if (rc != 0) goto fail;
    rc = d->ops->backlight_set(d->ops_context, 1);
    if (rc != 0) goto fail;
    d->active = 1u;
    vqeaf_cap_set("display", VQEAF_CAP_READY, 0u, 0);
    return 0;

fail:
    if (panel_on) d->ops->panel_deinitialize(d->ops_context);
    if (scanout_on) d->ops->dpu_stop(d->ops_context);
    if (dsi_on) vqeaf_sm6150_dsi_disable(d->dsi_bus);
    if (regulators_on) d->ops->regulators_set(d->ops_context, 0);
    if (clocks_on) d->ops->clocks_set(d->ops_context, 0);
    vqeaf_cap_set("display", VQEAF_CAP_ERROR, 0u, rc);
    return rc == 0 ? -1 : rc;
}

static int display_remove(void *device)
{
    vqeaf_sm6150_display_device_t *d = (vqeaf_sm6150_display_device_t *)device;
    if (d == NULL || !d->active) return -1;
    if (d->ops != NULL) {
        if (d->ops->backlight_set != NULL)
            d->ops->backlight_set(d->ops_context, 0);
        if (d->ops->panel_deinitialize != NULL)
            d->ops->panel_deinitialize(d->ops_context);
        if (d->ops->dpu_stop != NULL) d->ops->dpu_stop(d->ops_context);
        if (d->dsi_bus != NULL) vqeaf_sm6150_dsi_disable(d->dsi_bus);
        if (d->ops->regulators_set != NULL)
            d->ops->regulators_set(d->ops_context, 0);
        if (d->ops->clocks_set != NULL)
            d->ops->clocks_set(d->ops_context, 0);
    }
    d->active = 0u;
    vqeaf_cap_set("display", VQEAF_CAP_PRESENT, 0u, 0);
    return 0;
}

static const VqeafDriver display_driver = {
    VQEAF_DRIVER_ABI, sizeof(display_driver), "sm6150-display",
    VQEAF_DEV_DISPLAY, display_probe, display_remove, display_match
};

const VqeafDriver *vqeaf_sm6150_display_driver(void)
{
    return &display_driver;
}
