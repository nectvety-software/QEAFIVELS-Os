#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "vqeaf/sm6150_dsi.h"

#define BUS_CTRL 0x004u
#define BUS_VID_CFG0 0x010u
#define BUS_VID_CFG1 0x020u
#define BUS_ACTIVE_H 0x024u
#define BUS_ACTIVE_V 0x028u
#define BUS_TOTAL 0x02cu
#define BUS_ACTIVE_HSYNC 0x030u
#define BUS_ACTIVE_VSYNC_HPOS 0x034u
#define BUS_ACTIVE_VSYNC_VPOS 0x038u
#define BUS_TRIG_CTRL 0x084u
#define BUS_LANE_SWAP_CTRL 0x0b0u
#define BUS_ERR_INT_MASK0 0x10cu
#define BUS_INTR_CTRL 0x110u
#define BUS_RESET 0x118u
#define BUS_CLK_CTRL 0x11cu
#define RAW_VERSION 0x1f0u
#define SHIFTED_VERSION 0x1f4u

#define VIDEO_DONE_RAW 0x00010000u
#define VIDEO_DONE_MASK 0x00020000u
#define ERROR_RAW 0x01000000u
#define ERROR_MASK 0x02000000u

typedef struct {
    uint32_t regs[0x200];
    uint32_t read_offsets[32];
    uint32_t read_count;
    uint32_t write_count;
    uint32_t delay_count;
    uint32_t delay_us;
    uint32_t intr_polls;
    uint32_t done_after;
} dsi_fake_t;

static dsi_fake_t fake;
static uint32_t fake_read(void *context, uint32_t offset);
static void fake_write(void *context, uint32_t offset, uint32_t value);
static void fake_delay(void *context, uint32_t microseconds);
static const vqeaf_sm6150_dsi_bus_t bus = {
    fake_read, fake_write, fake_delay, &fake, 8u
};

static uint32_t fake_read(void *context, uint32_t offset)
{
    dsi_fake_t *f = (dsi_fake_t *)context;
    assert(offset < sizeof(f->regs) / sizeof(f->regs[0]));
    assert(f->read_count < sizeof(f->read_offsets) / sizeof(f->read_offsets[0]));
    f->read_offsets[f->read_count++] = offset;
    if (offset == BUS_INTR_CTRL && f->done_after != 0u &&
        ++f->intr_polls >= f->done_after)
        f->regs[offset] |= VIDEO_DONE_RAW;
    return f->regs[offset];
}

static void fake_write(void *context, uint32_t offset, uint32_t value)
{
    dsi_fake_t *f = (dsi_fake_t *)context;
    assert(offset < sizeof(f->regs) / sizeof(f->regs[0]));
    ++f->write_count;
    if (offset == BUS_INTR_CTRL) {
        if ((value & (VIDEO_DONE_RAW | ERROR_RAW)) != 0u &&
            (value & (VIDEO_DONE_MASK | ERROR_MASK)) == 0u) {
            f->regs[offset] &= ~(value & (VIDEO_DONE_RAW | ERROR_RAW));
        } else {
            f->regs[offset] = (f->regs[offset] &
                               ~(VIDEO_DONE_MASK | ERROR_MASK)) |
                              (value & (VIDEO_DONE_MASK | ERROR_MASK));
            f->regs[offset] &= ~(value & (VIDEO_DONE_RAW | ERROR_RAW));
        }
    } else {
        f->regs[offset] = value;
    }
}

static void fake_delay(void *context, uint32_t microseconds)
{
    dsi_fake_t *f = (dsi_fake_t *)context;
    ++f->delay_count;
    f->delay_us = microseconds;
}

static void reset_fake(uint32_t version)
{
    memset(&fake, 0, sizeof(fake));
    fake.regs[0] = version;
    fake.regs[SHIFTED_VERSION] = 0x02000000u;
}

static void init_mode(vqeaf_sm6150_dsi_mode_t *mode)
{
    memset(mode, 0, sizeof(*mode));
    mode->timing = *vqeaf_sm6150_panel_reference_mode();
    mode->lane_count = 4u;
    mode->logical_to_physical[0] = 1u;
    mode->logical_to_physical[1] = 2u;
    mode->logical_to_physical[2] = 3u;
    mode->logical_to_physical[3] = 0u;
}

static void test_probe(void)
{
    uint32_t version = 0u;

    reset_fake(VQEAF_SM6150_DSI_6G_V2_3_1);
    assert(vqeaf_sm6150_dsi_probe(&bus, &version) == 0);
    assert(version == VQEAF_SM6150_DSI_6G_V2_3_1);
    assert(fake.read_count == 3u);
    assert(fake.read_offsets[0] == RAW_VERSION);
    assert(fake.read_offsets[1] == SHIFTED_VERSION);
    assert(fake.read_offsets[2] == 0u);

    reset_fake(0x20020001u);
    assert(vqeaf_sm6150_dsi_probe(&bus, &version) == -2);
    assert(version == 0x20020001u);

    reset_fake(VQEAF_SM6150_DSI_6G_V2_3_1);
    fake.regs[RAW_VERSION] = 0x01000000u;
    assert(vqeaf_sm6150_dsi_probe(&bus, &version) == -2);
    assert(version == 0x01000000u && fake.read_count == 1u);

    assert(vqeaf_sm6150_dsi_probe(NULL, &version) == -1);
    assert(vqeaf_sm6150_dsi_probe(&bus, NULL) == -1);
}

static void test_configure(void)
{
    vqeaf_sm6150_dsi_mode_t mode;

    reset_fake(VQEAF_SM6150_DSI_6G_V2_3_1);
    fake.regs[BUS_CTRL] = 0x13u;
    init_mode(&mode);
    assert(vqeaf_sm6150_dsi_configure(&bus, &mode) == 0);
    assert(fake.delay_count == 1u && fake.delay_us == 20000u);
    assert(fake.regs[BUS_VID_CFG0] == 0x230u);
    assert(fake.regs[BUS_VID_CFG1] == 0u);
    assert(fake.regs[BUS_ACTIVE_H] == (122u | (1202u << 16u)));
    assert(fake.regs[BUS_ACTIVE_V] == (11u | (2351u << 16u)));
    assert(fake.regs[BUS_TOTAL] == (1291u | (2363u << 16u)));
    assert(fake.regs[BUS_ACTIVE_HSYNC] == (2u << 16u));
    assert(fake.regs[BUS_ACTIVE_VSYNC_HPOS] == 0u);
    assert(fake.regs[BUS_ACTIVE_VSYNC_VPOS] == (3u << 16u));
    assert(fake.regs[BUS_TRIG_CTRL] == 0x80001004u);
    assert(fake.regs[BUS_LANE_SWAP_CTRL] == 3u);
    assert(fake.regs[BUS_ERR_INT_MASK0] == 0x13ff3fe0u);
    assert(fake.regs[BUS_RESET] == 0u);
    assert(fake.regs[BUS_CLK_CTRL] == 0x23fu);
    assert(fake.regs[BUS_CTRL] == 0x1f2u);

    {
        uint32_t writes = fake.write_count;
        mode.timing.h_total--;
        assert(vqeaf_sm6150_dsi_configure(&bus, &mode) == -1);
        assert(fake.write_count == writes);
    }
    init_mode(&mode);
    mode.logical_to_physical[1] = 1u;
    {
        uint32_t writes = fake.write_count;
        assert(vqeaf_sm6150_dsi_configure(&bus, &mode) == -1);
        assert(fake.write_count == writes);
    }
}

static void test_enable_wait_disable(void)
{
    vqeaf_sm6150_dsi_mode_t mode;

    reset_fake(VQEAF_SM6150_DSI_6G_V2_3_1);
    init_mode(&mode);
    assert(vqeaf_sm6150_dsi_configure(&bus, &mode) == 0);
    assert(vqeaf_sm6150_dsi_enable(&bus) == 0);
    assert(fake.regs[BUS_CTRL] == 0x1f3u);
    assert((fake.regs[BUS_INTR_CTRL] &
            (VIDEO_DONE_MASK | ERROR_MASK)) ==
           (VIDEO_DONE_MASK | ERROR_MASK));

    fake.done_after = 3u;
    assert(vqeaf_sm6150_dsi_wait_video_done(&bus) == 0);
    assert((fake.regs[BUS_INTR_CTRL] & VIDEO_DONE_RAW) == 0u);
    assert((fake.regs[BUS_INTR_CTRL] & VIDEO_DONE_MASK) != 0u);

    fake.done_after = 0u;
    assert(vqeaf_sm6150_dsi_wait_video_done(&bus) == -4);
    fake.regs[BUS_INTR_CTRL] |= ERROR_RAW;
    assert(vqeaf_sm6150_dsi_wait_video_done(&bus) == -3);
    assert((fake.regs[BUS_INTR_CTRL] & ERROR_RAW) == 0u);

    assert(vqeaf_sm6150_dsi_disable(&bus) == 0);
    assert(fake.regs[BUS_CTRL] == 0x1f0u);
    assert((fake.regs[BUS_INTR_CTRL] &
            (VIDEO_DONE_MASK | ERROR_MASK)) == 0u);

    fake.regs[BUS_CTRL] = 0u;
    assert(vqeaf_sm6150_dsi_wait_video_done(&bus) == -1);
}

int main(void)
{
    test_probe();
    test_configure();
    test_enable_wait_disable();
    puts("SM6150 DSI 6G-v2 fake-MMIO tests: PASS");
    return 0;
}
