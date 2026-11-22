/* SPDX-License-Identifier: MIT */
/*
 * qrtr_transport.c — native QRTR control codec + read-only server discovery.
 * Only control packets actually received can enter the server table; this
 * module never publishes a service and never invents a node/port answer.
 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "vqeaf/qrtr_transport.h"

#define QRTR_MAX_SERVERS 16u

static vqeaf_qrtr_server_t g_servers[QRTR_MAX_SERVERS];
static unsigned g_server_count;

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int vqeaf_qrtr_ctrl_encode(uint8_t *buf, size_t cap,
                           const vqeaf_qrtr_ctrl_t *in, size_t *out_len)
{
    if(buf == NULL || in == NULL || cap < VQEAF_QRTR_CTRL_SIZE) return -EINVAL;
    put_le32(buf, in->cmd);
    put_le32(buf + 4, in->service);
    put_le32(buf + 8, in->verinst);
    put_le32(buf + 12, in->node);
    put_le32(buf + 16, in->port);
    if(out_len != NULL) *out_len = VQEAF_QRTR_CTRL_SIZE;
    return 0;
}

int vqeaf_qrtr_ctrl_decode(const uint8_t *buf, size_t len,
                           vqeaf_qrtr_ctrl_t *out)
{
    if(buf == NULL || out == NULL || len < VQEAF_QRTR_CTRL_SIZE) return -EINVAL;
    out->cmd = get_le32(buf);
    out->service = get_le32(buf + 4);
    out->verinst = get_le32(buf + 8);
    out->node = get_le32(buf + 12);
    out->port = get_le32(buf + 16);
    return 0;
}

void vqeaf_qrtr_table_reset(void)
{
    memset(g_servers, 0, sizeof(g_servers));
    g_server_count = 0;
}

int vqeaf_qrtr_table_add(const vqeaf_qrtr_ctrl_t *pkt)
{
    unsigned i;
    uint32_t service = pkt->service;
    uint32_t instance = pkt->verinst >> 8;
    uint32_t version = pkt->verinst & 0xffu;

    if(pkt == NULL || service == 0 || pkt->node == 0 || pkt->port == 0)
        return -EINVAL;
    for(i = 0; i < g_server_count; i++) {
        if(g_servers[i].service == service &&
           g_servers[i].instance == instance &&
           g_servers[i].node == pkt->node && g_servers[i].port == pkt->port) {
            g_servers[i].version = version;
            return 0;
        }
    }
    if(g_server_count >= QRTR_MAX_SERVERS) return -ENOSPC;
    g_servers[g_server_count].service = service;
    g_servers[g_server_count].instance = instance;
    g_servers[g_server_count].version = version;
    g_servers[g_server_count].node = pkt->node;
    g_servers[g_server_count].port = pkt->port;
    g_server_count++;
    return 0;
}

int vqeaf_qrtr_table_drop(const vqeaf_qrtr_ctrl_t *pkt)
{
    unsigned i;
    if(pkt == NULL) return -EINVAL;
    for(i = 0; i < g_server_count; i++) {
        if(g_servers[i].service != pkt->service ||
           g_servers[i].instance != (pkt->verinst >> 8)) continue;
        if(pkt->node != 0 && g_servers[i].node != pkt->node) continue;
        if(pkt->port != 0 && g_servers[i].port != pkt->port) continue;
        /* Manual shift: avoids pulling libc memmove into the freestanding ELF. */
        {
            unsigned k;
            for(k = i; k + 1u < g_server_count; k++)
                g_servers[k] = g_servers[k + 1u];
        }
        g_server_count--;
        return 0;
    }
    return -ENOENT;
}

int vqeaf_qrtr_table_find(uint32_t service, uint32_t instance,
                          vqeaf_qrtr_server_t *out)
{
    unsigned i;
    if(service == 0) return -EINVAL;
    for(i = 0; i < g_server_count; i++) {
        if(g_servers[i].service != service ||
           (instance != 0 && g_servers[i].instance != instance)) continue;
        if(out != NULL) *out = g_servers[i];
        return 0;
    }
    return -ENOENT;
}

static void ingest(const uint8_t *buf, size_t len)
{
    vqeaf_qrtr_ctrl_t pkt;
    if(vqeaf_qrtr_ctrl_decode(buf, len, &pkt) != 0) return;
    if(pkt.cmd == VQEAF_QRTR_NEW_SERVER) (void)vqeaf_qrtr_table_add(&pkt);
    else if(pkt.cmd == VQEAF_QRTR_DEL_SERVER ||
            pkt.cmd == VQEAF_QRTR_BYE) (void)vqeaf_qrtr_table_drop(&pkt);
}

int vqeaf_qrtr_discover(const vqeaf_qrtr_io_t *io, uint32_t service,
                        uint32_t instance, uint32_t version,
                        uint32_t timeout_ms, vqeaf_qrtr_server_t *out)
{
    uint8_t buf[VQEAF_QRTR_CTRL_SIZE];
    vqeaf_qrtr_ctrl_t lookup;
    vqeaf_qrtr_addr_t dst;
    vqeaf_qrtr_addr_t src;
    uint64_t deadline;
    size_t len;
    int rc;

    if(io == NULL || io->send == NULL || io->recv == NULL ||
       io->now_ms == NULL || service == 0) return -EINVAL;

    memset(&lookup, 0, sizeof(lookup));
    lookup.cmd = VQEAF_QRTR_NEW_LOOKUP;
    lookup.service = service;
    lookup.verinst = ((uint32_t)(instance & 0xffu) << 8) | (version & 0xffu);
    rc = vqeaf_qrtr_ctrl_encode(buf, sizeof(buf), &lookup, &len);
    if(rc) return rc;
    dst.node = io->local_node;
    dst.port = VQEAF_QRTR_PORT_CTRL;
    rc = io->send(io->ctx, &dst, buf, len);
    if(rc) return rc;

    deadline = io->now_ms(io->ctx) + timeout_ms;
    for(;;) {
        uint64_t now = io->now_ms(io->ctx);
        uint32_t slice;
        if(now >= deadline) return -ENOENT;
        slice = (uint32_t)(deadline - now);
        memset(&src, 0, sizeof(src));
        len = 0;
        rc = io->recv(io->ctx, buf, sizeof(buf), &len, &src, slice);
        if(rc == -EINTR) continue;
        if(rc) return rc; /* -ENODEV/-ENETRESET propagated honestly */
        if(len == 0) continue;
        ingest(buf, len);
        rc = vqeaf_qrtr_table_find(service, instance, out);
        if(rc == 0) return 0;
    }
}
