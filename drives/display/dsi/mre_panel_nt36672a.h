/* SPDX-License-Identifier: MIT */
/* mre_panel_nt36672a.h - panel descriptor contract and the ID gate.
 *
 * A descriptor is data, not a driver: name, compatible, the mode, the on/off
 * command tables, the reset steps and the backlight range. Everything in the
 * nt36672a table below the descriptor comes from the vendor panel dtsi; if a
 * value is not in that file it is not in this table.
 *
 * The ID gate is the important part. A panel that has no recorded ID command
 * can never have its init table authorized, because nothing proved the panel
 * on the glass is the panel the table was written for. `mre_panel_initialization()`
 * therefore returns NULL unless a real ID read matched.
 */
#ifndef MRE_PANEL_NT36672A_H
#define MRE_PANEL_NT36672A_H

#include <stddef.h>
#include <stdint.h>

#include "mre_dsi_packet.h"

typedef struct mre_panel_reset_step {
    uint32_t gpio_value;
    uint32_t delay_ms;
} mre_panel_reset_step_t;

/* Either completely absent (dcs_command == 0, length == 0, no evidence) or
 * complete; a half-filled description is a validation error. */
typedef struct mre_panel_id {
    uint32_t dcs_command;
    uint32_t length;
    uint8_t expected[4];
    uint8_t match_mask[4];
    const char *evidence;
} mre_panel_id_t;

typedef struct mre_panel_descriptor {
    /* The device-tree compatible this table serves, e.g.
     * "novatek,nt36672a-shenchao". */
    const char *name;
    /* The string mre_panel_lookup_compatible() matches. The nt36672a table
     * stores the vendor panel-name property here ("nt36672a video mode dsi
     * shenchao panel"), because that is what its source records; the lookup
     * follows the table rather than the field name. */
    const char *compatible;
    mre_panel_mode_t mode;
    const mre_dsi_cmd_entry_t *on_sequence;
    size_t on_count;
    const mre_dsi_cmd_entry_t *off_sequence;
    size_t off_count;
    const mre_panel_reset_step_t *reset_sequence;
    size_t reset_count;
    uint32_t backlight_min_level;
    uint32_t backlight_max_level;
    uint32_t lp11_init;
    uint32_t force_clock_lane_hs;
    uint32_t bllp_power_mode;
    mre_panel_id_t id;
} mre_panel_descriptor_t;

/* The novatek,nt36672a-shenchao table compiled into this tree. Its descriptor
 * carries no ID command: the vendor tree defines none, so its init table is
 * unreachable until the bytes are captured from the handset. */
const mre_panel_descriptor_t *mre_panel_nt36672a_shenchao(void);

const mre_panel_descriptor_t *mre_panel_lookup_compatible(
    const char *compatible);

mre_status_t mre_panel_descriptor_validate(const mre_panel_descriptor_t *panel);

/* 1 when the descriptor carries a complete, non-zero-mask ID description. */
int mre_panel_id_defined(const mre_panel_descriptor_t *panel);

/* Compares only the masked bits; 0 on any mismatch or bad argument. */
int mre_panel_id_equal(const uint8_t *expected, const uint8_t *match_mask,
                       const uint8_t *actual, size_t length);

int mre_panel_id_matches(const mre_panel_descriptor_t *panel,
                         const uint8_t *read_id, size_t read_length);

/* The on-sequence, but only for a panel whose ID already matched. */
const mre_dsi_cmd_entry_t *mre_panel_initialization(
    const mre_panel_descriptor_t *panel, const uint8_t *read_id,
    size_t read_length, size_t *count_out);

/* Reads the ID over DSI, then decides. Needs the host controller's read path,
 * so this stays host-gated while mre_dsi_host_sm6150.c is not buildable. The
 * read configuration is declared by mre_dsi_host_sm6150.h; the tag is named
 * here so a consumer that only selects a panel does not pull the controller
 * header in. */
struct mre_dsi_dcs_read_config;
const mre_dsi_cmd_entry_t *mre_panel_identify_and_select(
    const mre_panel_descriptor_t *panel,
    const struct mre_dsi_dcs_read_config *read_config, uint8_t *id_out,
    size_t *count_out);

#endif
