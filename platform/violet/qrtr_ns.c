/* SPDX-License-Identifier: MIT */
/* platform/violet/qrtr_ns.c - userspace QRTR name service (qvendor's
 * qrtr-ns) for the stock violet kernel.
 *
 * Why this exists, measured on the phone:
 *   - /sys/module/wlan exists and dmesg says "icnss: Platform driver probed
 *     successfully"; icnss registers a QMI lookup for WLFW_SERVICE_ID_V01
 *     (the modem-hosted WLAN firmware service) during probe.
 *   - The kernel's net/qrtr has no name service (no NEW_LOOKUP handler), so
 *     that lookup is posted to the local control port (node, QRTR_PORT_CTRL)
 *     and dropped when nobody owns the port. Stock vendor images ship
 *     /vendor/bin/qrtr-ns for exactly this.
 *   - With the modem ONLINE and QRTR alive we still saw no WLFW server and no
 *     wlan0: nothing ever answered icnss's lookup.
 *
 * Claiming QRTR_PORT_CTRL has two documented effects in this kernel:
 *   1. qrtr_reset_ports() marks every other QRTR socket ENETRESET;
 *      qmi_handle_net_reset() then recreates the socket and re-sends every
 *      registered lookup, so icnss asks again even though it probed early.
 *   2. Per-endpoint HELLO state is cleared; the modem's rpmsg channel probe
 *      (which happens when the modem boots) registers the QRTR endpoint and
 *      sends HELLO, and the modem answers with its service list.
 *
 * This file only implements the name service: it never invents a server, it
 * never claims a link is up, and every packet it reports is one it received.
 */
#define _GNU_SOURCE

#include "qrtr_ns.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "violet.h"

#ifndef AF_QIPCRTR
#define AF_QIPCRTR 42
#endif

#define QRTR_NODE_BCAST  0xffffffffu
#define QRTR_PORT_CTRL   0xfffffffeu

/* Packet types, from include/uapi/linux/qrtr.h of this kernel. The numbers
 * differ from mainline (DATA is 1 here), so they are spelled out. */
enum {
    QRTR_TYPE_DATA       = 1,
    QRTR_TYPE_HELLO      = 2,
    QRTR_TYPE_BYE        = 3,
    QRTR_TYPE_NEW_SERVER = 4,
    QRTR_TYPE_DEL_SERVER = 5,
    QRTR_TYPE_DEL_CLIENT = 6,
    QRTR_TYPE_RESUME_TX  = 7,
    QRTR_TYPE_EXIT       = 8,
    QRTR_TYPE_PING       = 9,
    QRTR_TYPE_NEW_LOOKUP = 10,
    QRTR_TYPE_DEL_LOOKUP = 11,
    QRTR_TYPE_DEL_PROC   = 13
};

struct sockaddr_qrtr {
    unsigned short sq_family;
    uint32_t sq_node;
    uint32_t sq_port;
};

struct qrtr_ctrl_pkt {
    uint32_t cmd;
    union {
        struct {
            uint32_t service;
            uint32_t instance;   /* version | instance << 8 */
            uint32_t node;
            uint32_t port;
        } server;
        struct {
            uint32_t node;
            uint32_t port;
        } client;
        struct {
            uint32_t rsvd;
            uint32_t node;
        } proc;
    };
} __attribute__((packed));

#define NS_MAX_SERVERS 64
#define NS_MAX_LOOKUPS 32
#define NS_MAX_NODES   16

struct ns_server {
    uint32_t service;
    uint32_t verinst;
    uint32_t node;
    uint32_t port;
};

struct ns_lookup {
    uint32_t service;
    uint32_t verinst;
    uint32_t node;
    uint32_t port;
};

static int ns_started;
static volatile int ns_bound;
static int ns_fd = -1;
static uint32_t ns_nid;
static pthread_t ns_thread;
static int ns_thread_started;

static struct ns_server ns_servers[NS_MAX_SERVERS];
static int ns_server_count;
static struct ns_lookup ns_lookups[NS_MAX_LOOKUPS];
static int ns_lookup_count;
static uint32_t ns_nodes[NS_MAX_NODES];
static int ns_node_count;

/* Census of what the control port actually received, by packet type. */
static unsigned ns_pkts[16];
static unsigned ns_servers_seen;
static unsigned ns_lookups_seen;
static unsigned ns_forwards;
static unsigned ns_short_pkts;
static unsigned ns_rebinds;
static char ns_last[96] = "not started";

static void ns_addf(char *out, int cap, const char *fmt, ...)
{
    va_list args;
    int used;

    if(out == NULL || cap <= 0) return;
    used = (int)strlen(out);
    if(used >= cap - 1) return;
    va_start(args, fmt);
    vsnprintf(out + used, (size_t)(cap - used), fmt, args);
    va_end(args);
}

static int addr_make(struct sockaddr_qrtr *addr, uint32_t node, uint32_t port)
{
    memset(addr, 0, sizeof(*addr));
    addr->sq_family = AF_QIPCRTR;
    addr->sq_node = node;
    addr->sq_port = port;
    return 0;
}

static int ns_send(const struct qrtr_ctrl_pkt *pkt, uint32_t node,
                   uint32_t port)
{
    struct sockaddr_qrtr dst;
    ssize_t n;

    if(ns_fd < 0) return -1;
    addr_make(&dst, node, port);
    n = sendto(ns_fd, pkt, sizeof(*pkt), 0, (struct sockaddr *)&dst,
               sizeof(dst));
    if(n != (ssize_t)sizeof(*pkt)) return -1;
    return 0;
}

/* ---- node / server / lookup tables ------------------------------------ */

static int node_is_remote(uint32_t node)
{
    if(node == ns_nid || node == QRTR_NODE_BCAST || node == QRTR_PORT_CTRL)
        return 0;
    return 1;
}

static void node_note(uint32_t node)
{
    int i;

    if(!node_is_remote(node)) return;
    for(i = 0; i < ns_node_count; i++)
        if(ns_nodes[i] == node) return;
    if(ns_node_count >= NS_MAX_NODES) return;
    ns_nodes[ns_node_count++] = node;
    vio_log("qrtr-ns: remote node 0x%x", node);
}

static void server_add(uint32_t service, uint32_t verinst, uint32_t node,
                       uint32_t port)
{
    struct ns_server *slot = NULL;
    int i;

    for(i = 0; i < ns_server_count; i++) {
        if(ns_servers[i].service == service &&
           ns_servers[i].verinst >> 8 == verinst >> 8 &&
           ns_servers[i].node == node && ns_servers[i].port == port) {
            slot = &ns_servers[i];
            break;
        }
    }
    if(slot == NULL) {
        if(ns_server_count >= NS_MAX_SERVERS) return;
        slot = &ns_servers[ns_server_count++];
        ns_servers_seen++;
    }
    slot->service = service;
    slot->verinst = verinst;
    slot->node = node;
    slot->port = port;

    vio_log("qrtr-ns: server svc=0x%x inst=0x%x node=0x%x port=0x%x",
            service, verinst >> 8, node, port);
    snprintf(ns_last, sizeof(ns_last), "server 0x%x from node 0x%x",
             service, node);
}

static void server_drop(uint32_t node, uint32_t port)
{
    int i = 0;

    while(i < ns_server_count) {
        if(ns_servers[i].node == node &&
           (port == QRTR_NODE_BCAST || ns_servers[i].port == port)) {
            memmove(&ns_servers[i], &ns_servers[i + 1],
                    sizeof(ns_servers[0]) * (size_t)(ns_server_count - i - 1));
            ns_server_count--;
            continue;
        }
        i++;
    }
}

static void lookup_add(uint32_t service, uint32_t verinst, uint32_t node,
                       uint32_t port)
{
    struct ns_lookup *slot = NULL;
    int i;

    for(i = 0; i < ns_lookup_count; i++) {
        if(ns_lookups[i].service == service &&
           ns_lookups[i].verinst == verinst &&
           ns_lookups[i].node == node && ns_lookups[i].port == port) {
            slot = &ns_lookups[i];
            break;
        }
    }
    if(slot == NULL) {
        if(ns_lookup_count >= NS_MAX_LOOKUPS) return;
        slot = &ns_lookups[ns_lookup_count++];
        ns_lookups_seen++;
    }
    slot->service = service;
    slot->verinst = verinst;
    slot->node = node;
    slot->port = port;

    vio_log("qrtr-ns: lookup svc=0x%x inst=0x%x from node 0x%x port 0x%x",
            service, verinst >> 8, node, port);
    snprintf(ns_last, sizeof(ns_last), "lookup 0x%x from port 0x%x",
             service, port);
}

static void lookup_drop(uint32_t node, uint32_t port)
{
    int i = 0;

    while(i < ns_lookup_count) {
        if(ns_lookups[i].node == node &&
           (port == QRTR_NODE_BCAST || ns_lookups[i].port == port)) {
            memmove(&ns_lookups[i], &ns_lookups[i + 1],
                    sizeof(ns_lookups[0]) * (size_t)(ns_lookup_count - i - 1));
            ns_lookup_count--;
            continue;
        }
        i++;
    }
}

static int svc_matches(uint32_t service, uint32_t verinst, uint32_t other,
                       uint32_t other_verinst)
{
    if(service != other) return 0;
    /* Instance is the byte above the version field; the kernel QMI helper
     * encodes both the same way for lookups and servers. Version is not
     * part of the match, matching the in-kernel name server. */
    return (verinst >> 8) == (other_verinst >> 8);
}

static int lookup_matches(const struct ns_lookup *lookup, uint32_t service,
                          uint32_t verinst)
{
    return svc_matches(lookup->service, lookup->verinst, service, verinst);
}

/* Send NEW_SERVER to every local client that asked for this service. */
static void server_announce(uint32_t service, uint32_t verinst, uint32_t node,
                            uint32_t port)
{
    struct qrtr_ctrl_pkt pkt;
    int i;

    memset(&pkt, 0, sizeof(pkt));
    pkt.cmd = QRTR_TYPE_NEW_SERVER;
    pkt.server.service = service;
    pkt.server.instance = verinst;
    pkt.server.node = node;
    pkt.server.port = port;

    for(i = 0; i < ns_lookup_count; i++) {
        if(!lookup_matches(&ns_lookups[i], service, verinst)) continue;
        if(ns_lookups[i].node != ns_nid) continue;
        if(ns_send(&pkt, ns_lookups[i].node, ns_lookups[i].port) == 0) {
            vio_log("qrtr-ns: new_server svc=0x%x -> port 0x%x", service,
                    ns_lookups[i].port);
        }
    }
}

/* Ask one remote endpoint for matching services. Sending a control packet is
 * what drives the kernel's HELLO handshake for a freshly booted modem. */
static void lookup_forward(uint32_t service, uint32_t verinst)
{
    struct qrtr_ctrl_pkt pkt;
    int i;

    memset(&pkt, 0, sizeof(pkt));
    pkt.cmd = QRTR_TYPE_NEW_LOOKUP;
    pkt.server.service = service;
    pkt.server.instance = verinst;

    if(ns_node_count == 0) {
        /* No node id learnt yet: the kernel broadcast walks every endpoint
         * and is the only way to reach a modem that has not spoken. */
        if(ns_send(&pkt, QRTR_NODE_BCAST, QRTR_PORT_CTRL) == 0) ns_forwards++;
        return;
    }
    for(i = 0; i < ns_node_count; i++) {
        if(ns_send(&pkt, ns_nodes[i], QRTR_PORT_CTRL) == 0) ns_forwards++;
    }
}

static void send_all_servers(uint32_t node)
{
    struct qrtr_ctrl_pkt pkt;
    int i;

    memset(&pkt, 0, sizeof(pkt));
    pkt.cmd = QRTR_TYPE_NEW_SERVER;
    for(i = 0; i < ns_server_count; i++) {
        pkt.server.service = ns_servers[i].service;
        pkt.server.instance = ns_servers[i].verinst;
        pkt.server.node = ns_servers[i].node;
        pkt.server.port = ns_servers[i].port;
        (void)ns_send(&pkt, node, QRTR_PORT_CTRL);
    }
}

/* ---- packet handling -------------------------------------------------- */

static void handle_hello(const struct sockaddr_qrtr *from)
{
    struct qrtr_ctrl_pkt pkt;
    int i;

    if(!node_is_remote(from->sq_node)) return;
    vio_log("qrtr-ns: hello from node 0x%x", from->sq_node);
    node_note(from->sq_node);
    send_all_servers(from->sq_node);

    /* A node that just joined has not seen lookups registered before it
     * existed. Replay them, exactly like an announce would. */
    memset(&pkt, 0, sizeof(pkt));
    pkt.cmd = QRTR_TYPE_NEW_LOOKUP;
    for(i = 0; i < ns_lookup_count; i++) {
        pkt.server.service = ns_lookups[i].service;
        pkt.server.instance = ns_lookups[i].verinst;
        (void)ns_send(&pkt, from->sq_node, QRTR_PORT_CTRL);
        ns_forwards++;
    }
}

static void handle_lookup(const struct qrtr_ctrl_pkt *pkt,
                          const struct sockaddr_qrtr *from)
{
    struct qrtr_ctrl_pkt reply;
    uint32_t service = pkt->server.service;
    uint32_t verinst = pkt->server.instance;
    int i;
    int answered = 0;

    /* Control-port traffic is our own broadcast echo or a remote name
     * server; recording it would loop the lookup back into the fabric. */
    if(from->sq_port == QRTR_PORT_CTRL) return;
    if(from->sq_node != ns_nid) return;

    lookup_add(service, verinst, from->sq_node, from->sq_port);

    memset(&reply, 0, sizeof(reply));
    reply.cmd = QRTR_TYPE_NEW_SERVER;
    for(i = 0; i < ns_server_count; i++) {
        if(!svc_matches(service, verinst, ns_servers[i].service,
                        ns_servers[i].verinst))
            continue;
        reply.server.service = ns_servers[i].service;
        reply.server.instance = ns_servers[i].verinst;
        reply.server.node = ns_servers[i].node;
        reply.server.port = ns_servers[i].port;
        (void)ns_send(&reply, from->sq_node, from->sq_port);
        answered++;
    }
    if(answered > 0)
        vio_log("qrtr-ns: answered lookup svc=0x%x from cache (%d)",
                service, answered);

    lookup_forward(service, verinst);
}

static void handle_new_server(const struct qrtr_ctrl_pkt *pkt)
{
    uint32_t service = pkt->server.service;
    uint32_t verinst = pkt->server.instance;
    uint32_t node = pkt->server.node;
    uint32_t port = pkt->server.port;

    server_add(service, verinst, node, port);
    if(node != ns_nid) node_note(node);
    server_announce(service, verinst, node, port);
}

static void handle_del_server(const struct qrtr_ctrl_pkt *pkt)
{
    struct qrtr_ctrl_pkt out;
    int i;

    memset(&out, 0, sizeof(out));
    out.cmd = QRTR_TYPE_DEL_SERVER;
    out.server.service = pkt->server.service;
    out.server.instance = pkt->server.instance;
    out.server.node = pkt->server.node;
    out.server.port = pkt->server.port;

    for(i = 0; i < ns_lookup_count; i++) {
        if(!lookup_matches(&ns_lookups[i], pkt->server.service,
                           pkt->server.instance))
            continue;
        (void)ns_send(&out, ns_lookups[i].node, ns_lookups[i].port);
    }
    server_drop(pkt->server.node, pkt->server.port);
    vio_log("qrtr-ns: server gone svc=0x%x node=0x%x",
            pkt->server.service, pkt->server.node);
}

static void handle_ctrl(const struct qrtr_ctrl_pkt *pkt, size_t len,
                        const struct sockaddr_qrtr *from)
{
    uint32_t cmd = pkt->cmd;

    if(len < sizeof(*pkt)) {
        ns_short_pkts++;
        return;
    }
    if(cmd < 16) ns_pkts[cmd]++;

    switch(cmd) {
    case QRTR_TYPE_HELLO:
        handle_hello(from);
        break;
    case QRTR_TYPE_NEW_LOOKUP:
        handle_lookup(pkt, from);
        break;
    case QRTR_TYPE_DEL_LOOKUP:
        lookup_drop(from->sq_node, from->sq_port);
        break;
    case QRTR_TYPE_NEW_SERVER:
        handle_new_server(pkt);
        break;
    case QRTR_TYPE_DEL_SERVER:
        handle_del_server(pkt);
        break;
    case QRTR_TYPE_BYE:
        vio_log("qrtr-ns: bye from node 0x%x", pkt->client.node);
        server_drop(pkt->client.node, QRTR_NODE_BCAST);
        lookup_drop(pkt->client.node, QRTR_NODE_BCAST);
        break;
    case QRTR_TYPE_DEL_CLIENT:
        lookup_drop(pkt->client.node, pkt->client.port);
        break;
    default:
        break;
    }
}

/* ---- worker ------------------------------------------------------------ */

static int ns_bind(void)
{
    struct sockaddr_qrtr me;
    struct sockaddr_qrtr addr;
    socklen_t len = sizeof(me);

    memset(&me, 0, sizeof(me));
    if(getsockname(ns_fd, (struct sockaddr *)&me, &len) < 0) {
        vio_log("qrtr-ns: getsockname failed: %s", strerror(errno));
        return -1;
    }
    if(me.sq_family != AF_QIPCRTR || len != sizeof(me)) {
        vio_log("qrtr-ns: unexpected local address (family %u, len %u)",
                (unsigned)me.sq_family, (unsigned)len);
        return -1;
    }
    ns_nid = me.sq_node;

    addr_make(&addr, ns_nid, QRTR_PORT_CTRL);
    if(bind(ns_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        vio_log("qrtr-ns: bind ctrl port on node 0x%x failed: %s", ns_nid,
                strerror(errno));
        return -1;
    }
    ns_bound = 1;
    vio_log("qrtr-ns: bound node=0x%x port=0x%x (%s)", ns_nid,
            QRTR_PORT_CTRL, "name service up");
    return 0;
}

static int ns_socket(void)
{
    ns_fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
    if(ns_fd < 0) {
        vio_log("qrtr-ns: socket(AF_QIPCRTR) failed: %s", strerror(errno));
        return -1;
    }
    return ns_bind();
}

static void *ns_worker(void *arg)
{
    (void)arg;

    for(;;) {
        struct pollfd pfd;
        struct sockaddr_qrtr from;
        struct qrtr_ctrl_pkt pkt;
        struct iovec iov;
        struct msghdr msg;
        ssize_t n;
        int rc;

        if(ns_fd < 0) {
            vio_sleep_ms(500);
            if(ns_socket() != 0) return NULL;
        }

        pfd.fd = ns_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        rc = poll(&pfd, 1, 1000);
        if(rc < 0) {
            if(errno == EINTR) continue;
            vio_log("qrtr-ns: poll failed: %s", strerror(errno));
            vio_sleep_ms(500);
            continue;
        }
        if(rc == 0) continue;
        if(pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            /* ENETRESET means a new control port was claimed. Rebind, as the
             * kernel QMI helper does, and keep the cached lookups. */
            ns_rebinds++;
            close(ns_fd);
            ns_fd = -1;
            ns_bound = 0;
            continue;
        }
        if(!(pfd.revents & POLLIN)) continue;

        memset(&msg, 0, sizeof(msg));
        memset(&from, 0, sizeof(from));
        iov.iov_base = &pkt;
        iov.iov_len = sizeof(pkt);
        msg.msg_name = &from;
        msg.msg_namelen = sizeof(from);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        n = recvmsg(ns_fd, &msg, MSG_DONTWAIT);
        if(n < 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                continue;
            if(errno == ENETRESET) {
                ns_rebinds++;
                close(ns_fd);
                ns_fd = -1;
                ns_bound = 0;
                continue;
            }
            vio_log("qrtr-ns: recvmsg failed: %s", strerror(errno));
            continue;
        }
        if(n == 0) continue;
        handle_ctrl(&pkt, (size_t)n, &from);
    }
    return NULL;
}

/* ---- public API -------------------------------------------------------- */

int vio_qrtr_ns_start(void)
{
    if(ns_started) return ns_bound ? 0 : -1;
    ns_started = 1;

    if(ns_socket() != 0) {
        close(ns_fd);
        ns_fd = -1;
        return -1;
    }
    if(pthread_create(&ns_thread, NULL, ns_worker, NULL) != 0) {
        vio_log("qrtr-ns: cannot start worker: %s", strerror(errno));
        return -1;
    }
    pthread_detach(ns_thread);
    ns_thread_started = 1;
    return 0;
}

int vio_qrtr_ns_ready(void)
{
    return ns_bound && ns_thread_started;
}

void vio_qrtr_ns_report(char *out, int cap)
{
    int i;

    if(out == NULL || cap <= 0) return;
    ns_addf(out, cap,
            "qrtr-ns: nid=0x%x bound=%d lookups=%d servers=%d nodes=%d "
            "fwd=%u rebinds=%u short=%u\n",
            ns_nid, ns_bound, ns_lookup_count, ns_server_count, ns_node_count,
            ns_forwards, ns_rebinds, ns_short_pkts);
    ns_addf(out, cap,
            "qrtr-ns pkts: hello=%u lookup=%u server=%u del_server=%u "
            "bye=%u del_client=%u\n",
            ns_pkts[QRTR_TYPE_HELLO], ns_pkts[QRTR_TYPE_NEW_LOOKUP],
            ns_pkts[QRTR_TYPE_NEW_SERVER], ns_pkts[QRTR_TYPE_DEL_SERVER],
            ns_pkts[QRTR_TYPE_BYE], ns_pkts[QRTR_TYPE_DEL_CLIENT]);
    ns_addf(out, cap, "qrtr-ns last: %s\n", ns_last);

    if(ns_lookup_count > 0) {
        ns_addf(out, cap, "qrtr-ns lookups:");
        for(i = 0; i < ns_lookup_count; i++)
            ns_addf(out, cap, " 0x%x/0x%x->0x%x",
                    ns_lookups[i].service, ns_lookups[i].verinst >> 8,
                    ns_lookups[i].port);
        ns_addf(out, cap, "\n");
    }
    if(ns_server_count > 0) {
        ns_addf(out, cap, "qrtr-ns servers:");
        for(i = 0; i < ns_server_count; i++)
            ns_addf(out, cap, " 0x%x/0x%x@0x%x:0x%x",
                    ns_servers[i].service, ns_servers[i].verinst >> 8,
                    ns_servers[i].node, ns_servers[i].port);
        ns_addf(out, cap, "\n");
    }
    if(ns_node_count > 0) {
        ns_addf(out, cap, "qrtr-ns nodes:");
        for(i = 0; i < ns_node_count; i++)
            ns_addf(out, cap, " 0x%x", ns_nodes[i]);
        ns_addf(out, cap, "\n");
    }
}
