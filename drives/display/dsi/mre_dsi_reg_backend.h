#ifndef MRE_DSI_REG_BACKEND_H
#define MRE_DSI_REG_BACKEND_H

#include "mre_reg_io.h"

#define MRE_DSI_MAX_REG_OPS 64u

typedef struct mre_dsi_reg_op {
    uint32_t offset;
    uint32_t value;
    uint32_t verify_mask;
    uint32_t delay_after_us;
} mre_dsi_reg_op_t;

typedef struct mre_dsi_reg_backend {
    mre_reg_io_t host;
    mre_reg_io_t phy;
    uint32_t write_authorized;
    uint32_t timeout_us;
    uint32_t max_polls;
} mre_dsi_reg_backend_t;

mre_status_t mre_dsi_reg_backend_init(
    mre_dsi_reg_backend_t *backend, const mre_reg_io_t *host,
    const mre_reg_io_t *phy, uint32_t timeout_us, uint32_t max_polls);

mre_status_t mre_dsi_reg_sequence_apply(
    mre_dsi_reg_backend_t *backend, const mre_reg_io_t *block,
    const mre_dsi_reg_op_t *ops, size_t count);

mre_status_t mre_dsi_wait_status(
    mre_dsi_reg_backend_t *backend, const mre_reg_io_t *block,
    uint32_t offset, uint32_t mask, uint32_t expected,
    uint32_t *last_value);

#endif
