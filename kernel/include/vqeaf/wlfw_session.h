/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_WLFW_SESSION_H
#define VQEAF_WLFW_SESSION_H
#include <stddef.h>
#include <stdint.h>
#include "vqeaf/wlfw_client.h"

typedef struct {
    int32_t file_id;
    const uint8_t *data;
    size_t size;
} vqeaf_wlfw_calibration_t;

typedef struct {
    const vqeaf_qrtr_io_t *io;
    vqeaf_qrtr_addr_t server;
    uint64_t msa_address;
    uint32_t msa_size;
    const uint8_t *bdf;
    size_t bdf_size;
    int32_t bdf_file_id;
    const uint8_t *regdb;
    size_t regdb_size;
    int32_t regdb_file_id;
    const vqeaf_wlfw_calibration_t *calibration;
    size_t calibration_count;
    const uint8_t *factory_mac; /* read from this device's authenticated persist */
    uint32_t timeout_ms;
    uint8_t retries;
} vqeaf_wlfw_session_request_t;

typedef struct {
    vqeaf_wlfw_state_t state;
    vqeaf_wlfw_caps_t caps;
    uint32_t completed_calibrations;
    int last_error;
    uint8_t fw_ready;
} vqeaf_wlfw_session_result_t;

/* Preflight is side-effect free: no QMI packets, PMIC operations or device
 * writes. It rejects missing per-device calibration or factory MAC. */
int vqeaf_wlfw_session_validate(const vqeaf_wlfw_session_request_t *request);
/* Starts WLFW protocol only after firmware/ICNSS are already confirmed ready.
 * This does not power on WLAN. Caller owns the client and hardware lifecycle. */
int vqeaf_wlfw_session_run(const vqeaf_wlfw_session_request_t *request,
                           vqeaf_wlfw_client_t *client,
                           vqeaf_wlfw_session_result_t *result);

#endif
