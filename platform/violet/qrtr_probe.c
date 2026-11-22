/* SPDX-License-Identifier: MIT */
/*
 * qrtr_probe.c — real AF_QIPCRTR IO for the native QRTR discovery client.
 * No server is published and no response is fabricated. The measured report
 * states exactly what the socket sent/received and how long it took.
 */
#define _GNU_SOURCE
#include "qrtr_probe.h"
#include "violet.h"
#include "vqeaf/qrtr_transport.h"

#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef AF_QIPCRTR
#define AF_QIPCRTR 42
#endif

struct sockaddr_qrtr {
    unsigned short sq_family;
    uint32_t sq_node;
    uint32_t sq_port;
};

typedef struct {
    int fd;
    uint32_t local_node;
    uint64_t started_ms;
    unsigned recv_calls;
    size_t recv_bytes;
    uint32_t last_cmd;
    int have_cmd;
} live_t;

static uint64_t now_ms(void *p)
{
    struct timespec ts;
    (void)p;
    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000L);
}

static int live_send(void *p, const vqeaf_qrtr_addr_t *dst,
                     const uint8_t *buf, size_t len)
{
    live_t *l = (live_t *)p;
    struct sockaddr_qrtr sa;
    ssize_t n;
    memset(&sa, 0, sizeof(sa));
    sa.sq_family = AF_QIPCRTR;
    sa.sq_node = dst->node;
    sa.sq_port = dst->port;
    n = sendto(l->fd, buf, len, 0, (struct sockaddr *)&sa, sizeof(sa));
    if(n != (ssize_t)len) return n < 0 ? -errno : -EIO;
    return 0;
}

static int live_recv(void *p, uint8_t *buf, size_t cap, size_t *out_len,
                     vqeaf_qrtr_addr_t *src, uint32_t timeout_ms)
{
    live_t *l = (live_t *)p;
    struct pollfd pfd;
    struct sockaddr_qrtr from;
    struct iovec iov;
    struct msghdr msg;
    ssize_t n;
    int rc;

    pfd.fd = l->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    rc = poll(&pfd, 1, (int)timeout_ms);
    if(rc < 0) return errno == EINTR ? -EINTR : -errno;
    if(rc == 0) { *out_len = 0; return 0; } /* real deadline, no fake packet */
    if(pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return -ENODEV;
    if(!(pfd.revents & POLLIN)) { *out_len = 0; return 0; }

    memset(&msg, 0, sizeof(msg));
    memset(&from, 0, sizeof(from));
    iov.iov_base = buf;
    iov.iov_len = cap;
    msg.msg_name = &from;
    msg.msg_namelen = sizeof(from);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    n = recvmsg(l->fd, &msg, MSG_DONTWAIT);
    if(n < 0) {
        if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            *out_len = 0; return 0;
        }
        if(errno == ENETRESET || errno == ECONNRESET) return -ENETRESET;
        return -errno;
    }
    l->recv_calls++;
    l->recv_bytes += (size_t)n;
    if(src != NULL) { src->node = from.sq_node; src->port = from.sq_port; }
    if(n >= 4) {
        const uint8_t *b = (const uint8_t *)buf;
        l->last_cmd = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                      ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
        l->have_cmd = 1;
    }
    *out_len = (size_t)n;
    return 0;
}

static void addf(char *out, size_t cap, const char *fmt, ...)
{
    va_list ap;
    size_t used = strlen(out);
    int n;
    if(used + 1u >= cap) return;
    va_start(ap, fmt);
    n = vsnprintf(out + used, cap - used, fmt, ap);
    va_end(ap);
    if(n < 0) out[used] = '\0';
}

int vio_qrtr_discover_live(uint32_t service, uint32_t instance,
                           uint32_t version, uint32_t timeout_ms,
                           char *out, size_t cap)
{
    live_t l;
    vqeaf_qrtr_io_t io;
    vqeaf_qrtr_server_t srv;
    struct sockaddr_qrtr local;
    socklen_t sl = sizeof(local);
    int rc, found;
    uint64_t elapsed;

    if(out == NULL || cap < 32u || timeout_ms == 0) return -EINVAL;
    out[0] = '\0';
    memset(&l, 0, sizeof(l));
    l.fd = socket(AF_QIPCRTR, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if(l.fd < 0) {
        addf(out, cap, "qrtr-live: socket(AF_QIPCRTR) failed errno=%d\n", errno);
        return -errno;
    }
    memset(&local, 0, sizeof(local));
    if(getsockname(l.fd, (struct sockaddr *)&local, &sl) < 0 ||
       sl != sizeof(local) || local.sq_family != AF_QIPCRTR) {
        addf(out, cap, "qrtr-live: getsockname failed errno=%d\n", errno);
        close(l.fd);
        return -errno;
    }
    l.local_node = local.sq_node;
    l.started_ms = now_ms(NULL);
    io.ctx = &l;
    io.local_node = l.local_node;
    io.send = live_send;
    io.recv = live_recv;
    io.now_ms = now_ms;

    vqeaf_qrtr_table_reset();
    rc = vqeaf_qrtr_discover(&io, service, instance, version,
                             timeout_ms, &srv);
    found = (rc == 0);
    elapsed = now_ms(NULL) - l.started_ms;

    addf(out, cap,
         "qrtr-live: service=0x%x instance=%u version=%u local_node=0x%x\n",
         service, instance, version, l.local_node);
    addf(out, cap,
         "qrtr-live: lookup_sent=1 timeout_ms=%u elapsed_ms=%llu "
         "recv_calls=%u recv_bytes=%u\n",
         timeout_ms, (unsigned long long)elapsed, l.recv_calls,
         (unsigned)l.recv_bytes);
    if(l.have_cmd)
        addf(out, cap, "qrtr-live: last_ctrl_cmd=%u\n", l.last_cmd);
    if(found)
        addf(out, cap, "qrtr-live: server=FOUND node=0x%x port=0x%x VERIFIED\n",
             srv.node, srv.port);
    else
        addf(out, cap,
             "qrtr-live: server=NOT_REGISTERED rc=%d (%s) BLOCKED\n",
             rc, rc == -ENOENT ? "ENOENT" : "TRANSPORT_ERROR");
    addf(out, cap, "qrtr-live: note=only received NEW_SERVER can verify\n");

    vio_log("qrtr-live: svc=0x%x rc=%d elapsed=%ums recv=%u %s",
            service, rc, (unsigned)elapsed, l.recv_calls,
            found ? "VERIFIED" : "BLOCKED");
    close(l.fd);
    return rc;
}
