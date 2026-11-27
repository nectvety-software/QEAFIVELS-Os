/* SPDX-License-Identifier: MIT */
/* mre_reg_io.c - the bounded register window helpers.
 *
 * No hardware constants live here: the owner of the block supplies the base,
 * the length and the three ops. Everything this file enforces is about the
 * access pattern - bounds, read-back verification and a bounded poll - so a
 * driver can never report a value it did not actually read.
 */
#include "mre_reg_io.h"

static mre_status_t window_check(const mre_reg_io_t *io, uint32_t offset,
                                 int need_write)
{
    if (io == NULL || io->ops == NULL) return MRE_E_INVALID;
    if (need_write) {
        if (io->ops->write32 == NULL) return MRE_E_UNSUPPORTED;
    } else if (io->ops->read32 == NULL) {
        return MRE_E_UNSUPPORTED;
    }
    if (io->size != 0u && (uint64_t)offset + 4u > io->size)
        return MRE_E_INVALID;
    return MRE_OK;
}

mre_status_t mre_reg_read32(const mre_reg_io_t *io, uint32_t offset,
                            uint32_t *value)
{
    mre_status_t st;

    if (value == NULL) return MRE_E_INVALID;
    *value = 0u;
    st = window_check(io, offset, 0);
    if (st != MRE_OK) return st;
    return io->ops->read32(io->context, offset, value);
}

mre_status_t mre_reg_write32(const mre_reg_io_t *io, uint32_t offset,
                             uint32_t value)
{
    mre_status_t st = window_check(io, offset, 1);

    if (st != MRE_OK) return st;
    return io->ops->write32(io->context, offset, value);
}

mre_status_t mre_reg_write_verify32(const mre_reg_io_t *io, uint32_t offset,
                                    uint32_t value, uint32_t mask)
{
    uint32_t readback;
    mre_status_t st;

    if (mask == 0u) return MRE_E_INVALID;
    st = window_check(io, offset, 1);
    if (st != MRE_OK) return st;
    if (io->ops->read32 == NULL) return MRE_E_UNSUPPORTED;

    st = io->ops->write32(io->context, offset, value);
    if (st != MRE_OK) return st;
    st = io->ops->read32(io->context, offset, &readback);
    if (st != MRE_OK) return st;
    if (((readback ^ value) & mask) != 0u) return MRE_E_IO;
    return MRE_OK;
}

mre_status_t mre_reg_poll32(const mre_reg_io_t *io, uint32_t offset,
                            uint32_t mask, uint32_t expected,
                            uint32_t timeout_us, uint32_t max_polls,
                            uint32_t *last_value)
{
    uint64_t start = 0u;
    uint32_t poll, value = 0u;
    mre_status_t st;

    if (last_value != NULL) *last_value = 0u;
    if (mask == 0u || max_polls == 0u) return MRE_E_INVALID;
    st = window_check(io, offset, 0);
    if (st != MRE_OK) return st;

    if (io->ops->time_us != NULL) start = io->ops->time_us(io->context);
    for (poll = 0u; poll < max_polls; ++poll) {
        st = io->ops->read32(io->context, offset, &value);
        if (st != MRE_OK) return st;
        if ((value & mask) == expected) {
            if (last_value != NULL) *last_value = value;
            return MRE_OK;
        }
        if (io->ops->time_us != NULL &&
            (io->ops->time_us(io->context) - start) >= timeout_us)
            break;
        if (io->ops->delay_us != NULL)
            io->ops->delay_us(io->context, 1u);
    }
    if (last_value != NULL) *last_value = value;
    return MRE_E_BUSY;
}
