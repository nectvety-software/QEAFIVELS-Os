#include "mre_sm6150_dispcc.h"
#include <string.h>

#define RCG_SRC_MASK       (7u << 8)
#define RCG_SRC_SHIFT      8u
#define RCG_HID_MASK       0x1fu
#define RCG_UPDATE         1u
#define BRANCH_ENABLE      1u
#define BRANCH_HALT        (1u << 31)

typedef struct clock_desc {
    uint32_t cmd_offset;
    uint32_t branch_offset;
    uint32_t parent_mask;
} clock_desc_t;

/* Offsets and parent encodings are from the SM6150 DISP_CC reference driver.
 * Runtime resource base still comes from the verified platform FDT. */
static const clock_desc_t clocks[] = {
    {0x2170u, 0x2048u, (1u << 0) | (1u << 4) | (1u << 7)},
    {0x20c0u, 0x2024u, (1u << 0) | (1u << 1) | (1u << 7)},
    {0x20dcu, 0x202cu, (1u << 0) | (1u << 1) | (1u << 7)}
};

static mre_status_t clock_descriptor(mre_dispcc_clock_id_t id,
                                      const clock_desc_t **desc)
{
    if (desc == NULL || (uint32_t)id >= MRE_ARRAY_SIZE(clocks))
        return MRE_E_INVALID;
    *desc = &clocks[id];
    return MRE_OK;
}

static mre_status_t parent_id_for(mre_dispcc_clock_id_t clock,
                                   uint32_t select,
                                   mre_dispcc_parent_id_t *parent)
{
    if (parent == NULL) return MRE_E_INVALID;
    if (select == 0u) {
        *parent = MRE_DISPCC_PARENT_BI_TCXO;
        return MRE_OK;
    }
    if (select == 7u) {
        *parent = MRE_DISPCC_PARENT_CORE_TEST;
        return MRE_OK;
    }
    if (clock == MRE_DISPCC_MDSS_AHB_SRC && select == 4u) {
        *parent = MRE_DISPCC_PARENT_GPLL0;
        return MRE_OK;
    }
    if ((clock == MRE_DISPCC_MDSS_BYTE0_SRC ||
         clock == MRE_DISPCC_MDSS_ESC0_SRC) && select == 1u) {
        *parent = MRE_DISPCC_PARENT_DSI0_BYTE;
        return MRE_OK;
    }
    return MRE_E_UNSUPPORTED;
}

mre_status_t mre_sm6150_dispcc_init(
    mre_sm6150_dispcc_t *cc, const mre_reg_io_t *regs,
    mre_dispcc_parent_rate_fn get_parent_rate, void *parent_context,
    const mre_sm6150_dispcc_policy_t *policy)
{
    if (cc == NULL || regs == NULL || policy == NULL ||
        regs->size < 0x2180u)
        return MRE_E_INVALID;
    memset(cc, 0, sizeof(*cc));
    cc->regs = *regs;
    cc->get_parent_rate = get_parent_rate;
    cc->parent_context = parent_context;
    cc->policy = *policy;
    return MRE_OK;
}

mre_status_t mre_sm6150_dispcc_read_clock(
    mre_sm6150_dispcc_t *cc, mre_dispcc_clock_id_t id,
    mre_dispcc_clock_snapshot_t *out)
{
    const clock_desc_t *d;
    uint32_t cfg, branch;
    mre_dispcc_parent_id_t parent;
    mre_status_t st;
    if (cc == NULL || out == NULL) return MRE_E_INVALID;
    st = clock_descriptor(id, &d);
    if (st != MRE_OK) return st;
    st = mre_reg_read32(&cc->regs, d->cmd_offset + 4u, &cfg);
    if (st != MRE_OK) return st;
    st = mre_reg_read32(&cc->regs, d->branch_offset, &branch);
    if (st != MRE_OK) return st;

    memset(out, 0, sizeof(*out));
    out->parent_select = (cfg & RCG_SRC_MASK) >> RCG_SRC_SHIFT;
    out->hid_div = cfg & RCG_HID_MASK;
    out->branch_enabled = (branch & BRANCH_ENABLE) != 0u;
    out->branch_halted = (branch & BRANCH_HALT) != 0u;
    st = parent_id_for(id, out->parent_select, &parent);
    if (st != MRE_OK) return st;
    if (cc->get_parent_rate == NULL) return MRE_E_NOT_FOUND;
    st = cc->get_parent_rate(cc->parent_context, parent, &out->parent_rate_hz);
    if (st != MRE_OK) return st;
    if (out->parent_rate_hz == 0u) return MRE_E_INVALID;
    if (out->parent_rate_hz > UINT64_MAX / 2u) return MRE_E_INVALID;
    out->rate_hz = (out->parent_rate_hz * 2u) /
                   ((uint64_t)out->hid_div + 1u);
    return MRE_OK;
}

mre_status_t mre_sm6150_dispcc_set_rcg(
    mre_sm6150_dispcc_t *cc, mre_dispcc_clock_id_t id,
    uint32_t parent_select, uint32_t hid_div, uint64_t expected_rate_hz,
    uint32_t timeout_us, uint32_t max_polls)
{
    const clock_desc_t *d;
    mre_dispcc_parent_id_t parent;
    uint64_t parent_rate, calculated;
    uint32_t old_cfg, new_cfg, old_cmd, check;
    mre_status_t st;
    if (cc == NULL || parent_select > 7u || hid_div > 31u ||
        expected_rate_hz == 0u || timeout_us == 0u || max_polls == 0u)
        return MRE_E_INVALID;
    if (cc->policy.resource_verified == 0u ||
        cc->policy.power_domain_verified == 0u ||
        cc->policy.write_authorized == 0u)
        return MRE_E_PERMISSION;
    st = clock_descriptor(id, &d);
    if (st != MRE_OK) return st;
    if ((d->parent_mask & (1u << parent_select)) == 0u)
        return MRE_E_UNSUPPORTED;
    st = parent_id_for(id, parent_select, &parent);
    if (st != MRE_OK) return st;
    if (cc->get_parent_rate == NULL) return MRE_E_NOT_FOUND;
    st = cc->get_parent_rate(cc->parent_context, parent, &parent_rate);
    if (st != MRE_OK) return st;
    if (parent_rate > UINT64_MAX / 2u) return MRE_E_INVALID;
    calculated = (parent_rate * 2u) / ((uint64_t)hid_div + 1u);
    if (calculated != expected_rate_hz) return MRE_E_INVALID;

    st = mre_reg_read32(&cc->regs, d->cmd_offset, &old_cmd);
    if (st != MRE_OK) return st;
    if ((old_cmd & RCG_UPDATE) != 0u) return MRE_E_BUSY;
    st = mre_reg_read32(&cc->regs, d->cmd_offset + 4u, &old_cfg);
    if (st != MRE_OK) return st;
    new_cfg = (old_cfg & ~(RCG_SRC_MASK | RCG_HID_MASK)) |
              ((parent_select << RCG_SRC_SHIFT) & RCG_SRC_MASK) |
              (hid_div & RCG_HID_MASK);
    st = mre_reg_write32(&cc->regs, d->cmd_offset + 4u, new_cfg);
    if (st != MRE_OK) {
        uint32_t restored;
        mre_status_t rb = mre_reg_write_verify32(&cc->regs,
            d->cmd_offset + 4u, old_cfg, RCG_SRC_MASK | RCG_HID_MASK);
        if (rb == MRE_OK)
            rb = mre_reg_read32(&cc->regs, d->cmd_offset + 4u, &restored);
        if (rb != MRE_OK ||
            (restored & (RCG_SRC_MASK | RCG_HID_MASK)) !=
            (old_cfg & (RCG_SRC_MASK | RCG_HID_MASK)))
            return MRE_E_IO;
        return st;
    }
    st = mre_reg_write32(&cc->regs, d->cmd_offset, old_cmd | RCG_UPDATE);
    if (st == MRE_OK)
        st = mre_reg_poll32(&cc->regs, d->cmd_offset, RCG_UPDATE, 0u,
                            timeout_us, max_polls, NULL);
    if (st == MRE_OK)
        st = mre_reg_read32(&cc->regs, d->cmd_offset + 4u, &check);
    if (st == MRE_OK &&
        (check & (RCG_SRC_MASK | RCG_HID_MASK)) ==
        (new_cfg & (RCG_SRC_MASK | RCG_HID_MASK)))
        return MRE_OK;

    {
        uint32_t restored;
        mre_status_t rb = mre_reg_write32(&cc->regs, d->cmd_offset + 4u, old_cfg);
        if (rb == MRE_OK)
            rb = mre_reg_write32(&cc->regs, d->cmd_offset, old_cmd | RCG_UPDATE);
        if (rb == MRE_OK)
            rb = mre_reg_poll32(&cc->regs, d->cmd_offset, RCG_UPDATE, 0u,
                                timeout_us, max_polls, NULL);
        if (rb == MRE_OK)
            rb = mre_reg_read32(&cc->regs, d->cmd_offset + 4u, &restored);
        if (rb != MRE_OK ||
            (restored & (RCG_SRC_MASK | RCG_HID_MASK)) !=
            (old_cfg & (RCG_SRC_MASK | RCG_HID_MASK)))
            return MRE_E_IO;
    }
    return (st == MRE_OK) ? MRE_E_IO : st;
}

mre_status_t mre_sm6150_dispcc_set_branch(
    mre_sm6150_dispcc_t *cc, uint32_t offset, uint32_t enable,
    uint32_t timeout_us, uint32_t max_polls)
{
    uint32_t old_value, new_value, expected_halt;
    mre_status_t st;
    if (cc == NULL || enable > 1u || timeout_us == 0u || max_polls == 0u)
        return MRE_E_INVALID;
    if (cc->policy.resource_verified == 0u ||
        cc->policy.power_domain_verified == 0u ||
        cc->policy.write_authorized == 0u)
        return MRE_E_PERMISSION;
    if (offset != 0x2048u && offset != 0x2024u &&
        offset != 0x2028u && offset != 0x202cu)
        return MRE_E_PERMISSION;
    st = mre_reg_read32(&cc->regs, offset, &old_value);
    if (st != MRE_OK) return st;
    new_value = enable ? (old_value | BRANCH_ENABLE)
                       : (old_value & ~BRANCH_ENABLE);
    st = mre_reg_write_verify32(&cc->regs, offset, new_value, BRANCH_ENABLE);
    if (st != MRE_OK) {
        mre_status_t rb = mre_reg_write_verify32(&cc->regs, offset,
                                                  old_value, BRANCH_ENABLE);
        if (rb == MRE_OK)
            rb = mre_reg_poll32(&cc->regs, offset, BRANCH_HALT,
                                old_value & BRANCH_HALT,
                                timeout_us, max_polls, NULL);
        return (rb == MRE_OK) ? st : MRE_E_IO;
    }
    expected_halt = enable ? 0u : BRANCH_HALT;
    st = mre_reg_poll32(&cc->regs, offset, BRANCH_HALT, expected_halt,
                        timeout_us, max_polls, NULL);
    if (st != MRE_OK) {
        mre_status_t rb = mre_reg_write_verify32(&cc->regs, offset,
                                                  old_value, BRANCH_ENABLE);
        if (rb == MRE_OK)
            rb = mre_reg_poll32(&cc->regs, offset, BRANCH_HALT,
                                old_value & BRANCH_HALT,
                                timeout_us, max_polls, NULL);
        return (rb == MRE_OK) ? st : MRE_E_IO;
    }
    return MRE_OK;
}
