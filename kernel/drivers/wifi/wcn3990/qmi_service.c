/* SPDX-License-Identifier: MIT */
/*
 * qmi_service.c — minimal native QMI client (request/response, transaction
 * IDs, bounded timeout and retry). Independent implementation of the public
 * wire format; no code copied from the BSD Linaro codec in hosttools.
 * No response is ever synthesized: only bytes received from the transport
 * can become a successful result.
 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "vqeaf/qmi_service.h"

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

int vqeaf_qmi_header_encode(uint8_t *buf, size_t cap,
                            const vqeaf_qmi_header_t *h)
{
    if(buf == NULL || h == NULL || cap < VQEAF_QMI_HDR_SIZE) return -EINVAL;
    buf[0] = h->type;
    put_le16(buf + 1, h->txn_id);
    put_le16(buf + 3, h->msg_id);
    put_le16(buf + 5, h->len);
    return 0;
}

int vqeaf_qmi_header_decode(const uint8_t *buf, size_t len,
                            vqeaf_qmi_header_t *h)
{
    if(buf == NULL || h == NULL || len < VQEAF_QMI_HDR_SIZE) return -EINVAL;
    h->type = buf[0];
    h->txn_id = get_le16(buf + 1);
    h->msg_id = get_le16(buf + 3);
    h->len = get_le16(buf + 5);
    return 0;
}

int vqeaf_qmi_tlv_put(uint8_t *buf, size_t cap, uint8_t type,
                      const uint8_t *data, size_t len)
{
    if(buf == NULL || cap < VQEAF_QMI_TLV_HDR || len > 0xffffu ||
       len > cap - VQEAF_QMI_TLV_HDR) return -EINVAL;
    if(len > 0 && data == NULL) return -EINVAL;
    buf[0] = type;
    put_le16(buf + 1, (uint16_t)len);
    if(len > 0) memcpy(buf + VQEAF_QMI_TLV_HDR, data, len);
    return (int)(VQEAF_QMI_TLV_HDR + len);
}

int vqeaf_qmi_tlv_next(const uint8_t *buf, size_t len, size_t *off,
                       uint8_t *type, const uint8_t **data, size_t *data_len)
{
    size_t o, dl;
    if(buf == NULL || off == NULL || type == NULL || data == NULL ||
       data_len == NULL) return -EINVAL;
    o = *off;
    if(o > len || len - o < VQEAF_QMI_TLV_HDR) return -EINVAL;
    *type = buf[o];
    dl = get_le16(buf + o + 1);
    if(dl > len - o - VQEAF_QMI_TLV_HDR) return -EINVAL;
    *data = buf + o + VQEAF_QMI_TLV_HDR;
    *data_len = dl;
    *off = o + VQEAF_QMI_TLV_HDR + dl;
    return 0;
}

int vqeaf_qmi_result_decode(const uint8_t *buf, size_t len,
                            vqeaf_qmi_result_t *out)
{
    size_t off = 0;
    if(buf == NULL || out == NULL) return -EINVAL;
    while(off <= len) {
        uint8_t type;
        const uint8_t *data;
        size_t dl;
        if(vqeaf_qmi_tlv_next(buf, len, &off, &type, &data, &dl) != 0)
            break;
        if(type == 0 && dl >= 4u) {
            out->result = get_le16(data);
            out->error = get_le16(data + 2);
            return 0;
        }
    }
    /* Some services embed the result struct without a type-0 TLV. */
    if(len >= 4u) {
        out->result = get_le16(buf);
        out->error = get_le16(buf + 2);
        return 0;
    }
    return -EINVAL;
}

const char *vqeaf_qmi_error_name(uint16_t err)
{
    switch(err) {
    case 0:  return "none";
    case 1:  return "malformed_msg";
    case 2:  return "no_memory";
    case 3:  return "internal";
    case 5:  return "client_ids_exhausted";
    case 41: return "invalid_id";
    case 58: return "encoding";
    case 90: return "incompatible_state";
    case 94: return "not_supported";
    default: return "unknown";
    }
}

void vqeaf_qmi_client_init(vqeaf_qmi_client_t *c, const vqeaf_qrtr_io_t *io)
{
    if(c == NULL) return;
    memset(c, 0, sizeof(*c));
    c->io = io;
    c->now_ms = io != NULL ? io->now_ms : NULL;
    c->next_txn = 1;
    c->link = VQEAF_QMI_LINK_DOWN; /* endpoint must be discovered first */
}

void vqeaf_qmi_client_reset(vqeaf_qmi_client_t *c, vqeaf_qmi_link_t reason)
{
    if(c == NULL) return;
    memset(c->pending, 0, sizeof(c->pending));
    c->link = reason;
    if(reason == VQEAF_QMI_LINK_RESET) c->resets++;
}

int vqeaf_qmi_client_link(const vqeaf_qmi_client_t *c)
{
    return c == NULL ? VQEAF_QMI_LINK_DOWN : (int)c->link;
}

static int pending_take(vqeaf_qmi_client_t *c, uint16_t txn, uint16_t msg_id,
                        uint64_t deadline, uint8_t retries)
{
    unsigned i;
    for(i = 0; i < VQEAF_QMI_MAX_PENDING; i++) {
        vqeaf_qmi_pending_t *p = &c->pending[i];
        if(p->used) continue;
        p->used = 1;
        p->txn_id = txn;
        p->msg_id = msg_id;
        p->deadline_ms = deadline;
        p->retries_left = retries;
        return (int)i;
    }
    return -1;
}

static void pending_free(vqeaf_qmi_client_t *c, int slot)
{
    if(slot >= 0 && slot < (int)VQEAF_QMI_MAX_PENDING)
        memset(&c->pending[slot], 0, sizeof(c->pending[slot]));
}

static uint16_t next_txn(vqeaf_qmi_client_t *c)
{
    uint16_t t = c->next_txn++;
    if(c->next_txn == 0) c->next_txn = 1; /* 0 is reserved */
    return t ? t : (c->next_txn++);
}

int vqeaf_qmi_request(vqeaf_qmi_client_t *c, const vqeaf_qrtr_addr_t *dst,
                      uint16_t msg_id, const uint8_t *payload,
                      size_t payload_len, uint8_t *out, size_t out_cap,
                      size_t *out_len, uint32_t timeout_ms,
                      uint8_t retries)
{
    vqeaf_qmi_header_t h;
    size_t total, rx_len;
    uint64_t deadline;
    uint16_t txn;
    int slot, rc;

    if(c == NULL || c->io == NULL || c->io->recv == NULL ||
       c->io->send == NULL || c->now_ms == NULL || dst == NULL ||
       out == NULL || out_len == NULL || timeout_ms == 0) return -EINVAL;
    if(c->link != VQEAF_QMI_LINK_UP) return -ENODEV;
    if(payload_len > VQEAF_QMI_BUF_SIZE - VQEAF_QMI_HDR_SIZE ||
       (payload_len > 0 && payload == NULL)) return -EMSGSIZE;

    txn = next_txn(c);
    deadline = c->now_ms(c->io->ctx) + timeout_ms;
    slot = pending_take(c, txn, msg_id, deadline, retries);
    if(slot < 0) return -EAGAIN;

    memset(&h, 0, sizeof(h));
    h.type = VQEAF_QMI_REQ;
    h.txn_id = txn;
    h.msg_id = msg_id;
    h.len = (uint16_t)payload_len;
    rc = vqeaf_qmi_header_encode(c->tx_buf, sizeof(c->tx_buf), &h);
    if(rc) { pending_free(c, slot); return rc; }
    if(payload_len > 0)
        memcpy(c->tx_buf + VQEAF_QMI_HDR_SIZE, payload, payload_len);
    total = VQEAF_QMI_HDR_SIZE + payload_len;

    rc = c->io->send(c->io->ctx, dst, c->tx_buf, total);
    if(rc) { pending_free(c, slot); return rc; }
    c->sent++;

    for(;;) {
        uint64_t now = c->now_ms(c->io->ctx);
        uint32_t slice;
        vqeaf_qmi_header_t rh;
        if(now >= c->pending[slot].deadline_ms) {
            if(c->pending[slot].retries_left > 0) {
                c->pending[slot].retries_left--;
                c->pending[slot].deadline_ms = now + timeout_ms;
                rc = c->io->send(c->io->ctx, dst, c->tx_buf, total);
                if(rc) { pending_free(c, slot); return rc; }
                c->sent++;
                continue;
            }
            c->timeouts++;
            pending_free(c, slot);
            return -ETIMEDOUT;
        }
        slice = (uint32_t)(c->pending[slot].deadline_ms - now);
        rx_len = 0;
        rc = c->io->recv(c->io->ctx, c->rx_buf, sizeof(c->rx_buf), &rx_len,
                         NULL, slice);
        if(rc == -EINTR) continue;
        if(rc == -ENETRESET || rc == -ECONNRESET || rc == -ENODEV) {
            c->link = VQEAF_QMI_LINK_DOWN;
            pending_free(c, slot);
            return rc;
        }
        if(rc) { pending_free(c, slot); return rc; }
        /* A zero-length receive means "no packet in this slice", not a
         * malformed response. Counting it as bad corrupts diagnostics. */
        if(rx_len == 0) continue;
        if(rx_len < VQEAF_QMI_HDR_SIZE ||
           vqeaf_qmi_header_decode(c->rx_buf, rx_len, &rh) != 0 ||
           rh.len != rx_len - VQEAF_QMI_HDR_SIZE) {
            c->bad_responses++;
            continue; /* malformed bytes are never a successful answer */
        }
        if(rh.type == VQEAF_QMI_IND) { c->indications++; continue; }
        if(rh.type != VQEAF_QMI_RESP || rh.msg_id != msg_id ||
           rh.txn_id != txn) {
            c->bad_responses++;
            continue;
        }
        if(rx_len > out_cap) { pending_free(c, slot); return -EMSGSIZE; }
        memcpy(out, c->rx_buf, rx_len);
        *out_len = rx_len;
        c->received++;
        pending_free(c, slot);
        return 0;
    }
}
