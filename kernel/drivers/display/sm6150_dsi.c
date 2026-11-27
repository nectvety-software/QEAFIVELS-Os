#include <stddef.h>
#include <stdint.h>
#include "vqeaf/sm6150_dsi.h"

#define DSI_REG_SHIFT 4u
#define DSI_HW_VERSION 0x000u
#define DSI_CTRL 0x000u
#define DSI_VID_CFG0 0x00cu
#define DSI_VID_CFG1 0x01cu
#define DSI_ACTIVE_H 0x020u
#define DSI_ACTIVE_V 0x024u
#define DSI_TOTAL 0x028u
#define DSI_ACTIVE_HSYNC 0x02cu
#define DSI_ACTIVE_VSYNC_HPOS 0x030u
#define DSI_ACTIVE_VSYNC_VPOS 0x034u
#define DSI_TRIG_CTRL 0x080u
#define DSI_LANE_SWAP_CTRL 0x0acu
#define DSI_ERR_INT_MASK0 0x108u
#define DSI_INTR_CTRL 0x10cu
#define DSI_RESET 0x114u
#define DSI_CLK_CTRL 0x118u
#define DSI_VERSION_RAW 0x1f0u

#define DSI_CTRL_ENABLE 0x1u
#define DSI_CTRL_VID_MODE_EN 0x2u
#define DSI_CTRL_CMD_MODE_EN 0x4u
#define DSI_CTRL_CLK_EN 0x100u
#define DSI_CTRL_LANE1 0x10u
#define DSI_CTRL_LANE2 0x20u
#define DSI_CTRL_LANE3 0x40u
#define DSI_CTRL_LANE4 0x80u
#define DSI_CTRL_MODE_MASK (DSI_CTRL_ENABLE | DSI_CTRL_VID_MODE_EN | \
                            DSI_CTRL_CMD_MODE_EN)
#define DSI_CLK_CTRL_ENABLE 0x23fu
#define DSI_RESET_ASSERT 0x1u
#define DSI_VIDEO_DONE_RAW 0x00010000u
#define DSI_VIDEO_DONE_MASK 0x00020000u
#define DSI_ERROR_RAW 0x01000000u
#define DSI_ERROR_MASK 0x02000000u
#define DSI_ERR_INT_MASK_DEFAULT 0x13ff3fe0u
#define DSI_TRIG_CTRL_SW_DMA 0x80001004u
#define DSI_RESET_DELAY_US 20000u
#define DSI_RGB888_FORMAT 3u

static int bus_io_valid(const vqeaf_sm6150_dsi_bus_t *bus)
{
    return bus != NULL && bus->read32 != NULL && bus->write32 != NULL &&
           bus->poll_limit != 0u;
}

static int bus_valid(const vqeaf_sm6150_dsi_bus_t *bus)
{
    return bus_io_valid(bus) && bus->delay_us != NULL;
}

static uint32_t reg_read(const vqeaf_sm6150_dsi_bus_t *bus, uint32_t offset)
{
    return bus->read32(bus->context, DSI_REG_SHIFT + offset);
}

static void reg_write(const vqeaf_sm6150_dsi_bus_t *bus, uint32_t offset,
                      uint32_t value)
{
    bus->write32(bus->context, DSI_REG_SHIFT + offset, value);
}

static uint32_t lane_bits(uint32_t lane_count)
{
    switch (lane_count) {
    case 1u: return DSI_CTRL_LANE1;
    case 2u: return DSI_CTRL_LANE1 | DSI_CTRL_LANE2;
    case 3u: return DSI_CTRL_LANE1 | DSI_CTRL_LANE2 | DSI_CTRL_LANE3;
    default: return DSI_CTRL_LANE1 | DSI_CTRL_LANE2 | DSI_CTRL_LANE3 |
                    DSI_CTRL_LANE4;
    }
}

static int lane_swap(const uint8_t *map, uint32_t lane_count, uint32_t *value)
{
    static const uint8_t maps[8][4] = {
        {0u, 1u, 2u, 3u}, {3u, 0u, 1u, 2u},
        {2u, 3u, 0u, 1u}, {1u, 2u, 3u, 0u},
        {0u, 3u, 2u, 1u}, {1u, 0u, 3u, 2u},
        {2u, 1u, 0u, 3u}, {3u, 2u, 1u, 0u}
    };
    uint32_t i, j, seen = 0u;

    if (lane_count == 0u || lane_count > 4u || value == NULL) return 0;
    for (i = 0u; i < lane_count; ++i) {
        if (map[i] > 3u || (seen & (1u << map[i])) != 0u) return 0;
        seen |= 1u << map[i];
    }
    for (i = 0u; i < 8u; ++i) {
        for (j = 0u; j < lane_count; ++j) {
            if (map[j] != maps[i][j]) break;
        }
        if (j == lane_count) {
            *value = i;
            return 1;
        }
    }
    return 0;
}

static int mode_valid(const vqeaf_sm6150_dsi_mode_t *mode)
{
    const vqeaf_sm6150_panel_mode_t *t = &mode->timing;
    uint32_t swap;

    if (t->h_active == 0u || t->v_active == 0u || t->h_pulse == 0u ||
        t->v_pulse == 0u || t->refresh_hz == 0u ||
        t->pixel_clock_hz == 0u || t->pixel_clock_hz > UINT32_MAX ||
        t->dsi_traffic_mode > VQEAF_SM6150_DSI_TRAFFIC_BURST ||
        t->h_active > UINT16_MAX || t->h_front > UINT16_MAX ||
        t->h_back > UINT16_MAX || t->h_pulse > UINT16_MAX ||
        t->h_total > UINT16_MAX || t->v_active > UINT16_MAX ||
        t->v_front > UINT16_MAX || t->v_back > UINT16_MAX ||
        t->v_pulse > UINT16_MAX || t->v_total > UINT16_MAX)
        return 0;
    if (t->h_active + t->h_front + t->h_pulse + t->h_back != t->h_total ||
        t->v_active + t->v_front + t->v_pulse + t->v_back != t->v_total)
        return 0;
    return lane_swap(mode->logical_to_physical, mode->lane_count, &swap);
}

static void dsi_reset(const vqeaf_sm6150_dsi_bus_t *bus)
{
    uint32_t ctrl = reg_read(bus, DSI_CTRL);

    if ((ctrl & DSI_CTRL_MODE_MASK) != 0u) {
        reg_write(bus, DSI_CTRL, ctrl & ~DSI_CTRL_MODE_MASK);
    }
    reg_write(bus, DSI_CLK_CTRL, DSI_CLK_CTRL_ENABLE);
    reg_write(bus, DSI_RESET, DSI_RESET_ASSERT);
    bus->delay_us(bus->context, DSI_RESET_DELAY_US);
    reg_write(bus, DSI_RESET, 0u);
}

int vqeaf_sm6150_dsi_probe(const vqeaf_sm6150_dsi_bus_t *bus,
                           uint32_t *version)
{
    uint32_t raw, shifted, hw;

    if (!bus_io_valid(bus) || version == NULL) return -1;
    *version = 0u;
    raw = bus->read32(bus->context, DSI_VERSION_RAW);
    if (raw != 0u) {
        *version = raw;
        return -2;
    }
    shifted = bus->read32(bus->context, DSI_VERSION_RAW + DSI_REG_SHIFT);
    if ((shifted >> 24u) != 2u) {
        *version = bus->read32(bus->context, DSI_HW_VERSION);
        return -2;
    }
    hw = bus->read32(bus->context, DSI_HW_VERSION);
    *version = hw;
    return hw == VQEAF_SM6150_DSI_6G_V2_3_1 ? 0 : -2;
}

int vqeaf_sm6150_dsi_configure(const vqeaf_sm6150_dsi_bus_t *bus,
                               const vqeaf_sm6150_dsi_mode_t *mode)
{
    const vqeaf_sm6150_panel_mode_t *t;
    uint32_t swap, ctrl, hs_start, hs_end, ha_start, ha_end;
    uint32_t vs_start, vs_end, va_start, va_end, vid_cfg0;

    if (!bus_valid(bus) || mode == NULL || !mode_valid(mode)) return -1;
    t = &mode->timing;
    if (!lane_swap(mode->logical_to_physical, mode->lane_count, &swap)) return -1;

    hs_start = t->h_active + t->h_front;
    hs_end = hs_start + t->h_pulse;
    ha_start = t->h_total - hs_start;
    ha_end = ha_start + t->h_active;
    vs_start = t->v_active + t->v_front;
    vs_end = vs_start + t->v_pulse;
    va_start = t->v_total - vs_start;
    va_end = va_start + t->v_active;

    dsi_reset(bus);
    reg_write(bus, DSI_VID_CFG1, 0u);
    vid_cfg0 = (t->dsi_traffic_mode << 8u) | (DSI_RGB888_FORMAT << 4u);
    reg_write(bus, DSI_VID_CFG0, vid_cfg0);
    reg_write(bus, DSI_ACTIVE_H, ha_start | (ha_end << 16u));
    reg_write(bus, DSI_ACTIVE_V, va_start | (va_end << 16u));
    reg_write(bus, DSI_TOTAL, (t->h_total - 1u) |
                              ((t->v_total - 1u) << 16u));
    reg_write(bus, DSI_ACTIVE_HSYNC, t->h_pulse << 16u);
    reg_write(bus, DSI_ACTIVE_VSYNC_HPOS, 0u);
    reg_write(bus, DSI_ACTIVE_VSYNC_VPOS, t->v_pulse << 16u);
    reg_write(bus, DSI_TRIG_CTRL, DSI_TRIG_CTRL_SW_DMA);
    reg_write(bus, DSI_LANE_SWAP_CTRL, swap);
    reg_write(bus, DSI_ERR_INT_MASK0, DSI_ERR_INT_MASK_DEFAULT);
    reg_write(bus, DSI_INTR_CTRL, DSI_VIDEO_DONE_RAW | DSI_ERROR_RAW);
    reg_write(bus, DSI_CLK_CTRL, DSI_CLK_CTRL_ENABLE);
    ctrl = lane_bits(mode->lane_count) | DSI_CTRL_CLK_EN | DSI_CTRL_VID_MODE_EN;
    reg_write(bus, DSI_CTRL, ctrl);
    return 0;
}

int vqeaf_sm6150_dsi_enable(const vqeaf_sm6150_dsi_bus_t *bus)
{
    uint32_t ctrl, intr;

    if (!bus_io_valid(bus)) return -1;
    ctrl = reg_read(bus, DSI_CTRL);
    if ((ctrl & (DSI_CTRL_ENABLE | DSI_CTRL_VID_MODE_EN)) !=
        (DSI_CTRL_ENABLE | DSI_CTRL_VID_MODE_EN)) {
        reg_write(bus, DSI_CTRL, ctrl | DSI_CTRL_VID_MODE_EN | DSI_CTRL_ENABLE);
    }
    intr = reg_read(bus, DSI_INTR_CTRL);
    reg_write(bus, DSI_INTR_CTRL, intr | DSI_VIDEO_DONE_MASK | DSI_ERROR_MASK);
    return 0;
}

int vqeaf_sm6150_dsi_wait_video_done(const vqeaf_sm6150_dsi_bus_t *bus)
{
    uint32_t i, ctrl, intr;

    if (!bus_io_valid(bus)) return -1;
    ctrl = reg_read(bus, DSI_CTRL);
    if ((ctrl & (DSI_CTRL_ENABLE | DSI_CTRL_VID_MODE_EN)) !=
        (DSI_CTRL_ENABLE | DSI_CTRL_VID_MODE_EN))
        return -1;
    for (i = 0u; i < bus->poll_limit; ++i) {
        intr = reg_read(bus, DSI_INTR_CTRL);
        if ((intr & DSI_ERROR_RAW) != 0u) {
            reg_write(bus, DSI_INTR_CTRL, DSI_ERROR_RAW);
            return -3;
        }
        if ((intr & DSI_VIDEO_DONE_RAW) != 0u) {
            reg_write(bus, DSI_INTR_CTRL, DSI_VIDEO_DONE_RAW);
            return 0;
        }
    }
    return -4;
}

int vqeaf_sm6150_dsi_disable(const vqeaf_sm6150_dsi_bus_t *bus)
{
    uint32_t ctrl, intr;

    if (!bus_io_valid(bus)) return -1;
    ctrl = reg_read(bus, DSI_CTRL);
    reg_write(bus, DSI_CTRL, ctrl & ~DSI_CTRL_MODE_MASK);
    intr = reg_read(bus, DSI_INTR_CTRL);
    reg_write(bus, DSI_INTR_CTRL, intr & ~(DSI_VIDEO_DONE_MASK | DSI_ERROR_MASK));
    return 0;
}
