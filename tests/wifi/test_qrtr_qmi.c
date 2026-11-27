/* SPDX-License-Identifier: MIT */
/* Host-side QRTR/QMI protocol tests. Inputs are labelled FIXTURE files only. */
#include "vqeaf_assert.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vqeaf/qrtr_transport.h"
#include "vqeaf/qmi_service.h"

typedef struct {
    uint64_t now;
    uint32_t local_node;
    const uint8_t *pkt[8];
    size_t pkt_len[8];
    int pkt_err[8];
    int n, pos;
    uint8_t last_tx[128];
    size_t last_tx_len;
    int sends;
    int indications_seen;
} mock_t;

static int m_send(void *p, const vqeaf_qrtr_addr_t *dst,
                  const uint8_t *buf, size_t len)
{
    mock_t *m = (mock_t *)p;
    (void)dst;
    m->sends++;
    if(len > sizeof(m->last_tx)) return -EMSGSIZE;
    memcpy(m->last_tx, buf, len);
    m->last_tx_len = len;
    return 0;
}

static int m_recv(void *p, uint8_t *buf, size_t cap, size_t *out_len,
                  vqeaf_qrtr_addr_t *src, uint32_t timeout_ms)
{
    mock_t *m = (mock_t *)p;
    if(src != NULL) { src->node = 2; src->port = 0x1234; }
    if(m->pos < m->n) {
        int i = m->pos++;
        if(m->pkt_err[i]) return m->pkt_err[i];
        if(m->pkt_len[i] > cap) return -EMSGSIZE;
        memcpy(buf, m->pkt[i], m->pkt_len[i]);
        *out_len = m->pkt_len[i];
        return 0;
    }
    /* No more packets: consume the whole slice, as a blocking recv would. */
    m->now += timeout_ms;
    *out_len = 0;
    return 0;
}

static uint64_t m_now(void *p) { return ((mock_t *)p)->now; }
static void m_indication(void *p, uint16_t msg_id, const uint8_t *payload,
                         size_t payload_len)
{
    mock_t *m = (mock_t *)p;
    (void)msg_id;
    (void)payload;
    (void)payload_len;
    m->indications_seen++;
}

static void mock_init(mock_t *m)
{
    memset(m, 0, sizeof(*m));
    m->local_node = 1;
}

static void mock_push(mock_t *m, const uint8_t *p, size_t n)
{
    if(m->n >= 8) return;
    m->pkt[m->n] = p;
    m->pkt_len[m->n] = n;
    m->pkt_err[m->n] = 0;
    m->n++;
}

static void mock_push_err(mock_t *m, int err)
{
    if(m->n >= 8) return;
    m->pkt[m->n] = NULL;
    m->pkt_len[m->n] = 0;
    m->pkt_err[m->n] = err;
    m->n++;
}

static uint8_t *load(const char *dir, const char *name, size_t *len)
{
    char path[512];
    FILE *f;
    long n;
    uint8_t *buf;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "rb");
    if(!f) { fprintf(stderr, "missing fixture %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(n <= 0) { fclose(f); return NULL; }
    buf = (uint8_t *)malloc((size_t)n);
    if(!buf) { fclose(f); return NULL; }
    if(fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static void fixture_io(mock_t *m, vqeaf_qrtr_io_t *io)
{
    io->ctx = m;
    io->local_node = m->local_node;
    io->send = m_send;
    io->recv = m_recv;
    io->now_ms = m_now;
}

int main(int argc, char **argv)
{
    char dir[512];
    size_t n_new, n_del, n_short, n_ok, n_fail, n_ind;
    uint8_t *new_srv, *del_srv, *shortpkt, *ok, *fail, *ind;
    uint8_t buf[128];
    size_t len;
    mock_t m;
    vqeaf_qrtr_io_t io;
    vqeaf_qrtr_ctrl_t c;
    vqeaf_qrtr_server_t srv;
    vqeaf_qmi_client_t qc;
    vqeaf_qmi_result_t res;
    vqeaf_qmi_header_t h;
    size_t off, dl, olen, tlen;
    uint8_t type;
    const uint8_t *data;
    uint8_t wrong[64];
    int rc;

    if(argc < 2) { fprintf(stderr, "usage: %s FIXTURE_DIR\n", argv[0]); return 2; }
    snprintf(dir, sizeof(dir), "%s", argv[1]);
    new_srv = load(dir, "FIXTURE_qrtr_new_server_45.bin", &n_new);
    del_srv = load(dir, "FIXTURE_qrtr_del_server_45.bin", &n_del);
    shortpkt = load(dir, "FIXTURE_qrtr_short.bin", &n_short);
    ok = load(dir, "FIXTURE_qmi_response_ok.bin", &n_ok);
    fail = load(dir, "FIXTURE_qmi_response_failure.bin", &n_fail);
    ind = load(dir, "FIXTURE_qmi_indication.bin", &n_ind);
    assert(n_new == 20 && n_del == 20 && n_short == 8);
    assert(n_ok == 14 && n_fail == 14 && n_ind == 7);

    /* QRTR control codec: real wire roundtrip and bounds. */
    memset(&c, 0, sizeof(c));
    c.cmd = VQEAF_QRTR_NEW_SERVER; c.service = 0x45;
    c.verinst = (1u << 8) | 1u; c.node = 2; c.port = 0x1234;
    assert(vqeaf_qrtr_ctrl_encode(buf, sizeof(buf), &c, &tlen) == 0);
    assert(tlen == 20 && memcmp(buf, new_srv, 20) == 0);
    assert(vqeaf_qrtr_ctrl_decode(buf, tlen, &c) == 0);
    assert(c.service == 0x45 && c.verinst == 0x101 && c.node == 2 &&
           c.port == 0x1234);
    assert(vqeaf_qrtr_ctrl_decode(shortpkt, n_short, &c) == -EINVAL);
    assert(vqeaf_qrtr_ctrl_encode(buf, 8, &c, &tlen) == -EINVAL);

    /* Discovery: a received NEW_SERVER is the only way to find a service. */
    mock_init(&m);
    mock_push(&m, new_srv, n_new);
    fixture_io(&m, &io);
    assert(vqeaf_qrtr_discover(&io, 0x45, 1, 1, 500, &srv) == 0);
    assert(srv.service == 0x45 && srv.instance == 1 && srv.node == 2 &&
           srv.port == 0x1234);
    assert(m.last_tx_len == 20 && m.sends == 1);

    vqeaf_qrtr_table_reset(); /* isolate: prior case inserted a real server */
    mock_init(&m);
    fixture_io(&m, &io);
    assert(vqeaf_qrtr_discover(&io, 0x45, 1, 1, 50, &srv) == -ENOENT);
    assert(m.sends == 1); /* one real lookup, no fabricated answer */

    vqeaf_qrtr_table_reset();
    mock_init(&m);
    mock_push(&m, shortpkt, n_short);
    fixture_io(&m, &io);
    assert(vqeaf_qrtr_discover(&io, 0x45, 1, 1, 50, &srv) == -ENOENT);

    /* Server table: only real DEL_SERVER removes it. */
    {
        vqeaf_qrtr_ctrl_t add;
        memset(&add, 0, sizeof(add));
        add.cmd = VQEAF_QRTR_NEW_SERVER; add.service = 0x45;
        add.verinst = 0x101; add.node = 2; add.port = 0x1234;
        assert(vqeaf_qrtr_table_add(&add) == 0);
        assert(vqeaf_qrtr_table_find(0x45, 1, &srv) == 0);
        add.cmd = VQEAF_QRTR_DEL_SERVER;
        assert(vqeaf_qrtr_table_drop(&add) == 0);
        assert(vqeaf_qrtr_table_find(0x45, 1, &srv) == -ENOENT);
    }
    vqeaf_qrtr_table_reset();
    assert(vqeaf_qrtr_table_find(0x45, 1, &srv) == -ENOENT);

    /* QMI header + TLV codecs. */
    memset(&h, 0, sizeof(h));
    h.type = VQEAF_QMI_RESP; h.txn_id = 1; h.msg_id = 0x20; h.len = 7;
    assert(vqeaf_qmi_header_encode(buf, sizeof(buf), &h) == 0);
    assert(vqeaf_qmi_header_decode(buf, 7, &h) == 0);
    assert(h.txn_id == 1 && h.msg_id == 0x20 && h.len == 7);
    assert(vqeaf_qmi_header_decode(shortpkt, 4, &h) == -EINVAL);
    assert(vqeaf_qmi_tlv_put(buf, sizeof(buf), 0, (const uint8_t *)"\x00\x00\x00\x00", 4) == 7);
    off = 0;
    assert(vqeaf_qmi_tlv_next(buf, 7, &off, &type, &data, &dl) == 0);
    assert(type == 0 && dl == 4 && off == 7);
    assert(vqeaf_qmi_tlv_next(buf, 5, &off, &type, &data, &dl) == -EINVAL);
    assert(vqeaf_qmi_result_decode(ok + 7, 7, &res) == 0);
    assert(res.result == 0 && res.error == 0);

    /* Happy path: the response bytes really received become the result. */
    mock_init(&m);
    mock_push(&m, ok, n_ok);
    fixture_io(&m, &io);
    vqeaf_qmi_client_init(&qc, &io);
    vqeaf_qmi_client_reset(&qc, VQEAF_QMI_LINK_UP);
    assert(vqeaf_qmi_request(&qc, &(vqeaf_qrtr_addr_t){2, 0x1234}, 0x20,
                             NULL, 0, buf, sizeof(buf), &olen, 100, 0) == 0);
    assert(olen == 14);
    assert(vqeaf_qmi_result_decode(buf + 7, 7, &res) == 0);
    assert(res.result == 0 && res.error == 0);
    assert(qc.sent == 1 && qc.received == 1 && qc.timeouts == 0);

    /* Indications are delivered from received bytes and can be awaited without
     * fabricating a response or losing non-matching indications. */
    memcpy(wrong, ind, n_ind);
    wrong[3] = 0x21; wrong[4] = 0;
    mock_init(&m);
    mock_push(&m, wrong, n_ind);
    mock_push(&m, ind, n_ind);
    fixture_io(&m, &io);
    vqeaf_qmi_client_init(&qc, &io);
    vqeaf_qmi_client_reset(&qc, VQEAF_QMI_LINK_UP);
    vqeaf_qmi_client_set_indication_handler(&qc, m_indication, &m);
    assert(vqeaf_qmi_wait_indication(&qc, 0x20, buf, sizeof(buf), &olen,
                                     100) == 0);
    assert(olen == n_ind && memcmp(buf, ind, n_ind) == 0);
    assert(m.indications_seen == 2 && qc.indications == 2);

    /* QMI failure result is an honest answer, not a transport error. */
    mock_init(&m);
    mock_push(&m, fail, n_fail);
    fixture_io(&m, &io);
    vqeaf_qmi_client_init(&qc, &io);
    vqeaf_qmi_client_reset(&qc, VQEAF_QMI_LINK_UP);
    assert(vqeaf_qmi_request(&qc, &(vqeaf_qrtr_addr_t){2, 0x1234}, 0x20,
                             NULL, 0, buf, sizeof(buf), &olen, 100, 0) == 0);
    assert(vqeaf_qmi_result_decode(buf + 7, 7, &res) == 0);
    assert(res.result == 1 && res.error == 94);
    assert(strcmp(vqeaf_qmi_error_name(94), "not_supported") == 0);

    /* Wrong transaction id must never be accepted as this request's answer. */
    memcpy(wrong, ok, n_ok);
    wrong[1] = 2;
    mock_init(&m);
    mock_push(&m, wrong, n_ok);
    fixture_io(&m, &io);
    vqeaf_qmi_client_init(&qc, &io);
    vqeaf_qmi_client_reset(&qc, VQEAF_QMI_LINK_UP);
    assert(vqeaf_qmi_request(&qc, &(vqeaf_qrtr_addr_t){2, 0x1234}, 0x20,
                             NULL, 0, buf, sizeof(buf), &olen, 30, 0) == -ETIMEDOUT);
    assert(qc.bad_responses == 1 && qc.received == 0);

    /* Malformed QMI length is rejected, no success is reported. */
    memcpy(wrong, ok, n_ok);
    wrong[5] = 99;
    mock_init(&m);
    mock_push(&m, wrong, n_ok);
    fixture_io(&m, &io);
    vqeaf_qmi_client_init(&qc, &io);
    vqeaf_qmi_client_reset(&qc, VQEAF_QMI_LINK_UP);
    assert(vqeaf_qmi_request(&qc, &(vqeaf_qrtr_addr_t){2, 0x1234}, 0x20,
                             NULL, 0, buf, sizeof(buf), &olen, 30, 0) == -ETIMEDOUT);
    assert(qc.bad_responses >= 1);

    /* Firmware sends nothing: bounded retry, then honest ETIMEDOUT. */
    mock_init(&m);
    fixture_io(&m, &io);
    vqeaf_qmi_client_init(&qc, &io);
    vqeaf_qmi_client_reset(&qc, VQEAF_QMI_LINK_UP);
    assert(vqeaf_qmi_request(&qc, &(vqeaf_qrtr_addr_t){2, 0x1234}, 0x20,
                             NULL, 0, buf, sizeof(buf), &olen, 30, 2) == -ETIMEDOUT);
    assert(m.sends == 3 && qc.timeouts == 1);

    /* QRTR disconnect during the wait propagates and marks link down. */
    mock_init(&m);
    mock_push_err(&m, -ENODEV);
    fixture_io(&m, &io);
    vqeaf_qmi_client_init(&qc, &io);
    vqeaf_qmi_client_reset(&qc, VQEAF_QMI_LINK_UP);
    assert(vqeaf_qmi_request(&qc, &(vqeaf_qrtr_addr_t){2, 0x1234}, 0x20,
                             NULL, 0, buf, sizeof(buf), &olen, 100, 2) == -ENODEV);
    assert(vqeaf_qmi_client_link(&qc) == VQEAF_QMI_LINK_DOWN);

    /* CNSS restart: reset clears pending; txn ids keep increasing. */
    mock_init(&m);
    mock_push(&m, ind, n_ind);
    fixture_io(&m, &io);
    vqeaf_qmi_client_init(&qc, &io);
    vqeaf_qmi_client_reset(&qc, VQEAF_QMI_LINK_UP);
    /* Consume txn 1 with a request that gets no answer. */
    assert(vqeaf_qmi_request(&qc, &(vqeaf_qrtr_addr_t){2, 0x1234}, 0x20,
                             NULL, 0, buf, sizeof(buf), &olen, 10, 0) == -ETIMEDOUT);
    vqeaf_qmi_client_reset(&qc, VQEAF_QMI_LINK_RESET);
    assert(qc.resets == 1 && vqeaf_qmi_client_link(&qc) == VQEAF_QMI_LINK_RESET);
    vqeaf_qmi_client_reset(&qc, VQEAF_QMI_LINK_UP);
    memcpy(wrong, ok, n_ok);
    wrong[1] = 2; /* next transaction after the reset */
    mock_push(&m, wrong, n_ok);
    assert(vqeaf_qmi_request(&qc, &(vqeaf_qrtr_addr_t){2, 0x1234}, 0x20,
                             NULL, 0, buf, sizeof(buf), &olen, 100, 0) == 0);
    assert(qc.indications == 1); /* indication consumed, not mistaken */

    printf("test_qrtr_qmi: ALL OK\n");
    return 0;
}
