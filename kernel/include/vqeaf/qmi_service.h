/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_QMI_SERVICE_H
#define VQEAF_QMI_SERVICE_H
#include <stdint.h>
#include <stddef.h>
#include "qrtr_transport.h"

/* Minimal native QMI client. Independent of the BSD-licensed Linaro codec in
 * platform/violet/hosttools; no code is copied from it. Implements only the
 * wire primitives a QMI client needs: 7-byte header, TLVs, transaction IDs,
 * request/response matching, bounded timeout and retry. */

#define VQEAF_QMI_REQ        0u
#define VQEAF_QMI_RESP       2u
#define VQEAF_QMI_IND        4u

#define VQEAF_QMI_HDR_SIZE   7u
#define VQEAF_QMI_TLV_HDR    3u

#define VQEAF_QMI_RESULT_SUCCESS 0u
#define VQEAF_QMI_RESULT_FAILURE 1u

typedef struct {
    uint8_t  type;
    uint16_t txn_id;
    uint16_t msg_id;
    uint16_t len;
} vqeaf_qmi_header_t;

typedef struct {
    uint16_t result;
    uint16_t error;
} vqeaf_qmi_result_t;

typedef enum {
    VQEAF_QMI_LINK_DOWN = 0,
    VQEAF_QMI_LINK_UP,
    VQEAF_QMI_LINK_RESET
} vqeaf_qmi_link_t;

#define VQEAF_QMI_MAX_PENDING 8u
/* WLFW permits 6144-byte data segments plus TLVs and the QMI header. */
#define VQEAF_QMI_BUF_SIZE   6400u

typedef struct {
    uint16_t txn_id;
    uint16_t msg_id;
    uint64_t deadline_ms;
    uint8_t  retries_left;
    uint8_t  used;
} vqeaf_qmi_pending_t;

typedef void (*vqeaf_qmi_indication_fn)(void *ctx, uint16_t msg_id,
                                        const uint8_t *payload,
                                        size_t payload_len);

typedef struct {
    const vqeaf_qrtr_io_t *io;
    uint64_t (*now_ms)(void *ctx);
    uint16_t next_txn;
    vqeaf_qmi_pending_t pending[VQEAF_QMI_MAX_PENDING];
    vqeaf_qmi_link_t link;
    uint32_t sent, received, timeouts, bad_responses, indications, resets;
    vqeaf_qmi_indication_fn indication;
    void *indication_ctx;
    /* Static I/O buffers: no malloc, and the client is used by one owner. */
    uint8_t tx_buf[VQEAF_QMI_BUF_SIZE];
    uint8_t rx_buf[VQEAF_QMI_BUF_SIZE];
} vqeaf_qmi_client_t;

/* Header / TLV codecs. All lengths are checked; no dynamic allocation. */
int vqeaf_qmi_header_encode(uint8_t *buf, size_t cap,
                            const vqeaf_qmi_header_t *h);
int vqeaf_qmi_header_decode(const uint8_t *buf, size_t len,
                            vqeaf_qmi_header_t *h);
int vqeaf_qmi_tlv_put(uint8_t *buf, size_t cap, uint8_t type,
                      const uint8_t *data, size_t len);
int vqeaf_qmi_tlv_next(const uint8_t *buf, size_t len, size_t *off,
                       uint8_t *type, const uint8_t **data, size_t *data_len);

/* Common result TLV (type 0, two u16 LE: result, error). */
int vqeaf_qmi_result_decode(const uint8_t *buf, size_t len,
                            vqeaf_qmi_result_t *out);
const char *vqeaf_qmi_error_name(uint16_t err);

/* Client lifecycle. */
void vqeaf_qmi_client_init(vqeaf_qmi_client_t *c, const vqeaf_qrtr_io_t *io);
void vqeaf_qmi_client_reset(vqeaf_qmi_client_t *c, vqeaf_qmi_link_t reason);
int  vqeaf_qmi_client_link(const vqeaf_qmi_client_t *c);
/* The callback receives only bytes actually read from the transport. Its
 * payload pointer is valid for the callback duration; it must not re-enter the
 * client. The handler survives reset because it belongs to the owner. */
void vqeaf_qmi_client_set_indication_handler(vqeaf_qmi_client_t *c,
                                             vqeaf_qmi_indication_fn fn,
                                             void *ctx);
/* Waits for one matching indication and returns its complete wire packet in
 * out (header included). Non-matching packets are consumed, never synthesized.
 * Returns -ETIMEDOUT, -ENODEV/-ENETRESET, -EMSGSIZE or -EINVAL as appropriate. */
int vqeaf_qmi_wait_indication(vqeaf_qmi_client_t *c, uint16_t msg_id,
                              uint8_t *out, size_t out_cap, size_t *out_len,
                              uint32_t timeout_ms);

/* Sends a request to dst and waits for the matching response. Response bytes
 * land in out (header included). Returns 0 with *out_len set, or a negative
 * errno: -ETIMEDOUT (no reply after retries), -EPROTO (malformed/mismatched
 * response), -ENODEV/-ENETRESET (transport reset), -EAGAIN (table full).
 * Indications and responses for other transactions are consumed, never
 * mistaken for this request's answer. */
int vqeaf_qmi_request(vqeaf_qmi_client_t *c, const vqeaf_qrtr_addr_t *dst,
                      uint16_t msg_id, const uint8_t *payload,
                      size_t payload_len, uint8_t *out, size_t out_cap,
                      size_t *out_len, uint32_t timeout_ms,
                      uint8_t retries);

#endif
