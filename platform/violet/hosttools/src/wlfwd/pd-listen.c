// SPDX-License-Identifier: GPL-2.0-only
/*
 * pd-listen: register an AP listener for the real WLAN protection domain.
 *
 * Direct mode: pd-listen <node> <port> <service-path>
 * Watch mode:  pd-listen --watch <service-path>
 *
 * Watch mode subscribes to QRTR service 0x42 and registers only after the
 * kernel QRTR name service reports a real server. It never publishes or
 * fabricates a service endpoint and never issues SERVREG_RESTART_PD.
 */
#include <sys/socket.h>
#include <sys/time.h>
#include <linux/qrtr.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libqrtr.h>

#define SERVREG_SERVICE 0x42
#define SERVREG_VERSION 0 /* QRTR version 0 means any published version */
#define SERVREG_REGISTER_LISTENER_REQ 0x0020
#define MAX_TRACKED 32

struct endpoint {
    uint32_t node;
    uint32_t port;
    int used;
};

static uint16_t next_txn = 1;

static int register_listener(int sock, uint32_t node, uint32_t port,
                             const char *service_path)
{
    struct sockaddr_qrtr dst = {
        .sq_family = AF_QIPCRTR,
        .sq_node = node,
        .sq_port = port
    };
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    uint8_t req[320], resp[512];
    size_t path_len = strlen(service_path);
    size_t tlv_len = 4 + 3 + path_len;
    size_t req_len = 7 + tlv_len;
    uint16_t txn = next_txn++;
    ssize_t n;

    if (path_len == 0 || path_len > 64) return -1;
    req[0] = 0;                         /* QMI request */
    req[1] = txn & 0xff;
    req[2] = txn >> 8;
    req[3] = SERVREG_REGISTER_LISTENER_REQ & 0xff;
    req[4] = SERVREG_REGISTER_LISTENER_REQ >> 8;
    req[5] = tlv_len & 0xff;
    req[6] = tlv_len >> 8;
    req[7] = 0x01; req[8] = 1; req[9] = 0; req[10] = 1;
    req[11] = 0x02;
    req[12] = path_len & 0xff;
    req[13] = path_len >> 8;
    memcpy(req + 14, service_path, path_len);

    if (sendto(sock, req, req_len, 0, (void *)&dst, sizeof(dst)) < 0) {
        fprintf(stderr, "pd-listen: send to %u:%u failed: %s\n",
                node, port, strerror(errno));
        return -1;
    }

    (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    for (;;) {
        struct sockaddr_qrtr from;
        socklen_t from_len = sizeof(from);
        n = recvfrom(sock, resp, sizeof(resp), 0, (void *)&from, &from_len);
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "pd-listen: response from %u:%u timed out/failed: %s\n",
                    node, port, strerror(errno));
            return -1;
        }
        if (from.sq_node != node || from.sq_port != port || n < 14)
            continue;
        if (resp[0] != 2 || (resp[1] | (uint16_t)resp[2] << 8) != txn ||
            (resp[3] | (uint16_t)resp[4] << 8) != SERVREG_REGISTER_LISTENER_REQ)
            continue;
        if (resp[7] != 0x02 || resp[8] != 4 || resp[9] != 0)
            continue;
        {
            unsigned result = resp[10] | (unsigned)resp[11] << 8;
            unsigned error = resp[12] | (unsigned)resp[13] << 8;
            printf("pd-listen: %s via servreg %u:%u result=%u error=%u\n",
                   service_path, node, port, result, error);
            fflush(stdout);
            return result == 0 ? 0 : -1;
        }
    }
}

static int watch_servreg(const char *service_path)
{
    struct endpoint seen[MAX_TRACKED] = {{0}};
    struct qrtr_packet pkt;
    uint8_t buf[512];
    uint32_t node, port;
    int sock = qrtr_open(0);

    if (sock < 0) {
        fprintf(stderr, "pd-listen: cannot open QRTR socket: %s\n",
                strerror(errno));
        return 1;
    }
    if (qrtr_new_lookup(sock, SERVREG_SERVICE, SERVREG_VERSION, 0) < 0) {
        fprintf(stderr, "pd-listen: SERVREG lookup failed: %s\n",
                strerror(errno));
        qrtr_close(sock);
        return 1;
    }
    printf("pd-listen: watching real QRTR service 0x%x for %s\n",
           SERVREG_SERVICE, service_path);
    fflush(stdout);

    for (;;) {
        int ready = qrtr_poll(sock, 1000);
        int n;
        if (ready < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "pd-listen: QRTR poll failed: %s\n",
                    strerror(errno));
            break;
        }
        if (ready == 0) continue;
        n = qrtr_recvfrom(sock, buf, sizeof(buf), &node, &port);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            fprintf(stderr, "pd-listen: QRTR receive failed: %s\n",
                    strerror(errno));
            break;
        }
        {
            struct sockaddr_qrtr from = {
                .sq_family = AF_QIPCRTR,
                .sq_node = node,
                .sq_port = port
            };
            if (qrtr_decode(&pkt, buf, (size_t)n, &from) < 0 ||
                port != QRTR_PORT_CTRL || pkt.service != SERVREG_SERVICE)
                continue;
        }
        if (pkt.type == QRTR_TYPE_NEW_SERVER) {
            int slot = -1, duplicate = 0;
            for (int i = 0; i < MAX_TRACKED; i++) {
                if (seen[i].used && seen[i].node == pkt.node &&
                    seen[i].port == pkt.port) duplicate = 1;
                if (!seen[i].used && slot < 0) slot = i;
            }
            if (duplicate) continue;
            printf("pd-listen: SERVREG NEW_SERVER node=%u port=%u version=%u instance=%u\n",
                   pkt.node, pkt.port, pkt.version, pkt.instance);
            fflush(stdout);
            if (register_listener(sock, pkt.node, pkt.port, service_path) == 0 &&
                slot >= 0) {
                seen[slot].node = pkt.node;
                seen[slot].port = pkt.port;
                seen[slot].used = 1;
            }
        } else if (pkt.type == QRTR_TYPE_DEL_SERVER) {
            for (int i = 0; i < MAX_TRACKED; i++) {
                if (seen[i].used && seen[i].node == pkt.node &&
                    seen[i].port == pkt.port)
                    seen[i].used = 0;
            }
            printf("pd-listen: SERVREG DEL_SERVER node=%u port=%u\n",
                   pkt.node, pkt.port);
            fflush(stdout);
        }
    }
    qrtr_close(sock);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--watch") == 0)
        return watch_servreg(argv[2]);
    if (argc == 4) {
        char *end_node, *end_port;
        unsigned long node = strtoul(argv[1], &end_node, 0);
        unsigned long port = strtoul(argv[2], &end_port, 0);
        int sock;
        if (*end_node || *end_port || node > UINT32_MAX || port > UINT32_MAX) {
            fprintf(stderr, "pd-listen: invalid node/port\n");
            return 1;
        }
        sock = qrtr_open(0);
        if (sock < 0) return 1;
        {
            int rc = register_listener(sock, (uint32_t)node, (uint32_t)port,
                                       argv[3]);
            qrtr_close(sock);
            return rc == 0 ? 0 : 1;
        }
    }
    fprintf(stderr,
            "usage: %s <node> <port> <service-path> | --watch <service-path>\n",
            argv[0]);
    return 1;
}
