#include "mre_dsi_reg_backend.h"

mre_status_t mre_dsi_reg_backend_init(
    mre_dsi_reg_backend_t *b, const mre_reg_io_t *host,
    const mre_reg_io_t *phy, uint32_t timeout_us, uint32_t max_polls)
{
    if (b == NULL || host == NULL || phy == NULL ||
        host->ops == NULL || phy->ops == NULL ||
        timeout_us == 0u || max_polls == 0u)
        return MRE_E_INVALID;
    b->host = *host;
    b->phy = *phy;
    b->write_authorized = 0u;
    b->timeout_us = timeout_us;
    b->max_polls = max_polls;
    return MRE_OK;
}

mre_status_t mre_dsi_reg_sequence_apply(
    mre_dsi_reg_backend_t *b, const mre_reg_io_t *block,
    const mre_dsi_reg_op_t *ops, size_t count)
{
    uint32_t old_values[MRE_DSI_MAX_REG_OPS];
    size_t attempted = 0u, i;
    mre_status_t st, rollback = MRE_OK;
    if (b == NULL || block == NULL || ops == NULL ||
        block->size < 4u || count == 0u || count > MRE_DSI_MAX_REG_OPS)
        return MRE_E_INVALID;
    if (b->write_authorized == 0u) return MRE_E_PERMISSION;

    for (i = 0u; i < count; ++i) {
        if ((ops[i].offset & 3u) != 0u ||
            (uint64_t)ops[i].offset > block->size - 4u ||
            ops[i].verify_mask == 0u)
            return MRE_E_INVALID;
    }

    for (i = 0u; i < count; ++i) {
        st = mre_reg_read32(block, ops[i].offset, &old_values[i]);
        if (st != MRE_OK) goto rollback_all;
        attempted = i + 1u;
        st = mre_reg_write_verify32(block, ops[i].offset, ops[i].value,
                                    ops[i].verify_mask);
        if (st != MRE_OK) goto rollback_all;
        if (ops[i].delay_after_us != 0u &&
            block->ops->delay_us != NULL)
            block->ops->delay_us(block->context, ops[i].delay_after_us);
    }
    return MRE_OK;

rollback_all:
    while (attempted > 0u) {
        uint32_t check;
        --attempted;
        if (mre_reg_write32(block, ops[attempted].offset,
                            old_values[attempted]) != MRE_OK ||
            mre_reg_read32(block, ops[attempted].offset, &check) != MRE_OK ||
            ((check ^ old_values[attempted]) & ops[attempted].verify_mask) != 0u)
            rollback = MRE_E_IO;
    }
    return (rollback == MRE_OK) ? st : MRE_E_IO;
}

mre_status_t mre_dsi_wait_status(
    mre_dsi_reg_backend_t *b, const mre_reg_io_t *block,
    uint32_t offset, uint32_t mask, uint32_t expected,
    uint32_t *last_value)
{
    if (b == NULL || block == NULL) return MRE_E_INVALID;
    return mre_reg_poll32(block, offset, mask, expected,
                          b->timeout_us, b->max_polls, last_value);
}
