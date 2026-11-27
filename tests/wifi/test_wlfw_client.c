/* SPDX-License-Identifier: MIT */
/* Host tests for the independent WCN3990 WLFW client (mock transport only). */
#include "vqeaf_assert.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vqeaf/wlfw_client.h"

#define TX_CAP 6400u

typedef struct {
    uint64_t now;
    uint32_t sends;
    uint16_t last_msg;
    uint16_t last_txn;
    int awaiting_response;
    uint8_t last_tx[TX_CAP];
    size_t last_tx_len;
    uint16_t fail_msg;
    uint16_t inject_msg;
    uint16_t inject_ind;
    int injected;
    uint16_t pending_ind;
    uint64_t register_status;
    int cap_missing_board;
    int malformed_result;
    int cal_bad_len;
    int events;
    const uint8_t *download_data;
    size_t download_len;
    uint32_t download_segments;
    uint8_t cal_data[8];
    size_t cal_len;
    int cal_end;
} mock_t;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}
static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void wr64(uint8_t *p, uint64_t v)
{
    wr32(p, (uint32_t)v); wr32(p + 4, (uint32_t)(v >> 32));
}

static int tlv_find(const uint8_t *buf, size_t len, uint8_t type,
                    const uint8_t **data, size_t *data_len)
{
    size_t off = 0;
    while(off < len) {
        uint8_t t = buf[off];
        size_t n = rd16(buf + off + 1);
        if(off + 3 + n > len) return -EINVAL;
        if(t == type) { *data = buf + off + 3; *data_len = n; return 0; }
        off += 3 + n;
    }
    return -ENOENT;
}

static int tlv_put(uint8_t *buf, size_t cap, size_t *off, uint8_t type,
                   const void *data, size_t len)
{
    int n = vqeaf_qmi_tlv_put(buf + *off, cap - *off, type, data, len);
    if(n < 0) return n;
    *off += (size_t)n;
    return 0;
}

static void check_download(mock_t *m)
{
    const uint8_t *p = m->last_tx + VQEAF_QMI_HDR_SIZE;
    size_t len = m->last_tx_len - VQEAF_QMI_HDR_SIZE;
    const uint8_t *v;
    size_t vl, chunk;
    uint32_t seg, total;
    int rc;
    assert(tlv_find(p, len, 0x01, &v, &vl) == 0 && vl == 1 && v[0] == 1);
    assert(tlv_find(p, len, 0x11, &v, &vl) == 0 && vl == 4);
    total = rd32(v);
    assert(total == m->download_len);
    assert(tlv_find(p, len, 0x12, &v, &vl) == 0 && vl == 4);
    seg = rd32(v);
    assert(seg == m->download_segments);
    assert(tlv_find(p, len, 0x13, &v, &vl) == 0 && vl >= 2);
    chunk = rd16(v);
    assert(chunk <= VQEAF_WLFW_SEG_MAX && chunk == vl - 2);
    assert(seg * VQEAF_WLFW_SEG_MAX + chunk <= m->download_len);
    assert(memcmp(v + 2, m->download_data + seg * VQEAF_WLFW_SEG_MAX,
                  chunk) == 0);
    rc = tlv_find(p, len, 0x14, &v, &vl);
    assert(rc == 0 && vl == 1);
    assert(v[0] == (seg * VQEAF_WLFW_SEG_MAX + chunk == m->download_len));
    m->download_segments++;
}

static int m_send(void *ctx, const vqeaf_qrtr_addr_t *dst,
                  const uint8_t *buf, size_t len)
{
    mock_t *m = (mock_t *)ctx;
    vqeaf_qmi_header_t h;
    (void)dst;
    assert(len <= sizeof(m->last_tx));
    assert(vqeaf_qmi_header_decode(buf, len, &h) == 0);
    assert(h.type == VQEAF_QMI_REQ && h.len == len - VQEAF_QMI_HDR_SIZE);
    m->sends++;
    m->last_msg = h.msg_id;
    m->last_txn = h.txn_id;
    m->awaiting_response = 1;
    memcpy(m->last_tx, buf, len);
    m->last_tx_len = len;
    if(h.msg_id == VQEAF_WLFW_BDF_DOWNLOAD ||
       h.msg_id == VQEAF_WLFW_CAL_DOWNLOAD)
        check_download(m);
    return 0;
}

static size_t make_response(mock_t *m, uint8_t *buf, size_t cap)
{
    size_t off = 0;
    uint8_t result[4];
    vqeaf_qmi_header_t h;
    wr16(result, m->last_msg == m->fail_msg ? 1 : 0);
    wr16(result + 2, m->last_msg == m->fail_msg ? 90 : 0);
    if(m->malformed_result) {
        memset(buf + 7, 0, 4);
        off = 4;
    } else {
        assert(tlv_put(buf + 7, cap - 7, &off, 0x00, result, 4) == 0);
        if(m->last_msg == VQEAF_WLFW_IND_REGISTER) {
            uint8_t status[8];
            wr64(status, m->register_status);
            assert(tlv_put(buf + 7, cap - 7, &off, 0x10, status, 8) == 0);
        }
        if(m->last_msg == VQEAF_WLFW_CAP && !m->cap_missing_board) {
            uint8_t value[8];
            wr32(value, 0x3990); wr32(value + 4, 1);
            assert(tlv_put(buf + 7, cap - 7, &off, 0x10, value, 8) == 0);
            wr32(value, 0x204); wr32(value + 4, 0);
            assert(tlv_put(buf + 7, cap - 7, &off, 0x11, value, 4) == 0);
            wr32(value, 0x315);
            assert(tlv_put(buf + 7, cap - 7, &off, 0x12, value, 4) == 0);
            assert(tlv_put(buf + 7, cap - 7, &off, 0x15, "\x02", 1) == 0);
        }
        if(m->last_msg == VQEAF_WLFW_CAL_UPDATE) {
            uint8_t dl[2 + sizeof(m->cal_data)];
            uint8_t end = m->cal_end ? 1 : 0;
            size_t dl_len = m->cal_len + 2;
            wr16(dl, (uint16_t)(m->cal_bad_len ? m->cal_len + 1 : m->cal_len));
            memcpy(dl + 2, m->cal_data, m->cal_len);
            assert(tlv_put(buf + 7, cap - 7, &off, 0x13, dl, dl_len) == 0);
            assert(tlv_put(buf + 7, cap - 7, &off, 0x14, &end, 1) == 0);
        }
    }
    memset(&h, 0, sizeof(h));
    h.type = VQEAF_QMI_RESP; h.txn_id = m->last_txn;
    h.msg_id = m->last_msg; h.len = (uint16_t)off;
    assert(vqeaf_qmi_header_encode(buf, cap, &h) == 0);
    return VQEAF_QMI_HDR_SIZE + off;
}

static size_t make_indication(uint16_t msg_id, uint8_t *buf, size_t cap)
{
    vqeaf_qmi_header_t h;
    memset(&h, 0, sizeof(h));
    h.type = VQEAF_QMI_IND; h.txn_id = 0x4242; h.msg_id = msg_id; h.len = 0;
    assert(vqeaf_qmi_header_encode(buf, cap, &h) == 0);
    return VQEAF_QMI_HDR_SIZE;
}

static int m_recv(void *ctx, uint8_t *buf, size_t cap, size_t *out_len,
                  vqeaf_qrtr_addr_t *src, uint32_t timeout_ms)
{
    mock_t *m = (mock_t *)ctx;
    (void)src;
    *out_len = 0;
    if(m->pending_ind != 0) {
        uint16_t id = m->pending_ind;
        m->pending_ind = 0;
        *out_len = make_indication(id, buf, cap);
        return 0;
    }
    if(m->inject_msg != 0 && m->inject_msg == m->last_msg && !m->injected) {
        m->injected = 1;
        *out_len = make_indication(m->inject_ind, buf, cap);
        return 0;
    }
    if(!m->awaiting_response) {
        m->now += timeout_ms;
        return 0;
    }
    *out_len = make_response(m, buf, cap);
    m->awaiting_response = 0;
    return 0;
}

static uint64_t m_now(void *ctx) { return ((mock_t *)ctx)->now; }

static void event(void *ctx, uint16_t msg_id)
{
    mock_t *m = (mock_t *)ctx;
    (void)msg_id;
    m->events++;
}

static void mock_init(mock_t *m)
{
    memset(m, 0, sizeof(*m));
    m->cal_len = 5;
    memcpy(m->cal_data, "calib", 5);
}

static void client_init(vqeaf_wlfw_client_t *c, mock_t *m,
                        vqeaf_qrtr_io_t *io)
{
    vqeaf_qrtr_addr_t server = { .node = 2, .port = 0x1234 };
    memset(io, 0, sizeof(*io));
    io->ctx = m; io->local_node = 1; io->send = m_send; io->recv = m_recv;
    io->now_ms = m_now;
    vqeaf_wlfw_client_init(c, io, &server, event, m);
}

static void reach_cap(vqeaf_wlfw_client_t *c, mock_t *m)
{
    assert(vqeaf_wlfw_connect(c, 100, 0) == 0);
    assert(c->state == VQEAF_WLFW_REGISTERED && !c->fw_ready);
    assert(vqeaf_wlfw_msa_info(c, 0xb0000000u, 0x100000u, 100) == 0);
    assert(c->state == VQEAF_WLFW_MSA_INFO_SENT);
    assert(vqeaf_wlfw_msa_ready(c, 100) == 0);
    assert(c->state == VQEAF_WLFW_MSA_READY_SENT);
    assert(vqeaf_wlfw_cap(c, 100) == 0);
    assert(c->state == VQEAF_WLFW_CAP_READY);
    assert(c->caps.board_id == 0x204 && c->caps.chip_id == 0x3990);
    assert(c->caps.num_macs == 2);
    (void)m;
}

int main(void)
{
    mock_t m;
    vqeaf_qrtr_io_t io;
    vqeaf_wlfw_client_t c;
    uint8_t bdf[7000], mac[6] = { 0x02, 0, 0, 0, 0, 1 };
    uint8_t cal[16];
    size_t cal_len;
    int cal_end;
    const uint8_t *v;
    size_t vl, off;
    uint8_t zero[6] = { 0 };

    for(off = 0; off < sizeof(bdf); off++) bdf[off] = (uint8_t)(off * 17u);

    /* Full control-plane sequence with real wire checks on every request. */
    mock_init(&m);
    m.download_data = bdf; m.download_len = sizeof(bdf);
    client_init(&c, &m, &io);
    assert(vqeaf_wlfw_msa_info(&c, 0xb0000000u, 0x100000u, 100) == -EINVAL);
    assert(c.state == VQEAF_WLFW_FAILED);
    client_init(&c, &m, &io);
    assert(vqeaf_wlfw_connect(&c, 100, 0) == 0);
    assert(c.state == VQEAF_WLFW_REGISTERED && !c.fw_ready);
    assert(m.last_msg == VQEAF_WLFW_IND_REGISTER);
    assert(tlv_find(m.last_tx + 7, m.last_tx_len - 7, 0x15, &v, &vl) == 0);
    assert(vl == 4 && rd32(v) == VQEAF_WLFW_CLIENT_ID);
    assert(vqeaf_wlfw_msa_info(&c, 0xb0000000u, 0x100000u, 100) == 0);
    assert(c.state == VQEAF_WLFW_MSA_INFO_SENT);
    assert(tlv_find(m.last_tx + 7, m.last_tx_len - 7, 0x01, &v, &vl) == 0);
    assert(vl == 8 && rd64(v) == 0xb0000000u);
    m.inject_msg = 0x002Eu; m.inject_ind = VQEAF_WLFW_MSA_READY_IND;
    assert(vqeaf_wlfw_msa_ready(&c, 100) == 0);
    assert(c.msa_ready && c.state == VQEAF_WLFW_MSA_READY_SENT);
    assert(vqeaf_wlfw_wait_msa_ready(&c, 100) == 0);
    assert(vqeaf_wlfw_cap(&c, 100) == 0);
    assert(c.caps.board_id == 0x204 && c.caps.chip_family == 1);
    assert(vqeaf_wlfw_download(&c, VQEAF_WLFW_BDF_DOWNLOAD, bdf, sizeof(bdf),
                               0, VQEAF_WLFW_BDF_TYPE_BIN, 100) == 0);
    assert(m.download_segments == 2 && c.state == VQEAF_WLFW_BDF_SENT);
    {
        int32_t ids[2] = { 0, 2 };
        assert(vqeaf_wlfw_cal_report(&c, ids, 2, 100) == 0);
        assert(c.state == VQEAF_WLFW_CAL_REPORTED);
        assert(tlv_find(m.last_tx + 7, m.last_tx_len - 7, 0x01, &v, &vl) == 0);
        assert(vl == 9 && v[0] == 2 && (int32_t)rd32(v + 1) == 0);
        assert((int32_t)rd32(v + 5) == 2);
    }
    assert(vqeaf_wlfw_send_mac(&c, mac, 100) == 0);
    assert(tlv_find(m.last_tx + 7, m.last_tx_len - 7, 0x10, &v, &vl) == 0);
    assert(vl == 6 && memcmp(v, mac, 6) == 0);
    assert(vqeaf_wlfw_cal_update(&c, 2, 3, cal, sizeof(cal), &cal_len,
                                 &cal_end, 100) == 0);
    assert(cal_len == 5 && !cal_end && memcmp(cal, "calib", 5) == 0);
    assert(!c.fw_ready);
    m.pending_ind = VQEAF_WLFW_FW_READY_IND;
    assert(vqeaf_wlfw_wait_fw_ready(&c, 100) == 0);
    assert(c.fw_ready && c.state == VQEAF_WLFW_FW_READY);
    assert(m.events >= 2);

    /* IND_REGISTER status is evidence too, but only when the FW_READY bit is
     * actually present. Discovery alone never sets it. */
    mock_init(&m);
    m.register_status = VQEAF_WLFW_STATUS_FW_READY;
    client_init(&c, &m, &io);
    assert(vqeaf_wlfw_connect(&c, 100, 0) == 0);
    assert(c.fw_ready && c.state == VQEAF_WLFW_FW_READY);

    /* An indication received during the final BDF response is not lost and
     * later successful calls cannot downgrade FW_READY. */
    mock_init(&m);
    m.download_data = bdf; m.download_len = sizeof(bdf);
    client_init(&c, &m, &io);
    reach_cap(&c, &m);
    m.inject_msg = VQEAF_WLFW_BDF_DOWNLOAD;
    m.inject_ind = VQEAF_WLFW_FW_READY_IND;
    assert(vqeaf_wlfw_download(&c, VQEAF_WLFW_BDF_DOWNLOAD, bdf, sizeof(bdf),
                               0, VQEAF_WLFW_BDF_TYPE_BIN, 100) == 0);
    assert(c.fw_ready && c.state == VQEAF_WLFW_FW_READY);
    assert(vqeaf_wlfw_cal_report(&c, NULL, 0, 100) == 0);
    assert(c.state == VQEAF_WLFW_FW_READY);

    /* Missing indication is a timeout, never a synthetic ready state. */
    mock_init(&m);
    client_init(&c, &m, &io);
    assert(vqeaf_wlfw_connect(&c, 100, 0) == 0);
    assert(vqeaf_wlfw_wait_fw_ready(&c, 20) == -ETIMEDOUT);
    assert(c.state == VQEAF_WLFW_FAILED && !c.fw_ready);

    mock_init(&m);
    client_init(&c, &m, &io);
    assert(vqeaf_wlfw_connect(&c, 100, 0) == 0);
    assert(vqeaf_wlfw_msa_info(&c, 0xb0000000u, 0x100000u, 100) == 0);
    assert(vqeaf_wlfw_msa_ready(&c, 100) == 0);
    assert(vqeaf_wlfw_wait_msa_ready(&c, 20) == -ETIMEDOUT);
    assert(c.state == VQEAF_WLFW_FAILED && !c.msa_ready);

    /* Required CAP data and QMI errors fail closed; reconnect drops stale CAP. */
    mock_init(&m);
    client_init(&c, &m, &io);
    reach_cap(&c, &m);
    m.cap_missing_board = 1;
    assert(vqeaf_wlfw_connect(&c, 100, 0) == 0);
    assert(c.caps.board_id == 0);
    assert(vqeaf_wlfw_msa_info(&c, 0xb0000000u, 0x100000u, 100) == 0);
    assert(vqeaf_wlfw_msa_ready(&c, 100) == 0);
    assert(vqeaf_wlfw_cap(&c, 100) == -ENOENT);
    assert(c.state == VQEAF_WLFW_FAILED && c.caps.board_id == 0);

    mock_init(&m);
    m.download_data = bdf; m.download_len = sizeof(bdf);
    m.fail_msg = VQEAF_WLFW_BDF_DOWNLOAD;
    client_init(&c, &m, &io);
    reach_cap(&c, &m);
    assert(vqeaf_wlfw_download(&c, VQEAF_WLFW_BDF_DOWNLOAD, bdf, sizeof(bdf),
                               0, VQEAF_WLFW_BDF_TYPE_BIN, 100) == -EPROTO);
    assert(c.state == VQEAF_WLFW_FAILED && c.last_qmi_error == 90);

    /* A ready indication received before registration cannot establish readiness. */
    mock_init(&m);
    client_init(&c, &m, &io);
    m.inject_msg = VQEAF_WLFW_IND_REGISTER;
    m.inject_ind = VQEAF_WLFW_FW_READY_IND;
    assert(vqeaf_wlfw_connect(&c, 100, 0) == 0);
    assert(c.state == VQEAF_WLFW_REGISTERED && !c.fw_ready);

    /* Malformed success payloads do not pass the legacy result fallback. */
    mock_init(&m);
    m.malformed_result = 1;
    client_init(&c, &m, &io);
    assert(vqeaf_wlfw_connect(&c, 100, 0) == -EPROTO);
    assert(c.state == VQEAF_WLFW_FAILED && !c.fw_ready);

    /* Declared calibration data cannot exceed the received TLV or capacity. */
    mock_init(&m);
    m.download_data = bdf; m.download_len = 1;
    client_init(&c, &m, &io);
    reach_cap(&c, &m);
    assert(vqeaf_wlfw_download(&c, VQEAF_WLFW_BDF_DOWNLOAD, bdf, 1,
                               0, VQEAF_WLFW_BDF_TYPE_BIN, 100) == 0);
    assert(vqeaf_wlfw_cal_report(&c, NULL, 0, 100) == 0);
    m.cal_bad_len = 1;
    assert(vqeaf_wlfw_cal_update(&c, 2, 0, cal, sizeof(cal), &cal_len,
                                 &cal_end, 100) == -EMSGSIZE);
    assert(c.state == VQEAF_WLFW_FAILED);

    /* Invalid caller input is rejected before it can reach the service. */
    mock_init(&m);
    client_init(&c, &m, &io);
    assert(vqeaf_wlfw_connect(&c, 100, 0) == 0);
    assert(vqeaf_wlfw_send_mac(&c, zero, 100) == -EINVAL);
    assert(vqeaf_wlfw_download(&c, VQEAF_WLFW_BDF_DOWNLOAD, bdf, 0,
                               0, VQEAF_WLFW_BDF_TYPE_BIN, 100) == -ENODATA);
    assert(vqeaf_wlfw_download(&c, VQEAF_WLFW_BDF_DOWNLOAD, bdf, 1,
                               0, -2, 100) == -EINVAL);
    assert(vqeaf_wlfw_cal_report(&c, NULL, VQEAF_WLFW_CAL_MAX + 1u,
                                 100) == -EINVAL);

    printf("test_wlfw_client: ALL OK\n");
    return 0;
}
