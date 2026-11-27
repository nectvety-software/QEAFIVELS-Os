/* SPDX-License-Identifier: MIT */
/* mre_reg_io.h - bounded 32-bit register window contract for MRE drives.
 *
 * Every MMIO block in drives/ is reached through this one shape, so a driver
 * never owns a raw pointer and a host test can swap a fake window in. The
 * window is read/modify/write with explicit masks: a write that is not
 * verified by a read-back is a write the driver cannot claim.
 *
 * The offsets handed to these helpers are *byte* offsets from the window base;
 * the caller supplies the base and the size. Size 0 means "the owner did not
 * declare a length", and only then are bounds not checked - every port that
 * knows its length must set it.
 */
#ifndef MRE_REG_IO_H
#define MRE_REG_IO_H

#include "mre_types.h"

typedef mre_status_t (*mre_reg_read32_fn)(void *context, uint32_t offset,
                                          uint32_t *value);
typedef mre_status_t (*mre_reg_write32_fn)(void *context, uint32_t offset,
                                           uint32_t value);
typedef void (*mre_reg_delay_us_fn)(void *context, uint32_t microseconds);
/* Monotonic microseconds; the poll loop only uses differences. May be NULL,
 * in which case the loop is bounded by max_polls alone. */
typedef uint64_t (*mre_reg_time_us_fn)(void *context);

typedef struct mre_reg_io_ops {
    mre_reg_read32_fn read32;
    mre_reg_write32_fn write32;
    mre_reg_delay_us_fn delay_us;
    mre_reg_time_us_fn time_us;
} mre_reg_io_ops_t;

typedef struct mre_reg_io {
    const mre_reg_io_ops_t *ops;
    void *context;
    uint32_t size;
} mre_reg_io_t;

/* Read into *value. MRE_E_INVALID for a NULL window/ops/value or an offset
 * outside the declared size; otherwise the ops' own status. */
mre_status_t mre_reg_read32(const mre_reg_io_t *io, uint32_t offset,
                            uint32_t *value);

/* Plain write; no read-back. Use only where the register is documented as
 * write-only (flush strobes, triggers). */
mre_status_t mre_reg_write32(const mre_reg_io_t *io, uint32_t offset,
                             uint32_t value);

/* Write, then read back and require (read ^ value) & mask == 0. A read-back
 * that disagrees is MRE_E_IO: the caller must unwind, not continue. */
mre_status_t mre_reg_write_verify32(const mre_reg_io_t *io, uint32_t offset,
                                    uint32_t value, uint32_t mask);

/* Poll until (value & mask) == expected, at most max_polls reads and at most
 * timeout_us once time_us is available. *last_value (optional) receives the
 * final read. MRE_E_BUSY when the condition never held. */
mre_status_t mre_reg_poll32(const mre_reg_io_t *io, uint32_t offset,
                            uint32_t mask, uint32_t expected,
                            uint32_t timeout_us, uint32_t max_polls,
                            uint32_t *last_value);

#endif
