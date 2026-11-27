/* SPDX-License-Identifier: MIT */
/* tests/mre_drives_smoke.c - host gate for the drives/ contracts.
 *
 * Every drive under drives/ is exercised here against fake registers, fake
 * buses and fake frames. The point is not to prove a register value: it is to
 * prove the fail-closed rules - bounds, read-back verification, permission
 * gates, rollback, and that a panel without a measured ID never gets its init
 * table authorized.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mre_dsi_host_sm6150.h"
#include "mre_dsi_packet.h"
#include "mre_dsi_reg_backend.h"
#include "host_mre_gic_stub.h"
#include "mre_fdt.h"
#include "mre_geni_i2c.h"
#include "mre_gic.h"
#include "mre_panel_nt36672a.h"
#include "mre_pmic_arb.h"
#include "mre_pmic_regulator.h"
#include "mre_reg_io.h"
#include "mre_sm6150_dispcc.h"
#include "mre_spmi.h"
#include "mre_touch.h"

/* ---------------------------------------------------------------- fake MMIO */

#define FAKE_WORDS 0x2400u

typedef struct {
    uint32_t regs[FAKE_WORDS];
    uint32_t reads, writes, delays;
    uint32_t drop_offset;   /* writes here are silently lost */
    uint32_t stall_offset;  /* reads here keep returning the stored value */
    uint32_t rx_word;
    uint32_t tx_word;   /* last word pushed into the GENI TX FIFO */
    uint64_t now;
} fake_reg_t;

static fake_reg_t g_regs;

static void regs_reset(void)
{
    memset(&g_regs, 0, sizeof(g_regs));
    g_regs.drop_offset = 0xffffffffu;
    g_regs.stall_offset = 0xffffffffu;
}

static mre_status_t reg_read(void *context, uint32_t offset, uint32_t *value)
{
    fake_reg_t *f = (fake_reg_t *)context;
    if (offset >= FAKE_WORDS) return MRE_E_INVALID;
    f->reads++;
    f->now++;
    if (offset == 0x610u) {          /* GENI master IRQ status: done, no error */
        *value = 1u | (1u << 30) | (1u << 26) | (1u << 27);
    } else if (offset == 0x800u) {   /* TX FIFO space */
        *value = 0xffffffffu;
    } else if (offset == 0x804u) {   /* RX FIFO words */
        *value = 8u;
    } else if (offset == 0x780u) {   /* RX FIFO data */
        *value = f->rx_word;
    } else {
        *value = f->regs[offset];
    }
    return MRE_OK;
}

static mre_status_t reg_write(void *context, uint32_t offset, uint32_t value)
{
    fake_reg_t *f = (fake_reg_t *)context;
    if (offset >= FAKE_WORDS) return MRE_E_INVALID;
    f->writes++;
    if (offset == f->drop_offset) return MRE_OK;
    if (offset == 0x700u) {                       /* GENI TX FIFO, write-only */
        f->tx_word = value;
        return MRE_OK;
    }
    /* DISP_CC RCG_UPDATE is self-clearing once the bank is applied. */
    if (offset == 0x2170u) value &= ~1u;
    f->regs[offset] = value;
    return MRE_OK;
}

static void reg_delay(void *context, uint32_t microseconds)
{
    fake_reg_t *f = (fake_reg_t *)context;
    f->delays++;
    f->now += microseconds;
}

static uint64_t reg_time(void *context)
{
    fake_reg_t *f = (fake_reg_t *)context;
    return f->now;
}

static const mre_reg_io_ops_t g_ops = {
    reg_read, reg_write, reg_delay, reg_time
};

static mre_reg_io_t window(uint32_t size)
{
    mre_reg_io_t io;
    io.ops = &g_ops;
    io.context = &g_regs;
    io.size = size;
    return io;
}

/* --------------------------------------------------------- register window */

static void test_reg_io(void)
{
    mre_reg_io_t io = window(0x100u);
    uint32_t value = 0u;

    regs_reset();
    assert(mre_reg_read32(NULL, 0u, &value) == MRE_E_INVALID);
    assert(mre_reg_read32(&io, 0u, NULL) == MRE_E_INVALID);
    assert(mre_reg_read32(&io, 0x100u, &value) == MRE_E_INVALID);
    assert(mre_reg_write32(&io, 0x100u, 7u) == MRE_E_INVALID);
    assert(mre_reg_write32(&io, 0xf8u, 7u) == MRE_OK);
    assert(mre_reg_write_verify32(&io, 0xf8u, 0u, 0u) == MRE_E_INVALID);

    assert(mre_reg_write_verify32(&io, 0x10u, 0xabcdu, 0xffffu) == MRE_OK);
    assert(g_regs.regs[0x10u] == 0xabcdu);

    /* A write the block does not keep is an error, not a success. */
    g_regs.drop_offset = 0x14u;
    assert(mre_reg_write_verify32(&io, 0x14u, 0x55u, 0xffu) == MRE_E_IO);

    /* Poll: a bit that never sets is BUSY, and it stops at max_polls. */
    g_regs.regs[0x20u] = 0u;
    assert(mre_reg_poll32(&io, 0x20u, 1u, 1u, 10u, 4u, &value) == MRE_E_BUSY);
    assert(value == 0u);
    assert(mre_reg_poll32(&io, 0x20u, 1u, 1u, 0u, 0u, NULL) == MRE_E_INVALID);
    g_regs.regs[0x20u] = 1u;
    assert(mre_reg_poll32(&io, 0x20u, 1u, 1u, 10u, 4u, &value) == MRE_OK);
    assert(value == 1u);
}

/* ------------------------------------------------------------ DSI packets */

static void test_dsi_packets(void)
{
    const mre_panel_descriptor_t *panel = mre_panel_nt36672a_shenchao();
    mre_dsi_timing_t timing;
    uint8_t short_packet[4];
    uint8_t payload[3] = {0x68u, 0x03u, 0x04u};
    uint8_t long_packet[MRE_DSI_LONG_HEADER_BYTES + 3u];
    uint32_t words[2];
    size_t count = 0u;
    mre_dsi_cmd_entry_t entry;

    assert(panel != NULL);
    assert(mre_panel_mode_validate(&panel->mode) == MRE_OK);
    assert(mre_dsi_timing_compute(&panel->mode, &timing) == MRE_OK);
    assert(timing.h_total == 1292u);
    assert(timing.v_total == 2364u);
    assert(timing.pixel_clock_hz == 1292u * 2364u * 60u);
    assert(timing.bit_clock_hz == timing.pixel_clock_hz * 24u / 4u);
    assert(timing.byte_clock_hz == timing.bit_clock_hz / 8u);
    assert(timing.pclk_to_byte_div == 12u);

    {
        mre_panel_mode_t bad = panel->mode;
        bad.lanes = 5u;
        assert(mre_panel_mode_validate(&bad) == MRE_E_INVALID);
        bad = panel->mode;
        bad.bpp = 16u;
        assert(mre_panel_mode_validate(&bad) == MRE_E_INVALID);
        bad = panel->mode;
        bad.framerate = 0u;
        assert(mre_panel_mode_validate(&bad) == MRE_E_INVALID);
        assert(mre_panel_mode_validate(NULL) == MRE_E_INVALID);
    }

    assert(mre_dsi_encode_short_write(MRE_DSI_DT_DCS_SHORT_WRITE_1, 0xffu,
                                      0x23u, short_packet) == MRE_OK);
    assert(short_packet[0] == 0x15u && short_packet[1] == 0xffu &&
           short_packet[2] == 0x23u && short_packet[3] == 0u);
    assert(mre_dsi_encode_short_write(0x39u, 0u, 0u, short_packet) ==
           MRE_E_INVALID);

    assert(mre_dsi_encode_long_write(MRE_DSI_DT_DCS_LONG_WRITE, payload, 3u,
                                     long_packet, sizeof(long_packet)) ==
           MRE_OK);
    assert(long_packet[0] == 0x39u && long_packet[1] == 3u &&
           long_packet[2] == 0u && long_packet[3] == 0u);
    assert(long_packet[4] == 0x68u && long_packet[6] == 0x04u);
    assert(mre_dsi_encode_long_write(0x39u, payload, 3u, long_packet, 6u) ==
           MRE_E_NOMEM);

    assert(mre_dsi_pack_words(long_packet, 7u, words, 2u, &count) == MRE_OK);
    assert(count == 2u);
    assert(words[0] == 0x00000339u);
    assert(words[1] == 0x00040368u);
    {
        uint32_t one[1];
        assert(mre_dsi_pack_words(long_packet, 5u, one, 1u, &count) ==
               MRE_E_NOMEM);
    }

    memset(&entry, 0, sizeof(entry));
    entry.di = MRE_DSI_DT_DCS_SHORT_WRITE_1;
    entry.payload_length = 2u;
    entry.payload[0] = 0x53u;
    entry.payload[1] = 0x2cu;
    assert(mre_dsi_encode_entry(&entry, short_packet, sizeof(short_packet),
                                &count) == MRE_OK);
    assert(count == 4u && short_packet[1] == 0x53u);
    entry.payload_length = 1u;
    assert(mre_dsi_encode_entry(&entry, short_packet, sizeof(short_packet),
                                &count) == MRE_E_INVALID);
    assert(mre_dsi_command_table_validate(panel->on_sequence,
                                          panel->on_count) == MRE_OK);
    assert(mre_dsi_command_table_validate(NULL, 1u) == MRE_E_INVALID);
}

/* ------------------------------------------------------- panel and ID gate */

static int g_dcs_status, g_dcs_calls;
static uint8_t g_dcs_id[2];

mre_status_t mre_dsi_host_sm6150_dcs_read(
    const mre_dsi_dcs_read_config_t *config, uint8_t dcs_command, uint8_t *out,
    uint32_t out_length)
{
    (void)config;
    (void)dcs_command;
    g_dcs_calls++;
    if (g_dcs_status != MRE_OK) return (mre_status_t)g_dcs_status;
    if (out == NULL || out_length != sizeof(g_dcs_id)) return MRE_E_INVALID;
    memcpy(out, g_dcs_id, sizeof(g_dcs_id));
    return MRE_OK;
}

static void test_panel_identity(void)
{
    const mre_panel_descriptor_t *nt = mre_panel_nt36672a_shenchao();
    mre_panel_descriptor_t panel;
    mre_dsi_dcs_read_config_t config;
    uint8_t read_id[2] = {0xabu, 0xcdu};
    size_t count = 99u;

    assert(mre_panel_descriptor_validate(nt) == MRE_OK);
    /* The table records the DT compatible in `name` and the vendor
     * panel-name string in `compatible`, and the lookup matches the latter. */
    assert(strcmp(nt->name, "novatek,nt36672a-shenchao") == 0);
    assert(mre_panel_lookup_compatible(nt->compatible) == nt);
    assert(mre_panel_lookup_compatible("novatek,unknown") == NULL);

    /* No ID command in the vendor tree: nothing may authorize the table. */
    assert(mre_panel_id_defined(nt) == 0);
    assert(mre_panel_initialization(nt, read_id, 2u, &count) == NULL);
    assert(count == 0u);

    {
        mre_panel_descriptor_t bad = *nt;
        bad.backlight_max_level = bad.backlight_min_level;
        assert(mre_panel_descriptor_validate(&bad) == MRE_E_INVALID);
        bad = *nt;
        bad.id.dcs_command = 0x04u;   /* half-filled ID description */
        assert(mre_panel_descriptor_validate(&bad) == MRE_E_INVALID);
    }

    /* A descriptor that does carry an ID gate: only a masked match authorizes. */
    panel = *nt;
    panel.id.dcs_command = 0x04u;
    panel.id.length = 2u;
    panel.id.expected[0] = 0xabu;
    panel.id.expected[1] = 0xc0u;
    panel.id.match_mask[0] = 0xffu;
    panel.id.match_mask[1] = 0xf0u;
    panel.id.evidence = "host-test id capture";
    assert(mre_panel_descriptor_validate(&panel) == MRE_OK);
    assert(mre_panel_id_defined(&panel) == 1);
    assert(mre_panel_id_matches(&panel, read_id, 2u) == 1);
    assert(mre_panel_id_matches(&panel, read_id, 1u) == 0);
    assert(mre_panel_id_equal(panel.id.expected, panel.id.match_mask, read_id,
                              2u) == 1);

    memset(&config, 0, sizeof(config));
    g_dcs_calls = 0;
    g_dcs_status = MRE_OK;
    g_dcs_id[0] = 0xabu;
    g_dcs_id[1] = 0xcdu;
    assert(mre_panel_identify_and_select(&panel, &config, NULL, &count) ==
           panel.on_sequence);
    assert(count == panel.on_count);
    assert(g_dcs_calls == 1);

    g_dcs_id[1] = 0x00u;   /* different panel byte */
    assert(mre_panel_identify_and_select(&panel, &config, NULL, &count) == NULL);
    assert(count == 0u);

    g_dcs_status = MRE_E_IO;
    assert(mre_panel_identify_and_select(&panel, &config, NULL, &count) == NULL);
    assert(mre_panel_identify_and_select(nt, &config, NULL, &count) == NULL);
}

/* ---------------------------------------------------------------- DISP_CC */

static mre_status_t parent_rate(void *context, mre_dispcc_parent_id_t parent,
                                uint64_t *rate_hz)
{
    (void)context;
    switch (parent) {
    case MRE_DISPCC_PARENT_GPLL0:
        *rate_hz = 600000000u;
        return MRE_OK;
    case MRE_DISPCC_PARENT_BI_TCXO:
        *rate_hz = 19200000u;
        return MRE_OK;
    default:
        return MRE_E_UNSUPPORTED;
    }
}

static void test_dispcc(void)
{
    mre_sm6150_dispcc_t cc;
    mre_sm6150_dispcc_policy_t policy = {1u, 1u, 0u};
    mre_dispcc_clock_snapshot_t snapshot;
    mre_reg_io_t regs = window(0x2200u);

    regs_reset();
    assert(mre_sm6150_dispcc_init(NULL, &regs, parent_rate, NULL, &policy) ==
           MRE_E_INVALID);
    assert(mre_sm6150_dispcc_init(&cc, &regs, parent_rate, NULL, NULL) ==
           MRE_E_INVALID);
    {
        mre_reg_io_t small = window(0x1000u);
        assert(mre_sm6150_dispcc_init(&cc, &small, parent_rate, NULL,
                                      &policy) == MRE_E_INVALID);
    }
    assert(mre_sm6150_dispcc_init(&cc, &regs, parent_rate, NULL, &policy) ==
           MRE_OK);

    /* AHB_SRC: select 4 (GPLL0), no divider -> 600 MHz * 2. */
    g_regs.regs[0x2174u] = (4u << 8) | 0u;
    g_regs.regs[0x2048u] = 1u;
    assert(mre_sm6150_dispcc_read_clock(&cc, MRE_DISPCC_MDSS_AHB_SRC,
                                        &snapshot) == MRE_OK);
    assert(snapshot.parent_select == 4u);
    assert(snapshot.hid_div == 0u);
    assert(snapshot.branch_enabled == 1u);
    assert(snapshot.rate_hz == 1200000000u);

    /* Select 5 is not a parent of AHB_SRC and must not be guessed. */
    g_regs.regs[0x2174u] = (5u << 8);
    assert(mre_sm6150_dispcc_read_clock(&cc, MRE_DISPCC_MDSS_AHB_SRC,
                                        &snapshot) == MRE_E_UNSUPPORTED);

    /* No write authorization yet: the clock tree is not touched. */
    g_regs.regs[0x2174u] = (4u << 8);
    {
        uint32_t writes = g_regs.writes;
        assert(mre_sm6150_dispcc_set_rcg(&cc, MRE_DISPCC_MDSS_AHB_SRC, 4u, 2u,
                                         400000000u, 100u, 8u) ==
               MRE_E_PERMISSION);
        assert(g_regs.writes == writes);
    }

    cc.policy.write_authorized = 1u;
    assert(mre_sm6150_dispcc_set_rcg(&cc, MRE_DISPCC_MDSS_AHB_SRC, 4u, 2u,
                                     399999999u, 100u, 8u) == MRE_E_INVALID);
    assert(mre_sm6150_dispcc_set_rcg(&cc, MRE_DISPCC_MDSS_AHB_SRC, 4u, 2u,
                                     400000000u, 100u, 8u) == MRE_OK);
    assert(((g_regs.regs[0x2174u] >> 8) & 7u) == 4u);
    assert((g_regs.regs[0x2174u] & 0x1fu) == 2u);

    /* Branch enable must land and clear the halt bit. */
    g_regs.regs[0x2048u] = 0x80000000u;
    assert(mre_sm6150_dispcc_set_branch(&cc, 0x2048u, 1u, 100u, 8u) ==
           MRE_E_BUSY);
    g_regs.regs[0x2048u] = 0u;
    assert(mre_sm6150_dispcc_set_branch(&cc, 0x2048u, 1u, 100u, 8u) == MRE_OK);
    assert((g_regs.regs[0x2048u] & 1u) == 1u);
    assert(mre_sm6150_dispcc_set_branch(&cc, 0x1234u, 1u, 100u, 8u) ==
           MRE_E_PERMISSION);
}

/* -------------------------------------------------------- DSI reg backend */

static void test_dsi_reg_backend(void)
{
    mre_dsi_reg_backend_t backend;
    mre_reg_io_t host = window(0x2000u);
    mre_reg_io_t phy = window(0x1000u);
    mre_dsi_reg_op_t ops[2];

    regs_reset();
    assert(mre_dsi_reg_backend_init(NULL, &host, &phy, 10u, 4u) ==
           MRE_E_INVALID);
    assert(mre_dsi_reg_backend_init(&backend, &host, &phy, 0u, 4u) ==
           MRE_E_INVALID);
    assert(mre_dsi_reg_backend_init(&backend, &host, &phy, 10u, 4u) == MRE_OK);

    ops[0].offset = 0x40u;
    ops[0].value = 0x11u;
    ops[0].verify_mask = 0xffu;
    ops[0].delay_after_us = 0u;
    ops[1].offset = 0x44u;
    ops[1].value = 0x22u;
    ops[1].verify_mask = 0xffu;
    ops[1].delay_after_us = 5u;

    assert(mre_dsi_reg_sequence_apply(&backend, &host, ops, 2u) ==
           MRE_E_PERMISSION);

    backend.write_authorized = 1u;
    assert(mre_dsi_reg_sequence_apply(&backend, &host, ops, 2u) == MRE_OK);
    assert(g_regs.regs[0x40u] == 0x11u);
    assert(g_regs.regs[0x44u] == 0x22u);
    assert(g_regs.delays == 1u);

    ops[0].offset = 0x42u;   /* unaligned */
    assert(mre_dsi_reg_sequence_apply(&backend, &host, ops, 1u) ==
           MRE_E_INVALID);

    /* Second op is lost: the first one is rolled back to its old value. */
    g_regs.regs[0x40u] = 0x01u;
    ops[0].offset = 0x40u;
    ops[0].value = 0x33u;
    ops[1].offset = 0x44u;
    ops[1].value = 0x44u;
    g_regs.drop_offset = 0x44u;
    assert(mre_dsi_reg_sequence_apply(&backend, &host, ops, 2u) == MRE_E_IO);
    assert(g_regs.regs[0x40u] == 0x01u);
}

/* --------------------------------------------------------------- GENI I2C */

/* The GENI driver takes its own read/write shape, so give it thin wrappers. */
static uint32_t geni_read(void *context, uint32_t offset)
{
    uint32_t value = 0u;
    reg_read(context, offset, &value);
    return value;
}

static void geni_write(void *context, uint32_t offset, uint32_t value)
{
    reg_write(context, offset, value);
}

static void test_geni_i2c_bus(void)
{
    mre_geni_i2c_bus_t bus;
    uint8_t tx[4] = {0x10u, 0x20u, 0x30u, 0x40u};
    uint8_t rx[4] = {0u};

    regs_reset();
    memset(&bus, 0, sizeof(bus));
    assert(mre_geni_i2c_attach(NULL) == MRE_E_INVALID);
    assert(mre_geni_i2c_attach(&bus) == MRE_E_INVALID);
    bus.read32 = geni_read;
    bus.write32 = geni_write;
    bus.context = &g_regs;

    assert(mre_geni_i2c_attach(&bus) == MRE_E_PERMISSION);
    bus.resources_verified = 1u;
    assert(mre_geni_i2c_attach(&bus) == MRE_E_PERMISSION);
    bus.fifo_mode_verified = 1u;

    g_regs.regs[0x064u] = 1u;    /* FIFO disabled: DMA-only block */
    assert(mre_geni_i2c_attach(&bus) == MRE_E_UNSUPPORTED);
    g_regs.regs[0x064u] = 0u;
    g_regs.regs[0x040u] = 1u;    /* a command is already active */
    assert(mre_geni_i2c_attach(&bus) == MRE_E_BUSY);
    g_regs.regs[0x040u] = 0u;
    assert(mre_geni_i2c_attach(&bus) == MRE_OK);
    assert(bus.attached == 1u);
    assert(bus.poll_limit != 0u);

    assert(mre_geni_i2c_write(&bus, 0x80u, tx, 1u) == MRE_E_INVALID);
    assert(mre_geni_i2c_write(&bus, 0x10u, tx, 2u) == MRE_OK);
    assert(g_regs.regs[0x26cu] == 2u);
    assert(g_regs.tx_word == 0x2010u);

    g_regs.rx_word = 0xd0c0b0a0u;
    assert(mre_geni_i2c_read(&bus, 0x10u, rx, 4u) == MRE_OK);
    assert(rx[0] == 0xa0u && rx[1] == 0xb0u && rx[2] == 0xc0u && rx[3] == 0xd0u);

    assert(mre_geni_i2c_write_read(&bus, 0x10u, NULL, 2u, rx, 1u) ==
           MRE_E_INVALID);
    assert(mre_geni_i2c_write_read(&bus, 0x10u, tx, 1u, rx, 1u) == MRE_OK);
}

/* ------------------------------------------------------ SPMI and regulator */

typedef struct {
    uint8_t regs[256];
    uint32_t writes;
    int drop_writes;
} fake_spmi_t;

static fake_spmi_t g_spmi;

static mre_status_t spmi_read8(void *context, uint8_t sid, uint16_t address,
                               uint8_t *value, uint32_t timeout_us)
{
    fake_spmi_t *f = (fake_spmi_t *)context;
    (void)sid;
    if (timeout_us == 0u || value == NULL || address > 0xffu)
        return MRE_E_INVALID;
    *value = f->regs[address];
    return MRE_OK;
}

static mre_status_t spmi_write8(void *context, uint8_t sid, uint16_t address,
                                uint8_t value, uint32_t timeout_us)
{
    fake_spmi_t *f = (fake_spmi_t *)context;
    (void)sid;
    if (timeout_us == 0u || address > 0xffu) return MRE_E_INVALID;
    f->writes++;
    if (!f->drop_writes) f->regs[address] = value;
    return MRE_OK;
}

static const mre_spmi_ops_t g_spmi_ops = {spmi_read8, spmi_write8};

static void test_spmi_regulator(void)
{
    mre_spmi_bus_t bus;
    mre_pmic_regulator_t regulator;
    mre_pmic_regulator_desc_t desc;
    uint32_t enabled = 9u, actual = 0u;

    memset(&g_spmi, 0, sizeof(g_spmi));
    assert(mre_spmi_init(NULL, &g_spmi_ops, &g_spmi, 10u) == MRE_E_INVALID);
    assert(mre_spmi_init(&bus, NULL, &g_spmi, 10u) == MRE_E_INVALID);
    assert(mre_spmi_init(&bus, &g_spmi_ops, &g_spmi, 0u) == MRE_E_INVALID);
    assert(mre_spmi_init(&bus, &g_spmi_ops, &g_spmi, 10u) == MRE_OK);
    assert(bus.verified == 0u);

    assert(mre_spmi_read8(&bus, 0x10u, 0u, &(uint8_t){0u}) == MRE_E_INVALID);
    assert(mre_spmi_write8(&bus, 0x0u, 0u, 1u) == MRE_E_PERMISSION);

    memset(&desc, 0, sizeof(desc));
    desc.name = "vreg_l1a";
    desc.sid = 0u;
    desc.enable_address = 0x40u;
    desc.enable_mask = 0x80u;
    desc.enable_value = 0x80u;
    desc.voltage_address = 0x41u;
    desc.voltage_mask = 0x7fu;
    desc.voltage_shift = 0u;
    desc.step_uv = 8000u;
    desc.min_uv = 375000u;
    desc.selector_max = 0x7fu;
    desc.evidence_verified = 1u;

    assert(mre_pmic_regulator_init(NULL, &bus, &desc) == MRE_E_INVALID);
    assert(mre_pmic_regulator_init(&regulator, &bus, NULL) == MRE_E_INVALID);
    {
        mre_pmic_regulator_desc_t bad = desc;
        bad.enable_mask = 0u;
        assert(mre_pmic_regulator_init(&regulator, &bus, &bad) ==
               MRE_E_INVALID);
        bad = desc;
        bad.enable_value = 0x40u;
        assert(mre_pmic_regulator_init(&regulator, &bus, &bad) ==
               MRE_E_INVALID);
        bad = desc;
        bad.step_uv = 0u;
        assert(mre_pmic_regulator_init(&regulator, &bus, &bad) ==
               MRE_E_INVALID);
        bad = desc;
        bad.voltage_shift = 8u;
        assert(mre_pmic_regulator_init(&regulator, &bus, &bad) ==
               MRE_E_INVALID);
    }
    assert(mre_pmic_regulator_init(&regulator, &bus, &desc) == MRE_OK);

    g_spmi.regs[0x40u] = 0x80u;
    assert(mre_pmic_regulator_get_enabled(&regulator, &enabled) == MRE_OK);
    assert(enabled == 1u);
    g_spmi.regs[0x40u] = 0x00u;
    assert(mre_pmic_regulator_get_enabled(&regulator, &enabled) == MRE_OK);
    assert(enabled == 0u);

    /* Bus not verified: no write leaves the driver. */
    assert(mre_pmic_regulator_set_enabled(&regulator, 1u) == MRE_E_PERMISSION);
    bus.verified = 1u;
    assert(mre_pmic_regulator_set_enabled(&regulator, 2u) == MRE_E_INVALID);
    assert(mre_pmic_regulator_set_enabled(&regulator, 1u) == MRE_OK);
    assert(g_spmi.regs[0x40u] == 0x80u);
    assert(mre_pmic_regulator_set_enabled(&regulator, 0u) == MRE_OK);
    assert(g_spmi.regs[0x40u] == 0x00u);

    /* A write the PMIC does not keep must roll back, not report success. */
    g_spmi.drop_writes = 1;
    assert(mre_pmic_regulator_set_enabled(&regulator, 1u) == MRE_E_IO);
    g_spmi.drop_writes = 0;

    assert(mre_pmic_regulator_set_voltage(&regulator, 375001u, &actual) ==
           MRE_E_INVALID);
    assert(mre_pmic_regulator_set_voltage(&regulator, 375000u + 8000u,
                                          &actual) == MRE_OK);
    assert(actual == 383000u);
    assert((g_spmi.regs[0x41u] & 0x7fu) == 1u);
    assert(mre_pmic_regulator_set_voltage(&regulator, 0x7fu * 8000u + 375000u +
                                              8000u,
                                          &actual) == MRE_E_INVALID);
}

/* ------------------------------------------------------------- PMIC arbiter */

typedef struct {
    uint32_t version;
    uint16_t apid_ppid;
    uint8_t apid_owner;
    uint8_t rdata;
    uint32_t writes;
    uint64_t now;
} fake_arb_t;

static fake_arb_t g_arb;

static mre_status_t arb_read(void *context, mre_pmic_arb_region_t region,
                             uint32_t offset, uint32_t *value)
{
    fake_arb_t *f = (fake_arb_t *)context;
    *value = 0u;
    switch (region) {
    case MRE_PMIC_ARB_CORE:
        if (offset == 0u) *value = f->version;
        else if (offset >= 0x900u && offset == 0x900u)
            *value = ((uint32_t)f->apid_ppid) << 8;
        return MRE_OK;
    case MRE_PMIC_ARB_CONFIG:
        if (offset >= 0x700u && offset == 0x700u) *value = f->apid_owner;
        return MRE_OK;
    case MRE_PMIC_ARB_OBSERVER:
        /* Observer channels answer at the same inner offsets as a channel. */
        if ((offset & 0xffffu) == 0x08u) *value = 1u;    /* DONE */
        else if ((offset & 0xffffu) == 0x18u) *value = f->rdata;
        return MRE_OK;
    case MRE_PMIC_ARB_CHANNELS:
    default:
        if ((offset & 0xffffu) == 0x08u) *value = 1u;    /* DONE */
        else if ((offset & 0xffffu) == 0x18u) *value = f->rdata;
        return MRE_OK;
    }
}

static mre_status_t arb_write(void *context, mre_pmic_arb_region_t region,
                              uint32_t offset, uint32_t value)
{
    fake_arb_t *f = (fake_arb_t *)context;
    (void)region;
    (void)offset;
    (void)value;
    f->writes++;
    return MRE_OK;
}

static uint64_t arb_time(void *context)
{
    fake_arb_t *f = (fake_arb_t *)context;
    return ++f->now;
}

static void arb_delay(void *context, uint32_t microseconds)
{
    fake_arb_t *f = (fake_arb_t *)context;
    f->now += microseconds;
}

static void arb_lock(void *context) { (void)context; }
static void arb_unlock(void *context) { (void)context; }
static void arb_barrier(void *context) { (void)context; }

static const mre_pmic_arb_io_ops_t g_arb_ops = {
    arb_read, arb_write, arb_time, arb_delay, arb_lock, arb_unlock, arb_barrier
};

static void arb_windows(mre_pmic_arb_window_t windows[4])
{
    windows[MRE_PMIC_ARB_CORE].base = 0x0c000000u;
    windows[MRE_PMIC_ARB_CORE].size = 0x1100u;
    windows[MRE_PMIC_ARB_CHANNELS].base = 0x0c040000u;
    windows[MRE_PMIC_ARB_CHANNELS].size = (MRE_PMIC_ARB_MAX_APIDS - 1u) *
                                              0x10000u + 0x100u;
    windows[MRE_PMIC_ARB_OBSERVER].base = 0x0c240000u;
    windows[MRE_PMIC_ARB_OBSERVER].size = 5u * 0x10000u +
                                          MRE_PMIC_ARB_MAX_APIDS * 0x80u + 0x100u;
    windows[MRE_PMIC_ARB_CONFIG].base = 0x0c2c0000u;
    windows[MRE_PMIC_ARB_CONFIG].size = 0x0f00u;
}

static void test_pmic_arb(void)
{
    mre_pmic_arb_t arb;
    mre_pmic_arb_window_t windows[4];
    mre_spmi_bus_t bus;
    uint8_t byte = 0u;

    memset(&g_arb, 0, sizeof(g_arb));
    arb_windows(windows);
    assert(mre_pmic_arb_parse_fdt(NULL, windows, &(uint8_t){0u},
                                  &(uint8_t){0u}) == MRE_E_INVALID);
    assert(mre_pmic_arb_init(NULL, &g_arb_ops, &g_arb, windows, 0u, 0u, 10u,
                             4u, 1u) == MRE_E_INVALID);
    assert(mre_pmic_arb_init(&arb, &g_arb_ops, &g_arb, windows, 0u, 0u, 10u,
                             4u, 0u) == MRE_E_INVALID);
    assert(mre_pmic_arb_init(&arb, &g_arb_ops, &g_arb, windows, 6u, 0u, 10u,
                             4u, 1u) == MRE_E_INVALID);
    {
        mre_pmic_arb_window_t small[4];
        memcpy(small, windows, sizeof(small));
        small[MRE_PMIC_ARB_CORE].size = 0x100u;
        assert(mre_pmic_arb_init(&arb, &g_arb_ops, &g_arb, small, 0u, 0u, 10u,
                                 4u, 1u) == MRE_E_INVALID);
    }
    assert(mre_pmic_arb_init(&arb, &g_arb_ops, &g_arb, windows, 0u, 0u, 10u,
                             4u, 1u) == MRE_OK);

    /* Version below the v5 layout is refused, not approximated. */
    g_arb.version = 0x00040000u;
    assert(mre_pmic_arb_probe(&arb) == MRE_E_UNSUPPORTED);
    assert(arb.probed == 0u);

    /* v5 with an empty APID map: nothing to arbitrate. */
    g_arb.version = MRE_PMIC_ARB_VERSION_V5_MIN;
    assert(mre_pmic_arb_probe(&arb) == MRE_E_NOT_FOUND);

    /* One APID owned by our execution environment. */
    g_arb.apid_ppid = 0x002u;   /* sid 0, address >> 8 == 2 */
    g_arb.apid_owner = 0u;
    assert(mre_pmic_arb_probe(&arb) == MRE_OK);
    assert(arb.probed == 1u);
    assert(arb.version == MRE_PMIC_ARB_VERSION_V5_MIN);

    assert(mre_pmic_arb_bind_spmi(&arb, &bus, 10u) == MRE_OK);
    assert(mre_spmi_read8(&bus, 0u, 0x0205u, &byte) == MRE_OK);
    assert(g_arb.writes >= 1u);
    assert(mre_spmi_write8(&bus, 0u, 0x0205u, 0x55u) == MRE_E_PERMISSION);
    assert(mre_pmic_arb_authorize_writes(&arb, &bus, 0u, 1u) ==
           MRE_E_PERMISSION);
    assert(mre_pmic_arb_authorize_writes(&arb, &bus, 1u, 1u) == MRE_OK);
    assert(bus.verified == 1u);
    assert(mre_spmi_write8(&bus, 0u, 0x0205u, 0x55u) == MRE_OK);
}

/* ------------------------------------------------------------------ touch */

typedef struct {
    mre_touch_frame_t frame;
    uint32_t calls;
} fake_touch_t;

static fake_touch_t g_touch;

static mre_status_t touch_read(mre_touch_frame_t *frame)
{
    g_touch.calls++;
    *frame = g_touch.frame;
    return MRE_OK;
}

static void test_touch(void)
{
    mre_touch_device_t device;
    mre_touch_ops_t ops = {touch_read};
    mre_touch_transform_t transform = {2000u, 2000u, 1080u, 2340u, 0, 0, 0};
    mre_input_event_t event;
    uint32_t i;

    memset(&g_touch, 0, sizeof(g_touch));
    assert(mre_touch_init(NULL, &ops, &transform) == MRE_E_INVALID);
    assert(mre_touch_init(&device, NULL, &transform) == MRE_E_INVALID);
    {
        mre_touch_transform_t bad = transform;
        bad.raw_max_x = 0u;
        assert(mre_touch_init(&device, &ops, &bad) == MRE_E_INVALID);
        bad = transform;
        bad.panel_height = 0u;
        assert(mre_touch_init(&device, &ops, &bad) == MRE_E_INVALID);
    }
    assert(mre_touch_init(&device, &ops, &transform) == MRE_OK);

    /* IRQ registration goes through the platform GIC, then the handler only
     * marks work pending: nothing reads the controller from interrupt. */
    vqeaf_host_gic_reset();
    assert(mre_touch_register_irq(&device, 291u) == MRE_OK);
    assert(vqeaf_host_gic_irq == 291u && vqeaf_host_gic_enabled == 1);
    assert(vqeaf_host_gic_context == &device);
    vqeaf_host_gic_handler(vqeaf_host_gic_irq, vqeaf_host_gic_context);
    assert(device.irq_pending == 1u);
    device.irq_pending = 0u;

    assert(mre_touch_poll(&device, &event) == MRE_E_NOT_FOUND);
    assert(mre_touch_service(&device, 1u) == MRE_E_NOT_FOUND);
    assert(g_touch.calls == 0u);

    g_touch.frame.count = 2u;
    g_touch.frame.contacts[0].x = 2000u;
    g_touch.frame.contacts[0].y = 2000u;
    g_touch.frame.contacts[0].id = 3u;
    g_touch.frame.contacts[0].down = 1u;
    g_touch.frame.contacts[1].x = 0u;
    g_touch.frame.contacts[1].y = 1000u;
    g_touch.frame.contacts[1].id = 4u;
    g_touch.frame.contacts[1].down = 1u;

    mre_touch_irq(&device);
    assert(mre_touch_service(&device, 42u) == MRE_OK);
    assert(device.timestamp_ns == 42u);
    assert(mre_touch_poll(&device, &event) == MRE_OK);
    assert(event.type == MRE_INPUT_TOUCH);
    assert(event.code == 3);
    assert(event.x == 1079 && event.y == 2339);
    assert(event.timestamp_ns == 42u);
    assert(mre_touch_poll(&device, &event) == MRE_OK);
    assert(event.code == 4 && event.x == 0);
    assert(event.y == (int32_t)(1000u * 2339u / 2000u));
    assert(mre_touch_poll(&device, &event) == MRE_E_NOT_FOUND);

    /* Too many contacts in one frame is rejected, not truncated. */
    g_touch.frame.count = MRE_TOUCH_MAX_CONTACTS + 1u;
    mre_touch_irq(&device);
    assert(mre_touch_service(&device, 43u) == MRE_E_INVALID);
    g_touch.frame.count = 1u;

    /* A full queue counts a drop instead of overwriting an unconsumed event.
     * The ring keeps CAPACITY-1 events, which is what the next loop fills. */
    for (i = 0; i + 1u < MRE_TOUCH_QUEUE_CAPACITY; i++) {
        mre_touch_irq(&device);
        assert(mre_touch_service(&device, 100u + i) == MRE_OK);
    }
    assert(device.dropped == 0u);
    mre_touch_irq(&device);
    assert(mre_touch_service(&device, 999u) == MRE_OK);
    assert(device.dropped == 1u);

    /* swap_xy happens first, then each axis is scaled and flipped. */
    transform.swap_xy = 1;
    transform.flip_x = 1;
    transform.flip_y = 1;
    assert(mre_touch_init(&device, &ops, &transform) == MRE_OK);
    g_touch.frame.count = 1u;
    g_touch.frame.contacts[0].x = 1000u;
    g_touch.frame.contacts[0].y = 0u;
    mre_touch_irq(&device);
    assert(mre_touch_service(&device, 1u) == MRE_OK);
    assert(mre_touch_poll(&device, &event) == MRE_OK);
    assert(event.x == 1079);                       /* was y=0, flipped */
    assert(event.y == 2339 - (1000u * 2339u / 2000u));
}

/* The SPMI PMIC arbiter is described entirely by the live device tree. Run
 * the parser against the in-tree Violet ABL blob when a path is given: the
 * windows and the execution environment then come from the board, not from a
 * constant in this test. */
static void test_pmic_arb_fdt(const char *path)
{
    FILE *file;
    long size;
    uint8_t *blob;
    mre_fdt_t fdt;
    mre_pmic_arb_window_t windows[4];
    uint8_t ee = 0u, channel = 0u;

    if (path == NULL) {
        puts("pmic arb FDT: skipped (no device-tree path)");
        return;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        printf("pmic arb FDT: skipped (%s unreadable)\n", path);
        return;
    }
    assert(fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size > 0);
    assert(fseek(file, 0, SEEK_SET) == 0);
    blob = (uint8_t *)malloc((size_t)size);
    assert(blob != NULL);
    assert(fread(blob, 1u, (size_t)size, file) == (size_t)size);
    fclose(file);

    assert(mre_fdt_open(&fdt, blob, (size_t)size) == MRE_OK);
    assert(mre_pmic_arb_parse_fdt(&fdt, windows, &ee, &channel) == MRE_OK);
    assert(windows[MRE_PMIC_ARB_CORE].base != 0u);
    assert(windows[MRE_PMIC_ARB_CORE].size >= 0x1100u);
    assert(windows[MRE_PMIC_ARB_CONFIG].size >= 0x0f00u);
    assert(windows[MRE_PMIC_ARB_CHANNELS].size >=
           (MRE_PMIC_ARB_MAX_APIDS - 1u) * 0x10000u + 0x20u);
    assert(windows[MRE_PMIC_ARB_OBSERVER].size >= 0x80u);
    assert(ee <= 5u && channel <= 5u);
    printf("pmic arb from %s: core=0x%llx/%u chnls=0x%llx/%u "
           "obsrvr=0x%llx/%u cnfg=0x%llx/%u ee=%u ch=%u\n",
           path, (unsigned long long)windows[MRE_PMIC_ARB_CORE].base,
           windows[MRE_PMIC_ARB_CORE].size,
           (unsigned long long)windows[MRE_PMIC_ARB_CHANNELS].base,
           windows[MRE_PMIC_ARB_CHANNELS].size,
           (unsigned long long)windows[MRE_PMIC_ARB_OBSERVER].base,
           windows[MRE_PMIC_ARB_OBSERVER].size,
           (unsigned long long)windows[MRE_PMIC_ARB_CONFIG].base,
           windows[MRE_PMIC_ARB_CONFIG].size, ee, channel);
    free(blob);
}

int main(int argc, char **argv)
{
    test_reg_io();
    test_dsi_packets();
    test_panel_identity();
    test_dispcc();
    test_dsi_reg_backend();
    test_geni_i2c_bus();
    test_spmi_regulator();
    test_pmic_arb();
    test_pmic_arb_fdt(argc > 1 ? argv[1] : NULL);
    test_touch();
    puts("MRE drives contract tests: PASS");
    return 0;
}
