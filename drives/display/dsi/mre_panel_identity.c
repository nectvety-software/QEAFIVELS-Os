#include "mre_panel_nt36672a.h"
#include "mre_dsi_host_sm6150.h"

int mre_panel_id_defined(const mre_panel_descriptor_t *panel)
{
    uint32_t index;
    uint32_t mask_bits = 0u;

    if (panel == NULL) return 0;
    if (panel->id.dcs_command == 0u || panel->id.length == 0u ||
        panel->id.evidence == NULL)
        return 0;
    for (index = 0u; index < panel->id.length; ++index)
        mask_bits |= panel->id.match_mask[index];
    return mask_bits != 0u ? 1 : 0;
}

int mre_panel_id_equal(const uint8_t *expected, const uint8_t *match_mask,
                       const uint8_t *actual, size_t length)
{
    size_t index;

    if (expected == NULL || match_mask == NULL || actual == NULL ||
        length == 0u)
        return 0;
    for (index = 0u; index < length; ++index) {
        if ((uint8_t)(expected[index] & match_mask[index]) !=
            (uint8_t)(actual[index] & match_mask[index]))
            return 0;
    }
    return 1;
}

int mre_panel_id_matches(const mre_panel_descriptor_t *panel,
                         const uint8_t *read_id, size_t read_length)
{
    if (!mre_panel_id_defined(panel)) return 0;
    if (read_id == NULL || read_length != (size_t)panel->id.length) return 0;
    return mre_panel_id_equal(panel->id.expected, panel->id.match_mask, read_id,
                              read_length);
}

const mre_dsi_cmd_entry_t *mre_panel_initialization(
    const mre_panel_descriptor_t *panel, const uint8_t *read_id,
    size_t read_length, size_t *count_out)
{
    if (count_out != NULL) *count_out = 0u;
    if (panel == NULL || panel->on_sequence == NULL || panel->on_count == 0u)
        return NULL;
    if (!mre_panel_id_matches(panel, read_id, read_length)) return NULL;
    if (count_out != NULL) *count_out = panel->on_count;
    return panel->on_sequence;
}

const mre_dsi_cmd_entry_t *mre_panel_identify_and_select(
    const mre_panel_descriptor_t *panel,
    const mre_dsi_dcs_read_config_t *read_config, uint8_t *id_out,
    size_t *count_out)
{
    uint8_t id[4];
    uint32_t length;
    uint32_t index;
    mre_status_t status;

    if (count_out != NULL) *count_out = 0u;
    if (panel == NULL || read_config == NULL) return NULL;

    /* A descriptor without a vendor-documented ID can never authorize the
       init table, and no command is sent to the panel on its behalf. */
    if (!mre_panel_id_defined(panel)) return NULL;

    length = panel->id.length;
    if (length == 0u || length > sizeof(id)) return NULL;
    status = mre_dsi_host_sm6150_dcs_read(read_config, panel->id.dcs_command,
                                          id, length);
    if (status != MRE_OK) {
        if (id_out != NULL)
            for (index = 0u; index < length; ++index) id_out[index] = 0u;
        return NULL;
    }
    if (id_out != NULL)
        for (index = 0u; index < length; ++index) id_out[index] = id[index];
    return mre_panel_initialization(panel, id, length, count_out);
}
