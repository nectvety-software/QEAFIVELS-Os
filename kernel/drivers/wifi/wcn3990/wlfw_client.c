/* SPDX-License-Identifier: MIT */
/* Independent WCN3990 WLFW QMI client. Only received packets advance state. */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "vqeaf/wlfw_client.h"

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_le64(const uint8_t *p)
{
    return (uint64_t)get_le32(p) | ((uint64_t)get_le32(p + 4) << 32);
}

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put_le64(uint8_t *p, uint64_t v)
{
    put_le32(p, (uint32_t)v);
    put_le32(p + 4, (uint32_t)(v >> 32));
}

static int put_u8(uint8_t *buf, size_t cap, size_t *off, uint8_t type,
                  uint8_t value)
{
    int n = vqeaf_qmi_tlv_put(buf + *off, cap - *off, type, &value, 1);
    if(n < 0) return n;
    *off += (size_t)n;
    return 0;
}

static int put_u32(uint8_t *buf, size_t cap, size_t *off, uint8_t type,
                   uint32_t value)
{
    uint8_t data[4];
    put_le32(data, value);
    int n = vqeaf_qmi_tlv_put(buf + *off, cap - *off, type, data, sizeof(data));
    if(n < 0) return n;
    *off += (size_t)n;
    return 0;
}

static int put_u64(uint8_t *buf, size_t cap, size_t *off, uint8_t type,
                   uint64_t value)
{
    uint8_t data[8];
    put_le64(data, value);
    int n = vqeaf_qmi_tlv_put(buf + *off, cap - *off, type, data, sizeof(data));
    if(n < 0) return n;
    *off += (size_t)n;
    return 0;
}

static int find_tlv(const uint8_t *buf, size_t len, uint8_t wanted,
                    const uint8_t **data, size_t *data_len)
{
    size_t off = 0;
    int found = 0;
    while(off < len) {
        uint8_t type;
        const uint8_t *value;
        size_t dl;
        if(vqeaf_qmi_tlv_next(buf, len, &off, &type, &value, &dl) != 0)
            return -EPROTO;
        if(type == wanted) {
            *data = value;
            *data_len = dl;
            found = 1;
        }
    }
    return found ? 0 : -ENOENT;
}

static int decode_result(const uint8_t *buf, size_t len,
                         vqeaf_qmi_result_t *out)
{
    const uint8_t *value;
    size_t value_len;
    int rc = find_tlv(buf, len, 0x00, &value, &value_len);
    if(rc != 0) return rc;
    if(value_len < 4u) return -EPROTO;
    out->result = get_le16(value);
    out->error = get_le16(value + 2);
    return 0;
}

static int fail(vqeaf_wlfw_client_t *c, int rc)
{
    c->state = VQEAF_WLFW_FAILED;
    return rc ? rc : -EIO;
}

static int request(vqeaf_wlfw_client_t *c, uint16_t msg_id,
                   const uint8_t *payload, size_t payload_len,
                   uint32_t timeout_ms)
{
    vqeaf_qmi_result_t result;
    size_t out_len = 0;
    int rc = vqeaf_qmi_request(&c->qmi, &c->server, msg_id, payload,
                               payload_len, c->scratch, sizeof(c->scratch),
                               &out_len, timeout_ms, 1);
    if(rc) return fail(c, rc);
    c->response_len = out_len;
    if(out_len < VQEAF_QMI_HDR_SIZE ||
       decode_result(c->scratch + VQEAF_QMI_HDR_SIZE,
                     out_len - VQEAF_QMI_HDR_SIZE, &result) != 0)
        return fail(c, -EPROTO);
    if(result.result != VQEAF_QMI_RESULT_SUCCESS) {
        c->last_qmi_error = result.error;
        return fail(c, -EPROTO);
    }
    c->last_qmi_error = 0;
    return 0;
}

static void on_indication(void *ctx, uint16_t msg_id,
                          const uint8_t *payload, size_t payload_len)
{
    vqeaf_wlfw_client_t *c = (vqeaf_wlfw_client_t *)ctx;
    (void)payload;
    if(c->state == VQEAF_WLFW_NEW || c->state == VQEAF_WLFW_FAILED) {
        if(c->event != NULL) c->event(c->event_ctx, msg_id);
        return;
    }
    if(payload_len == 0) {
        if(msg_id == VQEAF_WLFW_MSA_READY_IND) c->msa_ready = 1;
        if(msg_id == VQEAF_WLFW_FW_READY_IND) {
            c->fw_ready = 1;
            c->state = VQEAF_WLFW_FW_READY;
        }
    }
    if(c->event != NULL) c->event(c->event_ctx, msg_id);
}

const char *vqeaf_wlfw_state_name(vqeaf_wlfw_state_t state)
{
    switch(state) {
    case VQEAF_WLFW_NEW:           return "NEW";
    case VQEAF_WLFW_REGISTERED:    return "REGISTERED";
    case VQEAF_WLFW_MSA_INFO_SENT: return "MSA_INFO_SENT";
    case VQEAF_WLFW_MSA_READY_SENT:return "MSA_READY_SENT";
    case VQEAF_WLFW_CAP_READY:     return "CAP_READY";
    case VQEAF_WLFW_BDF_SENT:      return "BDF_SENT";
    case VQEAF_WLFW_CAL_REPORTED:  return "CAL_REPORTED";
    case VQEAF_WLFW_FW_READY:      return "FW_READY";
    case VQEAF_WLFW_FAILED:        return "FAILED";
    default:                       return "UNKNOWN";
    }
}

void vqeaf_wlfw_client_init(vqeaf_wlfw_client_t *c,
                            const vqeaf_qrtr_io_t *io,
                            const vqeaf_qrtr_addr_t *server,
                            void (*event)(void *ctx, uint16_t msg_id),
                            void *event_ctx)
{
    if(c == NULL) return;
    memset(c, 0, sizeof(*c));
    vqeaf_qmi_client_init(&c->qmi, io);
    vqeaf_qmi_client_set_indication_handler(&c->qmi, on_indication, c);
    if(server != NULL) c->server = *server;
    c->event = event;
    c->event_ctx = event_ctx;
}

int vqeaf_wlfw_connect(vqeaf_wlfw_client_t *c, uint32_t timeout_ms,
                       uint8_t retries)
{
    uint8_t one = 1;
    size_t off = 0;
    size_t out_len = 0;
    vqeaf_qmi_result_t result;
    const uint8_t *status;
    size_t status_len;
    int rc;

    if(c == NULL || c->qmi.io == NULL || c->server.node == 0 ||
       c->server.port == 0 || timeout_ms == 0) return -EINVAL;
    c->fw_ready = 0;
    c->msa_ready = 0;
    c->state = VQEAF_WLFW_NEW;
    memset(&c->caps, 0, sizeof(c->caps));
    c->last_qmi_error = 0;
    c->response_len = 0;
    vqeaf_qmi_client_reset(&c->qmi, VQEAF_QMI_LINK_UP);
    rc = put_u8(c->scratch, sizeof(c->scratch), &off, 0x10, one);
    if(rc) return fail(c, rc);
    rc = put_u8(c->scratch, sizeof(c->scratch), &off, 0x11, one);
    if(rc) return fail(c, rc);
    rc = put_u8(c->scratch, sizeof(c->scratch), &off, 0x12, one);
    if(rc) return fail(c, rc);
    rc = put_u8(c->scratch, sizeof(c->scratch), &off, 0x13, one);
    if(rc) return fail(c, rc);
    rc = put_u32(c->scratch, sizeof(c->scratch), &off, 0x15,
                 VQEAF_WLFW_CLIENT_ID);
    if(rc) return fail(c, rc);
    rc = put_u8(c->scratch, sizeof(c->scratch), &off, 0x17, one);
    if(rc) return fail(c, rc);
    rc = put_u8(c->scratch, sizeof(c->scratch), &off, 0x18, one);
    if(rc) return fail(c, rc);
    rc = put_u8(c->scratch, sizeof(c->scratch), &off, 0x1A, one);
    if(rc) return fail(c, rc);

    rc = vqeaf_qmi_request(&c->qmi, &c->server, VQEAF_WLFW_IND_REGISTER,
                           c->scratch, off, c->scratch, sizeof(c->scratch),
                           &out_len, timeout_ms, retries);
    if(rc) return fail(c, rc);
    if(out_len < VQEAF_QMI_HDR_SIZE ||
       decode_result(c->scratch + VQEAF_QMI_HDR_SIZE,
                     out_len - VQEAF_QMI_HDR_SIZE, &result) != 0)
        return fail(c, -EPROTO);
    if(result.result != VQEAF_QMI_RESULT_SUCCESS) {
        c->last_qmi_error = result.error;
        return fail(c, -EPROTO);
    }
    c->fw_status = 0;
    rc = find_tlv(c->scratch + VQEAF_QMI_HDR_SIZE,
                  out_len - VQEAF_QMI_HDR_SIZE, 0x10, &status, &status_len);
    if(rc == 0) {
        if(status_len != 8u) return fail(c, -EPROTO);
        c->fw_status = get_le64(status);
    } else if(rc != -ENOENT) {
        return fail(c, rc);
    }
    c->msa_ready = c->msa_ready ||
                   (c->fw_status & VQEAF_WLFW_STATUS_MSA_READY) != 0;
    c->fw_ready = c->fw_ready ||
                  (c->fw_status & VQEAF_WLFW_STATUS_FW_READY) != 0;
    c->state = c->fw_ready ? VQEAF_WLFW_FW_READY : VQEAF_WLFW_REGISTERED;
    c->last_qmi_error = 0;
    return 0;
}

int vqeaf_wlfw_msa_info(vqeaf_wlfw_client_t *c, uint64_t addr,
                        uint32_t size, uint32_t timeout_ms)
{
    size_t off = 0;
    int rc;
    if(c == NULL || addr == 0 || size == 0 || timeout_ms == 0) return -EINVAL;
    if(c->state != VQEAF_WLFW_REGISTERED) return fail(c, -EINVAL);
    rc = put_u64(c->scratch, sizeof(c->scratch), &off, 0x01, addr);
    if(rc) return fail(c, rc);
    rc = put_u32(c->scratch, sizeof(c->scratch), &off, 0x02, size);
    if(rc) return fail(c, rc);
    rc = request(c, 0x002Du, c->scratch, off, timeout_ms);
    if(rc == 0 && !c->fw_ready) c->state = VQEAF_WLFW_MSA_INFO_SENT;
    return rc;
}

int vqeaf_wlfw_msa_ready(vqeaf_wlfw_client_t *c, uint32_t timeout_ms)
{
    int rc;
    if(c == NULL || timeout_ms == 0) return -EINVAL;
    if(c->state != VQEAF_WLFW_MSA_INFO_SENT) return fail(c, -EINVAL);
    rc = request(c, 0x002Eu, NULL, 0, timeout_ms);
    if(rc == 0 && !c->fw_ready) c->state = VQEAF_WLFW_MSA_READY_SENT;
    return rc;
}

int vqeaf_wlfw_wait_msa_ready(vqeaf_wlfw_client_t *c, uint32_t timeout_ms)
{
    size_t out_len = 0;
    int rc;
    if(c == NULL || timeout_ms == 0) return -EINVAL;
    if(c->msa_ready) return 0;
    if(c->state != VQEAF_WLFW_MSA_READY_SENT &&
       c->state != VQEAF_WLFW_CAP_READY) return fail(c, -EINVAL);
    rc = vqeaf_qmi_wait_indication(&c->qmi, VQEAF_WLFW_MSA_READY_IND,
                                   c->scratch, sizeof(c->scratch), &out_len,
                                   timeout_ms);
    if(rc || !c->msa_ready) return fail(c, rc ? rc : -EPROTO);
    return 0;
}

int vqeaf_wlfw_cap(vqeaf_wlfw_client_t *c, uint32_t timeout_ms)
{
    const uint8_t *value;
    size_t len, out_len = 0;
    vqeaf_qmi_result_t result;
    int rc;
    if(c == NULL || timeout_ms == 0) return -EINVAL;
    if(c->state != VQEAF_WLFW_MSA_READY_SENT &&
       c->state != VQEAF_WLFW_CAP_READY) return fail(c, -EINVAL);
    rc = vqeaf_qmi_request(&c->qmi, &c->server, VQEAF_WLFW_CAP, NULL, 0,
                           c->scratch, sizeof(c->scratch), &out_len,
                           timeout_ms, 1);
    if(rc) return fail(c, rc);
    if(out_len < VQEAF_QMI_HDR_SIZE ||
       decode_result(c->scratch + VQEAF_QMI_HDR_SIZE,
                     out_len - VQEAF_QMI_HDR_SIZE, &result) != 0)
        return fail(c, -EPROTO);
    if(result.result != VQEAF_QMI_RESULT_SUCCESS) {
        c->last_qmi_error = result.error;
        return fail(c, -EPROTO);
    }
    rc = find_tlv(c->scratch + VQEAF_QMI_HDR_SIZE,
                  out_len - VQEAF_QMI_HDR_SIZE, 0x11, &value, &len);
    if(rc != 0 || len != 4u) return fail(c, rc == 0 ? -EPROTO : rc);
    memset(&c->caps, 0, sizeof(c->caps));
    c->caps.board_id = get_le32(value);
    rc = find_tlv(c->scratch + VQEAF_QMI_HDR_SIZE,
                  out_len - VQEAF_QMI_HDR_SIZE, 0x10, &value, &len);
    if(rc == 0) {
        if(len != 8u) return fail(c, -EPROTO);
        c->caps.chip_id = get_le32(value);
        c->caps.chip_family = get_le32(value + 4);
    } else if(rc != -ENOENT) {
        return fail(c, rc);
    }
    rc = find_tlv(c->scratch + VQEAF_QMI_HDR_SIZE,
                  out_len - VQEAF_QMI_HDR_SIZE, 0x12, &value, &len);
    if(rc == 0) {
        if(len != 4u) return fail(c, -EPROTO);
        c->caps.soc_id = get_le32(value);
    } else if(rc != -ENOENT) {
        return fail(c, rc);
    }
    rc = find_tlv(c->scratch + VQEAF_QMI_HDR_SIZE,
                  out_len - VQEAF_QMI_HDR_SIZE, 0x15, &value, &len);
    if(rc == 0) {
        if(len != 1u) return fail(c, -EPROTO);
        c->caps.num_macs = value[0];
    } else if(rc != -ENOENT) {
        return fail(c, rc);
    }
    if(!c->fw_ready) c->state = VQEAF_WLFW_CAP_READY;
    c->last_qmi_error = 0;
    return 0;
}

int vqeaf_wlfw_download(vqeaf_wlfw_client_t *c, uint16_t msg_id,
                        const uint8_t *data, size_t len, int32_t file_id,
                        int bdf_type, uint32_t timeout_ms)
{
    size_t sent = 0;
    uint32_t seg = 0;
    uint8_t one = 1;
    if(c == NULL || timeout_ms == 0) return -EINVAL;
    if(msg_id != VQEAF_WLFW_BDF_DOWNLOAD &&
       msg_id != VQEAF_WLFW_CAL_DOWNLOAD) return fail(c, -EINVAL);
    if(data == NULL || len == 0 || len > UINT32_MAX ||
       bdf_type < VQEAF_WLFW_BDF_TYPE_NONE || bdf_type > 255)
        return fail(c, len == 0 ? -ENODATA : -EINVAL);
    if(msg_id == VQEAF_WLFW_BDF_DOWNLOAD &&
       c->state != VQEAF_WLFW_CAP_READY &&
       c->state != VQEAF_WLFW_BDF_SENT) return fail(c, -EINVAL);

    while(sent < len) {
        size_t chunk = len - sent > VQEAF_WLFW_SEG_MAX ?
                       VQEAF_WLFW_SEG_MAX : len - sent;
        size_t off = 0;
        uint8_t end = sent + chunk == len ? 1 : 0;
        int rc;
        rc = put_u8(c->scratch, sizeof(c->scratch), &off, 0x01, one);
        if(rc) return fail(c, rc);
        rc = put_u32(c->scratch, sizeof(c->scratch), &off, 0x10,
                     (uint32_t)file_id);
        if(rc) return fail(c, rc);
        rc = put_u32(c->scratch, sizeof(c->scratch), &off, 0x11,
                     (uint32_t)len);
        if(rc) return fail(c, rc);
        rc = put_u32(c->scratch, sizeof(c->scratch), &off, 0x12, seg);
        if(rc) return fail(c, rc);
        if(chunk + 5u > sizeof(c->scratch) - off) return fail(c, -EMSGSIZE);
        c->scratch[off] = 0x13;
        put_le16(c->scratch + off + 1, (uint16_t)(chunk + 2u));
        put_le16(c->scratch + off + 3, (uint16_t)chunk);
        memcpy(c->scratch + off + 5, data + sent, chunk);
        off += chunk + 5u;
        rc = put_u8(c->scratch, sizeof(c->scratch), &off, 0x14, end);
        if(rc) return fail(c, rc);
        if(msg_id == VQEAF_WLFW_BDF_DOWNLOAD && bdf_type >= 0) {
            rc = put_u8(c->scratch, sizeof(c->scratch), &off, 0x15,
                        (uint8_t)bdf_type);
            if(rc) return fail(c, rc);
        }
        rc = request(c, msg_id, c->scratch, off, timeout_ms);
        if(rc) return rc;
        sent += chunk;
        seg++;
    }
    if(msg_id == VQEAF_WLFW_BDF_DOWNLOAD && !c->fw_ready)
        c->state = VQEAF_WLFW_BDF_SENT;
    return 0;
}

int vqeaf_wlfw_cal_report(vqeaf_wlfw_client_t *c, const int32_t *ids,
                          size_t count, uint32_t timeout_ms)
{
    uint8_t meta[1 + 4 * VQEAF_WLFW_CAL_MAX];
    size_t i, off = 0;
    int rc;
    if(c == NULL || timeout_ms == 0 || count > VQEAF_WLFW_CAL_MAX ||
       (count != 0 && ids == NULL)) return fail(c, -EINVAL);
    if(c->state != VQEAF_WLFW_BDF_SENT &&
       c->state != VQEAF_WLFW_CAL_REPORTED &&
       c->state != VQEAF_WLFW_FW_READY) return fail(c, -EINVAL);
    meta[0] = (uint8_t)count;
    for(i = 0; i < count; i++) put_le32(meta + 1 + 4 * i, (uint32_t)ids[i]);
    rc = vqeaf_qmi_tlv_put(c->scratch, sizeof(c->scratch), 0x01, meta,
                           1 + 4 * count);
    if(rc < 0) return fail(c, rc);
    off = (size_t)rc;
    rc = request(c, VQEAF_WLFW_CAL_REPORT, c->scratch, off, timeout_ms);
    if(rc == 0 && c->state != VQEAF_WLFW_FW_READY)
        c->state = VQEAF_WLFW_CAL_REPORTED;
    return rc;
}

int vqeaf_wlfw_cal_update(vqeaf_wlfw_client_t *c, int32_t cal_id,
                          uint32_t seg_id, uint8_t *data, size_t cap,
                          size_t *len, int *end, uint32_t timeout_ms)
{
    size_t off = 0, dl = 0;
    const uint8_t *value;
    size_t value_len;
    int rc;
    if(c == NULL || data == NULL || len == NULL || end == NULL ||
       timeout_ms == 0) return -EINVAL;
    rc = put_u32(c->scratch, sizeof(c->scratch), &off, 0x01, (uint32_t)cal_id);
    if(rc) return fail(c, rc);
    rc = put_u32(c->scratch, sizeof(c->scratch), &off, 0x02, seg_id);
    if(rc) return fail(c, rc);
    rc = request(c, VQEAF_WLFW_CAL_UPDATE, c->scratch, off, timeout_ms);
    if(rc) return rc;
    rc = find_tlv(c->scratch + VQEAF_QMI_HDR_SIZE,
                  c->response_len - VQEAF_QMI_HDR_SIZE,
                  0x13, &value, &value_len);
    if(rc != 0) return fail(c, rc);
    if(value_len < 2u) return fail(c, -EPROTO);
    dl = get_le16(value);
    if(dl > value_len - 2u || dl > cap) return fail(c, -EMSGSIZE);
    memcpy(data, value + 2, dl);
    rc = find_tlv(c->scratch + VQEAF_QMI_HDR_SIZE,
                  c->response_len - VQEAF_QMI_HDR_SIZE,
                  0x14, &value, &value_len);
    if(rc != 0 || value_len != 1u) return fail(c, rc == 0 ? -EPROTO : rc);
    *len = dl;
    *end = value[0] != 0;
    return 0;
}

int vqeaf_wlfw_send_mac(vqeaf_wlfw_client_t *c, const uint8_t mac[6],
                        uint32_t timeout_ms)
{
    int zero = 1, ff = 1, i, rc;
    size_t off;
    if(c == NULL || mac == NULL || timeout_ms == 0) return -EINVAL;
    for(i = 0; i < 6; i++) {
        if(mac[i] != 0) zero = 0;
        if(mac[i] != 0xff) ff = 0;
    }
    if((mac[0] & 1u) || zero || ff) return fail(c, -EINVAL);
    if(c->state != VQEAF_WLFW_CAL_REPORTED &&
       c->state != VQEAF_WLFW_FW_READY) return fail(c, -EINVAL);
    rc = vqeaf_qmi_tlv_put(c->scratch, sizeof(c->scratch), 0x10, mac, 6);
    if(rc < 0) return fail(c, rc);
    off = (size_t)rc;
    return request(c, VQEAF_WLFW_MAC_ADDR, c->scratch, off, timeout_ms);
}

int vqeaf_wlfw_wait_fw_ready(vqeaf_wlfw_client_t *c, uint32_t timeout_ms)
{
    size_t out_len = 0;
    int rc;
    if(c == NULL || timeout_ms == 0) return -EINVAL;
    if(c->fw_ready) return 0;
    rc = vqeaf_qmi_wait_indication(&c->qmi, VQEAF_WLFW_FW_READY_IND,
                                   c->scratch, sizeof(c->scratch), &out_len,
                                   timeout_ms);
    if(rc || !c->fw_ready) return fail(c, rc ? rc : -EPROTO);
    return 0;
}
