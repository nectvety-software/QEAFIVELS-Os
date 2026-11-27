/* SPDX-License-Identifier: MIT */
/* mre_geni_i2c.h - GENI I2C master contract (SM6150).
 *
 * The bus is a register window plus two facts the platform has to state:
 * that it mapped the right resources, and that the controller really is in
 * FIFO mode. Without both, attach() refuses instead of poking a block that
 * might belong to something else.
 *
 * Register offsets stay inside mre_geni_i2c.c: callers supply behaviour
 * (read32/write32 over their own mapping), never offsets.
 */
#ifndef MRE_GENI_I2C_H
#define MRE_GENI_I2C_H

#include <stddef.h>
#include <stdint.h>

#include "mre_types.h"

typedef uint32_t (*mre_geni_i2c_read32_fn)(void *context, uint32_t offset);
typedef void (*mre_geni_i2c_write32_fn)(void *context, uint32_t offset,
                                        uint32_t value);

typedef struct mre_geni_i2c_bus {
    mre_geni_i2c_read32_fn read32;
    mre_geni_i2c_write32_fn write32;
    void *context;
    uint32_t poll_limit;          /* 0 -> 100000 */
    uint32_t attached;
    uint32_t resources_verified;  /* platform: mapping/clock/pinctrl verified */
    uint32_t fifo_mode_verified;  /* platform: controller is in FIFO mode */
} mre_geni_i2c_bus_t;

/* 0 when the bus may be used. MRE_E_PERMISSION when either platform fact is
 * missing, MRE_E_UNSUPPORTED when the block reports DMA-only mode,
 * MRE_E_BUSY when a command is already active in the controller. */
mre_status_t mre_geni_i2c_attach(mre_geni_i2c_bus_t *bus);

/* Seven-bit address; up to 256 bytes either direction; ACK is required on a
 * read. Any FIFO/IRQ error aborts the command before returning. */
mre_status_t mre_geni_i2c_write(mre_geni_i2c_bus_t *bus, uint8_t address,
                                const uint8_t *data, size_t length);

mre_status_t mre_geni_i2c_read(mre_geni_i2c_bus_t *bus, uint8_t address,
                               uint8_t *data, size_t length);

/* Register-style access: write the pointer bytes, repeated-start, read. */
mre_status_t mre_geni_i2c_write_read(mre_geni_i2c_bus_t *bus, uint8_t address,
                                     const uint8_t *tx, size_t tx_length,
                                     uint8_t *rx, size_t rx_length);

#endif
