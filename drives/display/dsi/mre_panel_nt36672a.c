#include "mre_panel_nt36672a.h"
#include <string.h>

/* Source: arch/arm64/boot/dts/qcom/dsi-panel-nt36672a-shenchao-fhd-video.dtsi
 * ("nt36672a video mode dsi shenchao panel"). Every number below appears in
 * that file; nothing here is estimated. */

static const mre_dsi_cmd_entry_t g_nt36672a_on[] = {
    {0x15u, 2u, {0xFFu, 0x23u}, 0u},
    {0x15u, 2u, {0xFBu, 0x01u}, 0u},
    {0x15u, 2u, {0x07u, 0x20u}, 0u},
    {0x15u, 2u, {0x08u, 0x04u}, 0u},
    {0x15u, 2u, {0x09u, 0x00u}, 0u},
    {0x15u, 2u, {0xFFu, 0x25u}, 0u},
    {0x15u, 2u, {0xFBu, 0x01u}, 0u},
    {0x15u, 2u, {0x18u, 0x96u}, 0u},
    {0x15u, 2u, {0x05u, 0x04u}, 0u},
    {0x15u, 2u, {0xFFu, 0x27u}, 0u},
    {0x15u, 2u, {0xFBu, 0x01u}, 0u},
    {0x15u, 2u, {0xD9u, 0x10u}, 0u},
    {0x15u, 2u, {0xFFu, 0x24u}, 0u},
    {0x15u, 2u, {0xFBu, 0x01u}, 0u},
    {0x15u, 2u, {0x82u, 0x13u}, 0u},
    {0x15u, 2u, {0x84u, 0x31u}, 0u},
    {0x15u, 2u, {0x88u, 0x13u}, 0u},
    {0x15u, 2u, {0x8Au, 0x31u}, 0u},
    {0x15u, 2u, {0x8Eu, 0xE4u}, 0u},
    {0x15u, 2u, {0x8Fu, 0x01u}, 0u},
    {0x15u, 2u, {0x90u, 0x80u}, 0u},
    {0x15u, 2u, {0x9Du, 0xB0u}, 0u},
    {0x15u, 2u, {0xFFu, 0x26u}, 0u},
    {0x15u, 2u, {0xFBu, 0x01u}, 0u},
    {0x15u, 2u, {0xA9u, 0x12u}, 0u},
    {0x15u, 2u, {0xAAu, 0x10u}, 0u},
    {0x15u, 2u, {0xAEu, 0x8Au}, 0u},
    {0x15u, 2u, {0x1Cu, 0xFAu}, 0u},
    {0x15u, 2u, {0xFFu, 0x10u}, 0u},
    {0x15u, 2u, {0xB0u, 0x01u}, 0u},
    {0x15u, 2u, {0x51u, 0xFFu}, 0u},
    /* Long write: DCS 0x68 with two parameters (panel timing knob). */
    {0x39u, 3u, {0x68u, 0x03u, 0x04u}, 0u},
    {0x15u, 2u, {0x53u, 0x2Cu}, 0u},
    {0x15u, 2u, {0x55u, 0x00u}, 0u},
    /* Display on, then sleep out with the table's 0x46 (70) wait. */
    {0x05u, 1u, {0x29u, 0x00u}, 0u},
    {0x05u, 1u, {0x11u, 0x00u}, 0x46u}
};

static const mre_dsi_cmd_entry_t g_nt36672a_off[] = {
    {0x05u, 1u, {0x28u, 0x00u}, 0x14u},   /* display off, 20 */
    {0x05u, 1u, {0x10u, 0x00u}, 0x78u}    /* sleep in, 120 */
};

/* qcom,mdss-dsi-reset-sequence = <1 1>, <0 1>, <1 10>; */
static const mre_panel_reset_step_t g_nt36672a_reset[] = {
    {1u, 1u},
    {0u, 1u},
    {1u, 10u}
};

static const mre_panel_descriptor_t g_nt36672a_shenchao = {
    "novatek,nt36672a-shenchao",
    "nt36672a video mode dsi shenchao panel",
    {
        "nt36672a-shenchao-1080x2340-60",
        1080u, 2340u,
        90u, 120u, 2u, 0u,
        13u, 8u, 3u,
        60u, 24u, 4u
    },
    g_nt36672a_on, sizeof(g_nt36672a_on) / sizeof(g_nt36672a_on[0]),
    g_nt36672a_off, sizeof(g_nt36672a_off) / sizeof(g_nt36672a_off[0]),
    g_nt36672a_reset, sizeof(g_nt36672a_reset) / sizeof(g_nt36672a_reset[0]),
    1u,      /* bl-min-level */
    4095u,   /* bl-max-level */
    1u,      /* qcom,mdss-dsi-lp11-init */
    1u,      /* qcom,mdss-dsi-force-clock-lane-hs */
    1u,      /* qcom,mdss-dsi-bllp-power-mode */
    /* No panel-ID command is defined for this panel by the vendor tree at
       commit 54dd13a14 (no qcom,mdss-dsi-panel-id property in the DTS and no
       ID read in dsi_panel.c). Until the bytes are captured from this handset
       and recorded here, the init table stays unreachable. */
    {0u, 0u, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, NULL}
};

const mre_panel_descriptor_t *mre_panel_nt36672a_shenchao(void)
{
    return &g_nt36672a_shenchao;
}

const mre_panel_descriptor_t *mre_panel_lookup_compatible(const char *compatible)
{
    if (compatible == NULL) return NULL;
    if (strcmp(compatible, g_nt36672a_shenchao.compatible) == 0)
        return &g_nt36672a_shenchao;
    return NULL;
}

mre_status_t mre_panel_descriptor_validate(const mre_panel_descriptor_t *panel)
{
    mre_status_t st;
    if (panel == NULL || panel->name == NULL || panel->compatible == NULL)
        return MRE_E_INVALID;
    st = mre_panel_mode_validate(&panel->mode);
    if (st != MRE_OK) return st;
    st = mre_dsi_command_table_validate(panel->on_sequence, panel->on_count);
    if (st != MRE_OK) return st;
    st = mre_dsi_command_table_validate(panel->off_sequence, panel->off_count);
    if (st != MRE_OK) return st;
    if (panel->reset_sequence == NULL || panel->reset_count < 2u)
        return MRE_E_INVALID;
    if (panel->backlight_max_level <= panel->backlight_min_level)
        return MRE_E_INVALID;

    /* An ID description must be either completely absent or complete. A
       half-filled one would silently authorize or silently block a panel. */
    if (panel->id.dcs_command != 0u) {
        uint32_t index;
        uint32_t mask_bits = 0u;
        if (panel->id.length == 0u || panel->id.length > 4u)
            return MRE_E_INVALID;
        if (panel->id.evidence == NULL) return MRE_E_INVALID;
        for (index = 0u; index < panel->id.length; ++index)
            mask_bits |= panel->id.match_mask[index];
        if (mask_bits == 0u) return MRE_E_INVALID;
    } else if (panel->id.length != 0u || panel->id.evidence != NULL) {
        return MRE_E_INVALID;
    }
    return MRE_OK;
}
