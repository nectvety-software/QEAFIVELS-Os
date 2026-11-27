/* SPDX-License-Identifier: MIT */
/* mre_dsi_host_sm6150.h - SM6150 DSI host controller contract.
 *
 * This header is the *interface* half of mre_dsi_host_sm6150.c: the two
 * configuration records the caller fills, and the five operations the driver
 * offers. It is deliberately complete enough for the rest of the DSI stack
 * (panel identification, panel selection) to compile and be host-tested
 * against a fake read path.
 *
 * ============================ BLOCKED ============================
 * mre_dsi_host_sm6150.c is NOT in any build list yet. It programs the
 * downstream `dsi.xml.h` register map (DSI_CLKOUT_TIMING_CTRL,
 * DSI_VIDEO_MODE_*, DSI_TEST_PATTERN_GEN_*, DSI_RDBK_DATA*, ...), and that
 * map is not part of this repository. Writing those offsets from memory would
 * be inventing hardware facts, and a wrong offset here writes a real
 * controller, so the register definitions are intentionally absent: the
 * translation unit fails to build until the vendor map is added with its
 * source recorded. The DSI host therefore stays out of vqeaf_drives and out of
 * the Violet image, and `mre_panel_identify_and_select()` has no compiled
 * implementation.
 * ==================================================================
 */
#ifndef MRE_DSI_HOST_SM6150_H
#define MRE_DSI_HOST_SM6150_H

#include <stdint.h>

#include "mre_dsi_packet.h"
#include "mre_reg_io.h"

/* The controller programs the same nine timing fields the packet layer
 * derives, so the derived type is reused verbatim rather than duplicated. */
typedef mre_dsi_timing_t mre_dsi_sm6150_timing_t;

typedef struct {
    const mre_reg_io_t *ctrl;
    mre_dsi_sm6150_timing_t timing;
    uint32_t data_lanes;   /* 1..4 */
    uint32_t vc_id;        /* 0..3 */
    uint32_t format_code;  /* 0..3, vendor video_mode_format_map */
    uint32_t t_clk_pre;    /* 6 bits */
    uint32_t t_clk_post;   /* 6 bits */
    uint32_t traffic_mode; /* 0 sync pulse, 1 sync event, 2 burst */
} mre_dsi_host_sm6150_config_t;

typedef struct mre_dsi_dcs_read_config {
    const mre_reg_io_t *ctrl;
    uint32_t timeout_us; /* 0 -> 1000 */
    uint32_t max_polls;  /* 0 -> 100 */
} mre_dsi_dcs_read_config_t;

mre_status_t mre_dsi_host_sm6150_configure(
    const mre_dsi_host_sm6150_config_t *config);

mre_status_t mre_dsi_host_sm6150_enable(const mre_reg_io_t *ctrl);

mre_status_t mre_dsi_host_sm6150_disable(const mre_reg_io_t *ctrl);

/* DCS read of 1..4 bytes. A multi-byte read first sends DCS 0x37 with the
 * size, then the read command, then parses the response type from the
 * left-aligned read-back window. An unknown response type is MRE_E_IO, never
 * a guessed value. */
mre_status_t mre_dsi_host_sm6150_dcs_read(
    const mre_dsi_dcs_read_config_t *config, uint8_t dcs_command,
    uint8_t *out, uint32_t out_length);

mre_status_t mre_dsi_host_sm6150_soft_reset(const mre_reg_io_t *ctrl);

#endif
