#ifndef MRE_SM6150_DISPCC_H
#define MRE_SM6150_DISPCC_H

#include "mre_reg_io.h"

typedef enum mre_dispcc_clock_id {
    MRE_DISPCC_MDSS_AHB_SRC = 0,
    MRE_DISPCC_MDSS_BYTE0_SRC,
    MRE_DISPCC_MDSS_ESC0_SRC
} mre_dispcc_clock_id_t;

typedef enum mre_dispcc_parent_id {
    MRE_DISPCC_PARENT_BI_TCXO = 0,
    MRE_DISPCC_PARENT_DSI0_BYTE,
    MRE_DISPCC_PARENT_GPLL0,
    MRE_DISPCC_PARENT_CORE_TEST
} mre_dispcc_parent_id_t;

typedef mre_status_t (*mre_dispcc_parent_rate_fn)(
    void *context, mre_dispcc_parent_id_t parent, uint64_t *rate_hz);

typedef struct mre_sm6150_dispcc_policy {
    uint32_t resource_verified;
    uint32_t power_domain_verified;
    uint32_t write_authorized;
} mre_sm6150_dispcc_policy_t;

typedef struct mre_sm6150_dispcc {
    mre_reg_io_t regs;
    mre_dispcc_parent_rate_fn get_parent_rate;
    void *parent_context;
    mre_sm6150_dispcc_policy_t policy;
} mre_sm6150_dispcc_t;

typedef struct mre_dispcc_clock_snapshot {
    uint32_t parent_select;
    uint32_t hid_div;
    uint32_t branch_enabled;
    uint32_t branch_halted;
    uint64_t parent_rate_hz;
    uint64_t rate_hz;
} mre_dispcc_clock_snapshot_t;

mre_status_t mre_sm6150_dispcc_init(
    mre_sm6150_dispcc_t *cc, const mre_reg_io_t *regs,
    mre_dispcc_parent_rate_fn get_parent_rate, void *parent_context,
    const mre_sm6150_dispcc_policy_t *policy);

mre_status_t mre_sm6150_dispcc_read_clock(
    mre_sm6150_dispcc_t *cc, mre_dispcc_clock_id_t clock,
    mre_dispcc_clock_snapshot_t *snapshot);

mre_status_t mre_sm6150_dispcc_set_rcg(
    mre_sm6150_dispcc_t *cc, mre_dispcc_clock_id_t clock,
    uint32_t parent_select, uint32_t hid_div, uint64_t expected_rate_hz,
    uint32_t timeout_us, uint32_t max_polls);

mre_status_t mre_sm6150_dispcc_set_branch(
    mre_sm6150_dispcc_t *cc, uint32_t branch_offset, uint32_t enable,
    uint32_t timeout_us, uint32_t max_polls);

#endif
