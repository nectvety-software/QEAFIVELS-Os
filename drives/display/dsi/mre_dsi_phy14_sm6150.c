#include "mre_dsi_phy14_sm6150.h"

static mre_status_t lane_write_checked(const mre_reg_io_t *io, uint32_t lane,
                                       uint32_t offset, uint32_t value)
{
    return mre_reg_write_verify32(io, lane * MRE_DSI_PHY14_LANE_STRIDE + offset,
                                  value, 0xFFFFFFFFu);
}

/* One lane's timing registers, exactly as dsi_14nm_dphy_set_timing()
   programs them (dsi_phy_14nm.c:22-67). Field packing follows the generated
   field macros in dsi.xml.h:1390-1456:
   TIMING_CTRL_4 hs_exit[7:0], 5 hs_zero[7:0], 6 hs_prepare[7:0],
   7 hs_trail[7:0], 8 hs_rqst[7:0], CFG0 prepare_dly[7:6],
   CFG1 halfbyteclk_en[0], 9 ta_go[2:0] | ta_sure[6:4], 10 ta_get[2:0],
   11 trig3_cmd = 0xa0. */
static mre_status_t program_lane_timing(const mre_reg_io_t *io, uint32_t lane,
                                        const mre_dsi_phy14_timing_t *timing,
                                        int clock_lane)
{
    mre_status_t status;

    status = lane_write_checked(io, lane, MRE_DSI_PHY14_LN_TIMING_CTRL_4,
                                timing->hs_exit & 0xFFu);
    if (status != MRE_OK) return status;
    status = lane_write_checked(io, lane, MRE_DSI_PHY14_LN_TIMING_CTRL_5,
                                (clock_lane ? timing->clk_zero
                                            : timing->hs_zero) & 0xFFu);
    if (status != MRE_OK) return status;
    status = lane_write_checked(io, lane, MRE_DSI_PHY14_LN_TIMING_CTRL_6,
                                (clock_lane ? timing->clk_prepare
                                            : timing->hs_prepare) & 0xFFu);
    if (status != MRE_OK) return status;
    status = lane_write_checked(io, lane, MRE_DSI_PHY14_LN_TIMING_CTRL_7,
                                (clock_lane ? timing->clk_trail
                                            : timing->hs_trail) & 0xFFu);
    if (status != MRE_OK) return status;
    status = lane_write_checked(io, lane, MRE_DSI_PHY14_LN_TIMING_CTRL_8,
                                (clock_lane ? timing->clk_hs_rqst
                                            : timing->hs_rqst) & 0xFFu);
    if (status != MRE_OK) return status;
    status = lane_write_checked(io, lane, MRE_DSI_PHY14_LN_CFG0,
                                ((clock_lane ? timing->prepare_dly_ckln
                                             : timing->prepare_dly) & 0x3u)
                                    << 6);
    if (status != MRE_OK) return status;
    status = lane_write_checked(
        io, lane, MRE_DSI_PHY14_LN_CFG1,
        (clock_lane ? timing->halfbyte_en_ckln : timing->halfbyte_en) != 0u
            ? 0x1u
            : 0x0u);
    if (status != MRE_OK) return status;
    status = lane_write_checked(io, lane, MRE_DSI_PHY14_LN_TIMING_CTRL_9,
                                (timing->ta_go & 0x7u) |
                                    ((timing->ta_sure & 0x7u) << 4));
    if (status != MRE_OK) return status;
    status = lane_write_checked(io, lane, MRE_DSI_PHY14_LN_TIMING_CTRL_10,
                                timing->ta_get & 0x7u);
    if (status != MRE_OK) return status;
    return lane_write_checked(io, lane, MRE_DSI_PHY14_LN_TIMING_CTRL_11,
                              0xA0u);
}

mre_status_t mre_dsi_phy14_sm6150_configure(
    const mre_dsi_phy14_sm6150_config_t *config)
{
    uint32_t lane;
    mre_status_t status;

    if (config == NULL || config->common == NULL || config->lane == NULL ||
        config->pll == NULL)
        return MRE_E_INVALID;
    if (config->lane_count == 0u || config->lane_count > 5u ||
        config->clock_lane_index >= config->lane_count)
        return MRE_E_INVALID;

    /* LDO control keeps the vendor standalone value 0x1c; the extra VREG
       field is only set for the non-standalone use case (dsi_phy_14nm.c:78). */
    status = mre_reg_write_verify32(config->common,
                                    MRE_DSI_PHY14_CMN_LDO_CNTRL,
                                    MRE_DSI_PHY14_CMN_LDO_STANDALONE,
                                    0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* Global test control enable. dsi_phy_14nm.c:81. */
    status = mre_reg_write_verify32(config->common,
                                    MRE_DSI_PHY14_CMN_GLBL_TEST_CTRL,
                                    MRE_DSI_PHY14_CMN_GLBL_TEST, 0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* 4 data lanes + 1 clock lane configuration. dsi_phy_14nm.c:83-99. */
    for (lane = 0u; lane < config->lane_count; ++lane) {
        int clock_lane = ((int)lane == (int)config->clock_lane_index);

        status = lane_write_checked(config->lane, lane,
                                    MRE_DSI_PHY14_LN_VREG_CNTRL,
                                    MRE_DSI_PHY14_LN_VREG);
        if (status != MRE_OK) return status;
        status = lane_write_checked(config->lane, lane,
                                    MRE_DSI_PHY14_LN_STRENGTH_CTRL_0,
                                    MRE_DSI_PHY14_LN_STRENGTH_0);
        if (status != MRE_OK) return status;
        status = lane_write_checked(
            config->lane, lane, MRE_DSI_PHY14_LN_STRENGTH_CTRL_1,
            clock_lane ? MRE_DSI_PHY14_LN_STRENGTH_1_CLOCK
                       : MRE_DSI_PHY14_LN_STRENGTH_1_DATA);
        if (status != MRE_OK) return status;
        status = lane_write_checked(config->lane, lane, MRE_DSI_PHY14_LN_CFG3,
                                    clock_lane ? MRE_DSI_PHY14_LN_CFG3_CLOCK
                                               : MRE_DSI_PHY14_LN_CFG3_DATA);
        if (status != MRE_OK) return status;
        status = lane_write_checked(config->lane, lane, MRE_DSI_PHY14_LN_CFG2,
                                    MRE_DSI_PHY14_LN_CFG2_VALUE);
        if (status != MRE_OK) return status;
        status = lane_write_checked(config->lane, lane,
                                    MRE_DSI_PHY14_LN_TEST_DATAPATH, 0u);
        if (status != MRE_OK) return status;
        status = lane_write_checked(config->lane, lane,
                                    MRE_DSI_PHY14_LN_TEST_STR,
                                    MRE_DSI_PHY14_LN_TEST_STR_VALUE);
        if (status != MRE_OK) return status;
        status = program_lane_timing(config->lane, lane, &config->timing,
                                     clock_lane);
        if (status != MRE_OK) return status;
    }

    /* Make sure the PLL is not started. dsi_phy_14nm.c:101-102. */
    status = mre_reg_write_verify32(config->common,
                                    MRE_DSI_PHY14_CMN_PLL_CNTRL, 0u,
                                    0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* Reset the digital block: CTRL_1 = 0x80, wait 100 us, then 0.
       dsi_phy_14nm.c:104-107. */
    status = mre_reg_write_verify32(config->common,
                                    MRE_DSI_PHY14_CMN_CTRL_1,
                                    MRE_DSI_PHY14_CMN_CTRL_1_RESET,
                                    0xFFFFFFFFu);
    if (status != MRE_OK) return status;
    if (config->common->ops->delay_us != NULL)
        config->common->ops->delay_us(config->common->context, 100u);
    status = mre_reg_write_verify32(config->common,
                                    MRE_DSI_PHY14_CMN_CTRL_1, 0u, 0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* Remove power down from the PLL and all lanes. dsi_phy_14nm.c:117-118. */
    return mre_reg_write_verify32(config->common, MRE_DSI_PHY14_CMN_CTRL_0,
                                  MRE_DSI_PHY14_CMN_CTRL_0_POWER_UP,
                                  0xFFFFFFFFu);
}

mre_status_t mre_dsi_phy14_sm6150_enable(
    const mre_dsi_phy14_sm6150_config_t *config)
{
    uint32_t timeout_us;
    uint32_t max_polls;
    uint32_t status_value = 0u;
    mre_status_t status;

    if (config == NULL || config->common == NULL || config->pll == NULL)
        return MRE_E_INVALID;

    timeout_us = config->pll_timeout_us != 0u ? config->pll_timeout_us : 100u;
    max_polls = config->pll_max_polls != 0u ? config->pll_max_polls : 200u;

    /* PLL enable sequence: VREF_CFG1 = 0x10, then PLL_CNTRL = 1.
       dsi_pll_14nm.c:770-781. */
    status = mre_reg_write_verify32(config->pll,
                                    MRE_DSI_PHY14_PLL_VREF_CFG1,
                                    MRE_DSI_PHY14_PLL_VREF_ENABLE,
                                    0xFFFFFFFFu);
    if (status != MRE_OK) return status;
    status = mre_reg_write_verify32(config->common,
                                    MRE_DSI_PHY14_CMN_PLL_CNTRL, 1u,
                                    0xFFFFFFFFu);
    if (status != MRE_OK) return status;

    /* Lock poll: primary bit is BIT(5) of RESET_SM_READY_STATUS, with a
       fallback to BIT(0). dsi_pll_14nm.c:184-220. */
    status = mre_reg_poll32(config->pll,
                            MRE_DSI_PHY14_PLL_RESET_SM_READY_STATUS,
                            MRE_DSI_PHY14_PLL_LOCK_BIT,
                            MRE_DSI_PHY14_PLL_LOCK_BIT, timeout_us, max_polls,
                            &status_value);
    if (status != MRE_OK) {
        status = mre_reg_poll32(config->pll,
                                MRE_DSI_PHY14_PLL_RESET_SM_READY_STATUS,
                                MRE_DSI_PHY14_PLL_LOCK_FALLBACK_BIT,
                                MRE_DSI_PHY14_PLL_LOCK_FALLBACK_BIT,
                                timeout_us, max_polls, &status_value);
        if (status != MRE_OK) return MRE_E_IO;
    }
    return MRE_OK;
}

mre_status_t mre_dsi_phy14_sm6150_disable(
    const mre_dsi_phy14_sm6150_config_t *config)
{
    mre_status_t status;

    if (config == NULL || config->common == NULL) return MRE_E_INVALID;

    /* Stop the PLL (dsi_pll_14nm.c:800) then power the PHY down
       (dsi_phy_14nm.c:128-134). */
    status = mre_reg_write_verify32(config->common,
                                    MRE_DSI_PHY14_CMN_PLL_CNTRL, 0u,
                                    0xFFFFFFFFu);
    if (status != MRE_OK) return status;
    status = mre_reg_write_verify32(config->common,
                                    MRE_DSI_PHY14_CMN_GLBL_TEST_CTRL, 0u,
                                    0xFFFFFFFFu);
    if (status != MRE_OK) return status;
    return mre_reg_write_verify32(config->common, MRE_DSI_PHY14_CMN_CTRL_0,
                                  0u, 0xFFFFFFFFu);
}
