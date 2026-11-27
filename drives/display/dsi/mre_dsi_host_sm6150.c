#include "mre_dsi_host_sm6150.h"
#include <string.h>
#include "mre_dsi_packet.h"

/* Read-modify-write that verifies exactly the bits it changed. */
static mre_status_t update_checked(const mre_reg_io_t *io, uint32_t offset,
                                   uint32_t mask, uint32_t value)
{
    uint32_t current;
    mre_status_t status;

    status = mre_reg_read32(io, offset, &current);
    if (status != MRE_OK) return status;
    current = (current & ~mask) | (value & mask);
    return mre_reg_write_verify32(io, offset, current, mask);
}

static uint32_t lane_bits(uint32_t data_lanes)
{
    uint32_t bits = MRE_DSI_HOST_CTRL_CLOCK_LANE;
    if (data_lanes >= 1u) bits |= MRE_DSI_HOST_CTRL_LANE0;
    if (data_lanes >= 2u) bits |= MRE_DSI_HOST_CTRL_LANE1;
    if (data_lanes >= 3u) bits |= MRE_DSI_HOST_CTRL_LANE2;
    if (data_lanes >= 4u) bits |= MRE_DSI_HOST_CTRL_LANE3;
    return bits;
}

mre_status_t mre_dsi_host_sm6150_configure(
    const mre_dsi_host_sm6150_config_t *config)
{
    const mre_reg_io_t *io;
    const mre_dsi_sm6150_timing_t *timing;
    uint32_t active_h_start, active_h_end, h_total;
    uint32_t active_v_start, active_v_end, v_total;
    uint32_t value;
    mre_status_t status;

    if (config == NULL || config->ctrl == NULL) return MRE_E_INVALID;
    io = config->ctrl;
    timing = &config->timing;

    if (config->data_lanes == 0u || config->data_lanes > 4u ||
        config->vc_id > 3u || config->format_code > 3u ||
        config->t_clk_pre > 0x3Fu || config->t_clk_post > 0x3Fu)
        return MRE_E_INVALID;
    if (timing->h_active == 0u || timing->v_active == 0u)
        return MRE_E_INVALID;

    /* Clock lane timing: DSI_CLKOUT_TIMING_CTRL =
       (t_clk_post & 0x3F) << 8 | (t_clk_pre & 0x3F).
       dsi_ctrl_hw_cmn.c:105-108. */
    value = ((config->t_clk_post & 0x3Fu) << 8) |
            (config->t_clk_pre & 0x3Fu);
    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_CLKOUT_TIMING_CTRL, value,
                           0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* EOT packet control: append TX EOT, ignore RX EOT.
       dsi_ctrl_hw_cmn.c:110-113. */
    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_EOT_PACKET_CTRL,
                           0x1u | (1u << 4), 0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* Turn on dsi clocks: DSI_CLK_CTRL = 0x23F. dsi_ctrl_hw_cmn.c:115-116. */
    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_CLK_CTRL,
                           MRE_DSI_HOST_CLK_CTRL_ON, 0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* DSI_CTRL: ECC/CRC checks, clock lane and the present data lanes.
       dsi_ctrl_hw_cmn.c:118-131. */
    value = MRE_DSI_HOST_CTRL_ECC_CHECK | MRE_DSI_HOST_CTRL_CRC_CHECK |
            lane_bits(config->data_lanes);
    status = update_checked(io, MRE_DSI_HOST_REG_CTRL,
                            MRE_DSI_HOST_CTRL_ECC_CHECK |
                                MRE_DSI_HOST_CTRL_CRC_CHECK |
                                MRE_DSI_HOST_CTRL_CLOCK_LANE |
                                MRE_DSI_HOST_CTRL_LANE0 |
                                MRE_DSI_HOST_CTRL_LANE1 |
                                MRE_DSI_HOST_CTRL_LANE2 |
                                MRE_DSI_HOST_CTRL_LANE3,
                            value);
    if (status != MRE_OK) return status;

    /* Video engine control: traffic mode (9:8), virtual channel (1:0) and
       destination format (5:4). dsi_ctrl_hw_cmn.c:500-507 and the
       video_mode_format_map at dsi_ctrl_hw_cmn.c:37-38. */
    value = ((config->traffic_mode & 0x3u) << 8) |
            (config->vc_id & 0x3u) | ((config->format_code & 0x3u) << 4);
    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_VIDEO_MODE_CTRL, value,
                           0x00000333u);
    if (status != MRE_OK) return status;

    /* No pixel swap, no bit swap. dsi_ctrl_hw_cmn.c:509-513. */
    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_VIDEO_MODE_DATA_CTRL, 0u,
                           0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* Timing double buffering disabled. dsi_ctrl_hw_cmn.c:514-515. */
    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_TIMING_DB_MODE, 0u,
                           0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* Video timing registers. dsi_ctrl_hw_cmn.c:314-347. */
    active_h_start = timing->h_sync_width + timing->h_back_porch;
    active_h_end = active_h_start + timing->h_active;
    h_total = (timing->h_sync_width + timing->h_back_porch +
               timing->h_active + timing->h_front_porch) - 1u;

    active_v_start = timing->v_sync_width + timing->v_back_porch;
    active_v_end = active_v_start + timing->v_active;
    v_total = (timing->v_sync_width + timing->v_back_porch +
               timing->v_active + timing->v_front_porch) - 1u;

    value = ((active_h_end & 0xFFFFu) << 16) | (active_h_start & 0xFFFFu);
    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_VIDEO_MODE_ACTIVE_H, value,
                           0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    value = ((active_v_end & 0xFFFFu) << 16) | (active_v_start & 0xFFFFu);
    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_VIDEO_MODE_ACTIVE_V, value,
                           0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    value = ((v_total & 0xFFFFu) << 16) | (h_total & 0xFFFFu);
    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_VIDEO_MODE_TOTAL, value,
                           0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* HSYNC/VSYNC start at zero; end equals the sync width.
       dsi_ctrl_hw_cmn.c:326-347. */
    value = ((timing->h_sync_width & 0xFFFFu) << 16);
    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_VIDEO_MODE_HSYNC, value,
                           0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    value = ((timing->v_sync_width & 0xFFFFu) << 16);
    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_VIDEO_MODE_VSYNC, value,
                           0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_VIDEO_MODE_VSYNC_VPOS, value,
                           0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* Fixed helper values written by the vendor timing routine.
       dsi_ctrl_hw_cmn.c:349-352. */
    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_HS_TIMER_CTRL, 0x3FD08u,
                           0xFFFFFFFFu);
    if (status != MRE_OK) return status;
    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_MISR_VIDEO_CTRL, 0x10100u,
                           0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* DSI_DSI_TIMING_FLUSH is a write-only flush trigger: no read-back. */
    return mre_reg_write32(io, MRE_DSI_HOST_REG_TIMING_FLUSH, 0x1u);
}

mre_status_t mre_dsi_host_sm6150_enable(const mre_reg_io_t *ctrl)
{
    uint32_t clock_ctrl;
    mre_status_t status;

    if (ctrl == NULL) return MRE_E_INVALID;

    /* Force the byte/pixel/AHB clocks on across the DSI_EN write.
       dsi_ctrl_hw_cmn.c:594-621. */
    status = mre_reg_read32(ctrl, MRE_DSI_HOST_REG_CLK_CTRL, &clock_ctrl);
    if (status != MRE_OK) return status;
    status = mre_reg_write_verify32(ctrl, MRE_DSI_HOST_REG_CLK_CTRL,
                           clock_ctrl | MRE_DSI_HOST_CLK_CTRL_FORCE_ON,
                           0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    status = update_checked(ctrl, MRE_DSI_HOST_REG_CTRL,
                            MRE_DSI_HOST_CTRL_DSI_EN,
                            MRE_DSI_HOST_CTRL_DSI_EN);
    if (status != MRE_OK) return status;

    status = mre_reg_write_verify32(ctrl, MRE_DSI_HOST_REG_CLK_CTRL, clock_ctrl,
                           0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* Video engine after the controller is enabled. dsi_ctrl_hw_cmn.c:573-590. */
    return update_checked(ctrl, MRE_DSI_HOST_REG_CTRL,
                          MRE_DSI_HOST_CTRL_VIDEO_MODE_EN,
                          MRE_DSI_HOST_CTRL_VIDEO_MODE_EN);
}

mre_status_t mre_dsi_host_sm6150_disable(const mre_reg_io_t *ctrl)
{
    mre_status_t status;

    if (ctrl == NULL) return MRE_E_INVALID;

    status = update_checked(ctrl, MRE_DSI_HOST_REG_CTRL,
                            MRE_DSI_HOST_CTRL_VIDEO_MODE_EN, 0u);
    if (status != MRE_OK) return status;
    return update_checked(ctrl, MRE_DSI_HOST_REG_CTRL,
                          MRE_DSI_HOST_CTRL_DSI_EN, 0u);
}

/* Push one encoded packet through the controller command FIFO. This is the
   register-only path from dsi_ctrl_hw_cmn_kickoff_fifo_command()
   (dsi_ctrl_hw_cmn.c:708-762); it needs no DMA allocation. */
static mre_status_t send_packet(const mre_reg_io_t *io, const uint8_t *bytes,
                                size_t length)
{
    uint32_t words[8];
    size_t word_count = 0u;
    size_t index;
    uint32_t value;
    mre_status_t status;

    if (bytes == NULL || length == 0u || length > 32u) return MRE_E_INVALID;
    status = mre_dsi_pack_words(bytes, length, words,
                                sizeof(words) / sizeof(words[0]),
                                &word_count);
    if (status != MRE_OK) return status;

    /* CMD_DMA_TPG_EN | TPG_DMA_FIFO_MODE | PATTERN_SEL = custom pattern. */
    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_TEST_PATTERN_GEN_CTRL,
                           MRE_DSI_HOST_TPG_CMD_DMA_MODE |
                               MRE_DSI_HOST_TPG_PATTERN_SEL,
                           0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    for (index = 0u; index < word_count; ++index) {
        status = mre_reg_write_verify32(
            io, MRE_DSI_HOST_REG_TEST_PATTERN_GEN_CMD_DMA_INIT_VAL,
            words[index], 0xFFFFFFFFu);
        if (status != MRE_OK) return status;
    }
    /* An odd DWORD count needs one zero DWORD in the FIFO. */
    if ((word_count & 1u) != 0u) {
        status = mre_reg_write_verify32(
            io, MRE_DSI_HOST_REG_TEST_PATTERN_GEN_CMD_DMA_INIT_VAL, 0u,
            0xFFFFFFFFu);
        if (status != MRE_OK) return status;
    }

    /* Embedded mode, WC_SEL = 0, packet type = 0, no broadcast/master/LPM.
       dsi_ctrl_hw_cmn.c:658-676. */
    status = mre_reg_read32(io, MRE_DSI_HOST_REG_COMMAND_MODE_DMA_CTRL,
                            &value);
    if (status != MRE_OK) return status;
    value &= ~MRE_DSI_HOST_DMA_CTRL_CLEAR_MASK;
    value |= MRE_DSI_HOST_DMA_CTRL_EMBEDDED;
    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_COMMAND_MODE_DMA_CTRL,
                                    value, 0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* Disable write and read watermarks. dsi_ctrl_hw_cmn.c:678-683. */
    status = mre_reg_read32(io, MRE_DSI_HOST_REG_DMA_FIFO_CTRL, &value);
    if (status != MRE_OK) return status;
    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_DMA_FIFO_CTRL,
                                    value | MRE_DSI_HOST_DMA_FIFO_WATERMARK_OFF,
                                    0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    status = mre_reg_write_verify32(io, MRE_DSI_HOST_REG_DMA_CMD_LENGTH,
                           (uint32_t)length, 0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* Trigger is a write-only strobe. dsi_ctrl_hw_cmn.c:687-689. */
    return mre_reg_write32(io, MRE_DSI_HOST_REG_CMD_MODE_DMA_SW_TRIGGER,
                           0x1u);
}

/* Wait until the controller reports a non-zero read byte count in
   RDBK_DATA_CTRL[31:16] (dsi_ctrl_hw_cmn.c:835). */
static mre_status_t wait_read_count(const mre_reg_io_t *io, uint32_t timeout_us,
                                    uint32_t max_polls, uint32_t *count)
{
    uint64_t start = 0u;
    uint32_t poll;
    uint32_t value = 0u;

    if (count == NULL) return MRE_E_INVALID;
    *count = 0u;
    if (io->ops->time_us != NULL) start = io->ops->time_us(io->context);

    for (poll = 0u; poll < max_polls; ++poll) {
        if (mre_reg_read32(io, MRE_DSI_HOST_REG_RDBK_DATA_CTRL, &value) !=
            MRE_OK)
            return MRE_E_IO;
        *count = (value & MRE_DSI_HOST_RDBK_COUNT_MASK) >> 16;
        if (*count != 0u) return MRE_OK;
        if (io->ops->time_us != NULL &&
            (io->ops->time_us(io->context) - start) >= timeout_us)
            break;
        if (io->ops->delay_us != NULL)
            io->ops->delay_us(io->context, 1u);
    }
    return MRE_E_BUSY;
}

mre_status_t mre_dsi_host_sm6150_dcs_read(
    const mre_dsi_dcs_read_config_t *config, uint8_t dcs_command,
    uint8_t *out, uint32_t out_length)
{
    const mre_reg_io_t *io;
    uint32_t timeout_us, max_polls;
    uint32_t read_count = 0u;
    uint32_t words_to_read;
    uint32_t index;
    uint8_t buffer[16];
    uint8_t packet[MRE_DSI_LONG_HEADER_BYTES + 8u];
    uint8_t payload[3];
    const uint8_t *response;
    mre_status_t status;

    if (config == NULL || config->ctrl == NULL || out == NULL)
        return MRE_E_INVALID;
    if (out_length == 0u || out_length > 4u) return MRE_E_INVALID;
    io = config->ctrl;
    timeout_us = config->timeout_us != 0u ? config->timeout_us : 1000u;
    max_polls = config->max_polls != 0u ? config->max_polls : 100u;

    /* Multi-byte reads need an explicit return size: DCS 0x37 with the size
       little endian, sent as a long write. dsi_ctrl.c:1353-1373. */
    if (out_length > 2u) {
        payload[0] = 0x37u;
        payload[1] = (uint8_t)(out_length & 0xFFu);
        payload[2] = (uint8_t)((out_length >> 8) & 0xFFu);
        status = mre_dsi_encode_long_write(MRE_DSI_DT_DCS_LONG_WRITE, payload,
                                           3u, packet, sizeof(packet));
        if (status != MRE_OK) return status;
        status = send_packet(io, packet, MRE_DSI_LONG_HEADER_BYTES + 3u);
        if (status != MRE_OK) return status;
    }

    /* Clear the read-back registers. dsi_ctrl_hw_cmn.c:798-806. */
    status = mre_reg_write32(io, MRE_DSI_HOST_REG_RDBK_DATA_CTRL, 0x1u);
    if (status != MRE_OK) return status;
    status = mre_reg_write32(io, MRE_DSI_HOST_REG_RDBK_DATA_CTRL, 0x0u);
    if (status != MRE_OK) return status;

    /* Send the read command itself: DCS short packet (0x06, command). */
    status = mre_dsi_encode_short_write(MRE_DSI_DT_DCS_READ, dcs_command, 0u,
                                        packet);
    if (status != MRE_OK) return status;
    status = send_packet(io, packet, MRE_DSI_SHORT_PACKET_BYTES);
    if (status != MRE_OK) return status;

    status = wait_read_count(io, timeout_us, max_polls, &read_count);
    if (status != MRE_OK) return status;
    if (read_count > 16u) read_count = 16u;

    /* Assemble the read-back window: RDBK_DATA0 holds the first four bytes of
       the response, DATA1 the next four, and so on, each word big endian.
       dsi_ctrl_hw_cmn_get_cmd_read_data() starts its word walk at
       DSI_RDBK_DATA0 (dsi_ctrl_hw_cmn.c:858) and parses the response type at
       the start of that window (:1521-1543), so the response is left-aligned.
       The vendor multi-word path reads descending and compensates with a
       pointer offset (:858-875); that depends on the DMA read repetition this
       driver does not perform, so the order here is ascending and documented
       as unverified on hardware. A wrong order yields an unknown response
       type and therefore no identifier, never a false match. */
    memset(buffer, 0, sizeof(buffer));
    words_to_read = (read_count + 3u) / 4u;
    if (words_to_read > 4u) words_to_read = 4u;
    for (index = 0u; index < words_to_read; ++index) {
        uint32_t word = 0u;
        status = mre_reg_read32(io, MRE_DSI_HOST_REG_RDBK_DATA0 + 4u * index,
                                &word);
        if (status != MRE_OK) return status;
        buffer[index * 4u + 0u] = (uint8_t)((word >> 24) & 0xFFu);
        buffer[index * 4u + 1u] = (uint8_t)((word >> 16) & 0xFFu);
        buffer[index * 4u + 2u] = (uint8_t)((word >> 8) & 0xFFu);
        buffer[index * 4u + 3u] = (uint8_t)(word & 0xFFu);
    }
    response = buffer;

    switch (response[0]) {
    case MRE_DSI_RX_DCS_SHORT_READ_1: /* dsi_ctrl.c:1527-1530 */
        if (out_length != 1u) return MRE_E_IO;
        out[0] = response[1];
        return MRE_OK;
    case MRE_DSI_RX_DCS_SHORT_READ_2: /* dsi_ctrl.c:1531-1534 */
        if (out_length != 2u) return MRE_E_IO;
        out[0] = response[1];
        out[1] = response[2];
        return MRE_OK;
    case MRE_DSI_RX_DCS_LONG_READ: { /* dsi_ctrl.c:1535-1538 */
        uint32_t payload = (uint32_t)response[1] |
                           ((uint32_t)response[2] << 8);
        if (out_length > payload) return MRE_E_IO;
        if (read_count < 4u + out_length) return MRE_E_IO;
        memcpy(out, response + 4, out_length);
        return MRE_OK;
    }
    default: /* 0x02 acknowledge/error or an unknown type: dsi_ctrl.c:1521-1543 */
        return MRE_E_IO;
    }
}

mre_status_t mre_dsi_host_sm6150_soft_reset(const mre_reg_io_t *ctrl)
{
    uint32_t clock_ctrl;
    uint32_t control;
    mre_status_t status;

    if (ctrl == NULL) return MRE_E_INVALID;

    /* Clear DSI_EN, VIDEO_MODE_EN and CMD_MODE_EN. dsi_ctrl_hw_cmn.c:169-171. */
    status = mre_reg_read32(ctrl, MRE_DSI_HOST_REG_CTRL, &control);
    if (status != MRE_OK) return status;
    status = mre_reg_write_verify32(ctrl, MRE_DSI_HOST_REG_CTRL, control & ~0x7u,
                           0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* Force clocks on, pulse DSI_SOFT_RESET, restore clocks.
       dsi_ctrl_hw_cmn.c:174-187. */
    status = mre_reg_read32(ctrl, MRE_DSI_HOST_REG_CLK_CTRL, &clock_ctrl);
    if (status != MRE_OK) return status;
    status = mre_reg_write_verify32(ctrl, MRE_DSI_HOST_REG_CLK_CTRL,
                           clock_ctrl | MRE_DSI_HOST_CLK_CTRL_FORCE_ON,
                           0xFFFFFFFFu);
    if (status != MRE_OK) return status;
    status = mre_reg_write32(ctrl, MRE_DSI_HOST_REG_SOFT_RESET, 0x1u);
    if (status != MRE_OK) return status;
    status = mre_reg_write_verify32(ctrl, MRE_DSI_HOST_REG_SOFT_RESET, 0x0u, 0xFFFFFFFFu);
    if (status != MRE_OK) return status;
    status = mre_reg_write_verify32(ctrl, MRE_DSI_HOST_REG_CLK_CTRL, clock_ctrl,
                           0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* Restore the previous control value. dsi_ctrl_hw_cmn.c:190-191. */
    return mre_reg_write_verify32(ctrl, MRE_DSI_HOST_REG_CTRL, control, 0xFFFFFFFFu);
}
