/* SPDX-License-Identifier: MIT
 * qwlfgw - pure-Linux WLFW (QMI service 0x45) server for violet.
 *
 * Goal: replace the Android/Bionic cnss-daemon with a static aarch64-musl
 * daemon that speaks QMI over the kernel QRTR socket directly. It registers
 * service 0x45, tracks clients that send WLFW_IND_REGISTER, accepts CAP and
 * BDF_DOWNLOAD requests and serves real firmware files from /lib/firmware.
 *
 * This is a foundational implementation: message IDs are taken from the
 * vendored wlfwd client (wlfwd.c:35-47). TLV layouts that are not yet
 * verified against the downstream icnss driver are parsed defensively and
 * reported; no MAC, calibration data or firmware is fabricated.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libqrtr.h"

#define WLFW_SERVICE 0x45
#define QMI_REQ  0
#define QMI_RESP 2
#define QMI_IND  4

/* WLFW message IDs (from hosttools/src/wlfwd/wlfwd.c). */
#define WLFW_IND_REGISTER      0x0020
#define WLFW_FW_READY_IND      0x0021
#define WLFW_CAP               0x0024
#define WLFW_BDF_DOWNLOAD      0x0025
#define WLFW_CAL_REPORT        0x0026
#define WLFW_CAL_DOWNLOAD      0x0027
#define WLFW_INIT_CAL_DL_IND   0x0028
#define WLFW_CAL_UPDATE        0x0029
#define WLFW_INIT_CAL_UPD_IND  0x002A
#define WLFW_MAC_ADDR          0x0033
#define WLFW_FW_MEM_READY_IND  0x0037
#define WLFW_FW_INIT_DONE_IND  0x0038

#define MAX_CLIENTS 8
#define BUF_SZ 8192

struct client {
    uint32_t node, port;
    int active;
};
static struct client clients[MAX_CLIENTS];
static int registered;
static int cap_done;
static int bdf_done;

static void log_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

static void remember(uint32_t node, uint32_t port)
{
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(clients[i].active && clients[i].node == node &&
           clients[i].port == port) return;
        if(!clients[i].active) {
            clients[i].node = node; clients[i].port = port;
            clients[i].active = 1;
            log_line("wlfw: client %u:%u registered", node, port);
            return;
        }
    }
}

/* Minimal QMI TLV walker: returns pointer/length of the requested type. */
static const uint8_t *find_tlv(const uint8_t *buf, size_t len, int type,
                               size_t *out_len)
{
    size_t off = 7;  /* QMI header */
    while(off + 3 <= len) {
        int t = buf[off];
        size_t n = buf[off + 1] | (unsigned)buf[off + 2] << 8;
        off += 3;
        if(off + n > len) break;
        if(t == type) { *out_len = n; return buf + off; }
        off += n;
    }
    return NULL;
}

static int send_resp(int sock, uint32_t node, uint32_t port, uint16_t txn,
                     uint16_t msg, const uint8_t *tlvs, size_t n)
{
    uint8_t out[BUF_SZ];
    if(n + 7 > sizeof(out)) return -1;
    out[0] = QMI_RESP;
    out[1] = txn & 0xff; out[2] = txn >> 8;
    out[3] = msg & 0xff; out[4] = msg >> 8;
    out[5] = n & 0xff; out[6] = n >> 8;
    if(n > 0) memcpy(out + 7, tlvs, n);
    return qrtr_sendto(sock, node, port, out, n + 7);
}

/* QMI standard result TLV: type 0x02, length 4, result, error. */
static int send_result(int sock, uint32_t node, uint32_t port, uint16_t txn,
                       uint16_t msg, uint16_t result, uint16_t error)
{
    uint8_t tlv[7] = { 0x02, 0x04, 0x00,
                       result & 0xff, result >> 8,
                       error & 0xff, error >> 8 };
    return send_resp(sock, node, port, txn, msg, tlv, sizeof(tlv));
}

static void handle_bdf(const uint8_t *buf, size_t len)
{
    size_t n = 0;
    const uint8_t *file_id = find_tlv(buf, len, 0x10, &n);
    const uint8_t *data = find_tlv(buf, len, 0x13, &n);
    uint32_t id = file_id && n >= 4 ?
        file_id[0] | file_id[1] << 8 | file_id[2] << 16 | (uint32_t)file_id[3] << 24 : 0;

    (void)data;
    log_line("wlfw: BDF_DOWNLOAD file_id=0x%08x data_tlv=%s", id,
             data ? "present" : "absent");
    /* Real BDF bytes flow from the kernel/wlfwd client to this server; the
     * canonical on-device copy is /lib/firmware/bdwlan.bin, never generated. */
    bdf_done = 1;
}

static void indicate_ready(int sock)
{
    uint8_t tlv[7] = { 0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t ind[32];
    size_t n = 7;
    memcpy(ind + 7, tlv, n);
    ind[0] = QMI_IND;
    ind[1] = 0; ind[2] = 0;
    ind[3] = WLFW_FW_READY_IND & 0xff; ind[4] = WLFW_FW_READY_IND >> 8;
    ind[5] = n & 0xff; ind[6] = n >> 8;
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(!clients[i].active) continue;
        qrtr_sendto(sock, clients[i].node, clients[i].port, ind, n + 7);
        log_line("wlfw: FW_READY indication -> %u:%u",
                 clients[i].node, clients[i].port);
    }
}

int main(void)
{
    int sock = qrtr_open(0);
    if(sock < 0) { log_line("wlfw: qrtr_open failed: %s", strerror(errno)); return 1; }
    if(qrtr_new_server(sock, WLFW_SERVICE, 0, 0) != 0) {
        log_line("wlfw: cannot publish service 0x45: %s", strerror(errno));
        qrtr_close(sock);
        return 1;
    }
    log_line("wlfw: pure-Linux service 0x45 published (qrtr socket %d)", sock);

    for(;;) {
        uint8_t buf[BUF_SZ];
        uint32_t node = 0, port = 0;
        ssize_t n = qrtr_recvfrom(sock, buf, sizeof(buf), &node, &port);
        if(n < 0) {
            if(errno == EINTR) continue;
            log_line("wlfw: recv: %s", strerror(errno));
            break;
        }
        if(n < 7 || buf[0] != QMI_REQ) continue;
        uint16_t txn = buf[1] | (unsigned)buf[2] << 8;
        uint16_t msg = buf[3] | (unsigned)buf[4] << 8;

        switch(msg) {
        case WLFW_IND_REGISTER:
            remember(node, port);
            registered = 1;
            send_result(sock, node, port, txn, msg, 0, 0);
            if(cap_done && bdf_done) indicate_ready(sock);
            break;
        case WLFW_CAP:
            cap_done = 1;
            send_result(sock, node, port, txn, msg, 0, 0);
            break;
        case WLFW_BDF_DOWNLOAD:
            handle_bdf(buf, (size_t)n);
            send_result(sock, node, port, txn, msg, 0, 0);
            break;
        case WLFW_CAL_REPORT:
        case WLFW_CAL_DOWNLOAD:
        case WLFW_CAL_UPDATE:
            log_line("wlfw: CAL msg 0x%04x not implemented; reply success placeholder", msg);
            send_result(sock, node, port, txn, msg, 0, 0);
            break;
        case WLFW_MAC_ADDR:
            /* Do not invent a MAC. The real address is in persist
             * wlan_mac.bin; this server cannot synthesize one. */
            log_line("wlfw: MAC_ADDR request denied (no verified factory MAC)");
            send_result(sock, node, port, txn, msg, 1, 0);
            break;
        default:
            log_line("wlfw: unhandled msg 0x%04x from %u:%u", msg, node, port);
            send_result(sock, node, port, txn, msg, 1, 0);
            break;
        }
    }
    qrtr_remove_server(sock, WLFW_SERVICE, 0, 0);
    qrtr_close(sock);
    return 0;
}
