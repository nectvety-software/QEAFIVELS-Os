/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_WLFW_CLIENT_H
#define VQEAF_WLFW_CLIENT_H
#include <stddef.h>
#include <stdint.h>
#include "qmi_service.h"

/* WCN3990 WLFW QMI service. These are public wire-protocol constants; the
 * implementation is independent and does not copy Linux or vendor code. */
#define VQEAF_WLFW_SERVICE       0x45u
#define VQEAF_WLFW_CLIENT_ID     0x4b4e454cu
#define VQEAF_WLFW_SEG_MAX       6144u
#define VQEAF_WLFW_CAL_MAX       5u

#define VQEAF_WLFW_IND_REGISTER  0x0020u
#define VQEAF_WLFW_FW_READY_IND  0x0021u
#define VQEAF_WLFW_CAP           0x0024u
#define VQEAF_WLFW_BDF_DOWNLOAD  0x0025u
#define VQEAF_WLFW_CAL_REPORT    0x0026u
#define VQEAF_WLFW_CAL_DOWNLOAD  0x0027u
#define VQEAF_WLFW_INIT_CAL_DL   0x0028u
#define VQEAF_WLFW_CAL_UPDATE    0x0029u
#define VQEAF_WLFW_INIT_CAL_UP   0x002Au
#define VQEAF_WLFW_MSA_READY_IND 0x002Bu
#define VQEAF_WLFW_MAC_ADDR      0x0033u
#define VQEAF_WLFW_MEM_READY_IND 0x0037u
#define VQEAF_WLFW_FW_INIT_IND   0x0038u
#define VQEAF_WLFW_XO_CAL_IND    0x003Du

#define VQEAF_WLFW_STATUS_ALREADY_REGISTERED 0x01u
#define VQEAF_WLFW_STATUS_FW_READY           0x02u
#define VQEAF_WLFW_STATUS_MSA_READY           0x04u
#define VQEAF_WLFW_STATUS_MEM_READY           0x08u
#define VQEAF_WLFW_STATUS_FW_INIT_DONE        0x10u

#define VQEAF_WLFW_BDF_TYPE_BIN   0
#define VQEAF_WLFW_BDF_TYPE_REGDB 4
#define VQEAF_WLFW_BDF_TYPE_NONE  (-1)

/* FW_READY is only the WLFW firmware-control indication. It does not prove
 * HTC/WMI, netdev, RF, association or IP connectivity. */
typedef enum {
    VQEAF_WLFW_NEW = 0,
    VQEAF_WLFW_REGISTERED,
    VQEAF_WLFW_MSA_INFO_SENT,
    VQEAF_WLFW_MSA_READY_SENT,
    VQEAF_WLFW_CAP_READY,
    VQEAF_WLFW_BDF_SENT,
    VQEAF_WLFW_CAL_REPORTED,
    VQEAF_WLFW_FW_READY,
    VQEAF_WLFW_FAILED
} vqeaf_wlfw_state_t;

typedef struct {
    uint32_t chip_id;
    uint32_t chip_family;
    uint32_t board_id;
    uint32_t soc_id;
    uint8_t num_macs;
} vqeaf_wlfw_caps_t;

typedef struct {
    vqeaf_qmi_client_t qmi;
    vqeaf_qrtr_addr_t server;
    vqeaf_wlfw_state_t state;
    vqeaf_wlfw_caps_t caps;
    uint64_t fw_status;
    uint32_t last_qmi_error;
    size_t response_len;
    int msa_ready;
    int fw_ready;
    void (*event)(void *ctx, uint16_t msg_id);
    void *event_ctx;
    uint8_t scratch[VQEAF_QMI_BUF_SIZE];
} vqeaf_wlfw_client_t;

const char *vqeaf_wlfw_state_name(vqeaf_wlfw_state_t state);
void vqeaf_wlfw_client_init(vqeaf_wlfw_client_t *c,
                            const vqeaf_qrtr_io_t *io,
                            const vqeaf_qrtr_addr_t *server,
                            void (*event)(void *ctx, uint16_t msg_id),
                            void *event_ctx);

int vqeaf_wlfw_connect(vqeaf_wlfw_client_t *c, uint32_t timeout_ms,
                       uint8_t retries);
int vqeaf_wlfw_msa_info(vqeaf_wlfw_client_t *c, uint64_t addr,
                        uint32_t size, uint32_t timeout_ms);
int vqeaf_wlfw_msa_ready(vqeaf_wlfw_client_t *c, uint32_t timeout_ms);
int vqeaf_wlfw_wait_msa_ready(vqeaf_wlfw_client_t *c, uint32_t timeout_ms);
int vqeaf_wlfw_cap(vqeaf_wlfw_client_t *c, uint32_t timeout_ms);
int vqeaf_wlfw_download(vqeaf_wlfw_client_t *c, uint16_t msg_id,
                        const uint8_t *data, size_t len, int32_t file_id,
                        int bdf_type, uint32_t timeout_ms);
int vqeaf_wlfw_cal_report(vqeaf_wlfw_client_t *c, const int32_t *ids,
                          size_t count, uint32_t timeout_ms);
int vqeaf_wlfw_cal_update(vqeaf_wlfw_client_t *c, int32_t cal_id,
                          uint32_t seg_id, uint8_t *data, size_t cap,
                          size_t *len, int *end, uint32_t timeout_ms);
int vqeaf_wlfw_send_mac(vqeaf_wlfw_client_t *c, const uint8_t mac[6],
                        uint32_t timeout_ms);
int vqeaf_wlfw_wait_fw_ready(vqeaf_wlfw_client_t *c, uint32_t timeout_ms);

#endif
