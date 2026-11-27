/* SPDX-License-Identifier: MIT */
/* kernel/drivers/bluetooth/wcn3990_slim.c - btfmslim channel over qcom,slim-ngd.
 *
 * The Bluetooth driver (wcn3990_bt.c) speaks vqeaf_bt_ops_t. This file is the
 * transport implementation of that contract on violet: it opens the NGD client
 * once the controller is up, sends each HCI frame as one SLIMbus message
 * through vqeaf_slim_ngd_xfer, and reads with vqeaf_slim_ngd_rx_pop.
 *
 * What is ported is the handshake discipline of the upstream btfmslim driver:
 * never report a frame sent without the controller's TX_MSG_SENT, never return
 * a message the controller did not deliver, and refuse what the link cannot
 * carry instead of truncating it.
 *
 * NOT established here: the btfmslim logical-channel header, so a frame longer
 * than VQEAF_SLIM_NGD_MSG_MAX returns -EMSGSIZE and a received message still
 * carries its own SLIMbus header byte; and the SLIMbus enumeration of the
 * btfmslim slave, which is a provider hook (link->attach) because the client
 * contract has no enumeration verb. Both gaps are reported, not papered over. */
#include "vqeaf/wcn3990_slim.h"

#include <errno.h>
#include <string.h>

static int link_ready(const VqeafBtSlimLink *link)
{
    return link != NULL && link->ngd != NULL &&
           link->ngd->abi == VQEAF_SLIM_NGD_ABI &&
           link->ngd->size == sizeof(*link->ngd);
}

static int ea_zero(const uint8_t ea[VQEAF_BT_SLIM_EA_LEN])
{
    size_t i;
    for(i = 0; i < VQEAF_BT_SLIM_EA_LEN; i++)
        if(ea[i] != 0) return 0;
    return 1;
}

void vqeaf_bt_slim_init(VqeafBtSlimLink *link, VqeafSlimNgd *ngd)
{
    if(link == NULL) return;
    memset(link, 0, sizeof(*link));
    link->ngd = ngd;
    link->tx_retries = VQEAF_BT_SLIM_TX_RETRIES;
    link->rx_retries = VQEAF_BT_SLIM_RX_RETRIES;
    link->tx_timeout_ms = VQEAF_BT_SLIM_TX_TIMEOUT_MS;
}

int vqeaf_bt_slim_open(VqeafBtSlimLink *link,
                       const uint8_t peer[VQEAF_BT_SLIM_EA_LEN],
                       const uint8_t ifd[VQEAF_BT_SLIM_EA_LEN])
{
    int rc;

    if(!link_ready(link)) return -ENOSYS;
    if(peer == NULL || ifd == NULL) return -EINVAL;
    if(link->attached) return -EBUSY;
    if(ea_zero(peer) || ea_zero(ifd) ||
       memcmp(peer, ifd, VQEAF_BT_SLIM_EA_LEN) == 0) {
        link->last_error = -EINVAL;
        return -EINVAL;
    }
    if(link->ngd->state != VQEAF_SLIM_NGD_UP) {
        /* An unstarted client cannot carry a frame; do not pretend to attach. */
        link->last_error = -ENODEV;
        return -ENODEV;
    }
    if(link->attach != NULL) {
        rc = link->attach(link->attach_ctx, peer, ifd);
        if(rc < 0) {
            link->last_error = rc;
            return rc;
        }
    }
    memcpy(link->peer_ea, peer, VQEAF_BT_SLIM_EA_LEN);
    memcpy(link->ifd_ea, ifd, VQEAF_BT_SLIM_EA_LEN);
    link->peer_set = 1;
    link->attached = 1;
    link->last_error = 0;
    return 0;
}

void vqeaf_bt_slim_close(VqeafBtSlimLink *link)
{
    if(link == NULL) return;
    link->attached = 0;
}

/* Waits for the TX slot to clear. Only called when a wait hook exists: without
 * one, the caller gets the client's own word (queued) and nothing more. */
static int wait_tx_clear(VqeafBtSlimLink *link)
{
    uint32_t attempt;

    for(attempt = 0; attempt <= link->tx_retries && link->ngd->tx_busy; attempt++) {
        int rc = link->wait_ready(link->wait_ctx, link->tx_timeout_ms);
        if(rc < 0) return rc;
    }
    return link->ngd->tx_busy ? -ETIMEDOUT : 0;
}

int vqeaf_bt_slim_write(VqeafBtSlimLink *link, const uint8_t *buf, size_t len)
{
    uint32_t attempt;
    int rc;

    if(!link_ready(link)) return -ENOSYS;
    if(!link->attached) return -ENODEV;
    if(buf == NULL || len == 0u) return -EINVAL;
    if(len > VQEAF_SLIM_NGD_MSG_MAX) {
        /* No framing header exists in this tree to split an HCI frame. Cutting
         * it would corrupt the stream, so it is refused. */
        link->last_error = -EMSGSIZE;
        link->tx_failures++;
        return -EMSGSIZE;
    }

    for(attempt = 0;; attempt++) {
        rc = vqeaf_slim_ngd_xfer(link->ngd, buf, (uint32_t)len);
        if(rc != -EBUSY) break;
        if(link->wait_ready == NULL || attempt >= link->tx_retries) break;
        rc = link->wait_ready(link->wait_ctx, link->tx_timeout_ms);
        if(rc < 0) break;
    }
    if(rc < 0) {
        link->last_error = rc;
        link->tx_failures++;
        return rc;
    }

    link->msgs_out++;
    if(link->wait_ready != NULL) {
        rc = wait_tx_clear(link);
        if(rc < 0) {
            link->last_error = rc;
            link->tx_failures++;
            return rc;
        }
        link->tx_sent++;
    } else {
        /* Queued only: the platform has no way to observe TX_MSG_SENT yet. */
        link->tx_queued++;
    }
    link->last_error = 0;
    return 0;
}

int vqeaf_bt_slim_read(VqeafBtSlimLink *link, uint8_t *buf, size_t cap,
                       size_t *got, uint32_t timeout_ms)
{
    uint32_t attempt;

    if(!link_ready(link)) return -ENOSYS;
    if(!link->attached) return -ENODEV;
    if(buf == NULL || got == NULL || cap == 0u) return -EINVAL;
    *got = 0;

    for(attempt = 0;; attempt++) {
        uint32_t len = 0;
        int rc = vqeaf_slim_ngd_rx_pop(link->ngd, buf, (uint32_t)cap, &len);
        if(rc == 0) {
            *got = len;
            link->msgs_in++;
            link->last_error = 0;
            return 0;
        }
        if(rc != -EAGAIN) {
            /* EPROTO/ENOSPC belong to the message, not to the wait. */
            link->last_error = rc;
            return rc;
        }
        if(link->wait_ready == NULL || attempt >= link->rx_retries) break;
        rc = link->wait_ready(link->wait_ctx, timeout_ms);
        if(rc < 0) {
            link->last_error = rc;
            return rc;
        }
    }
    link->rx_timeouts++;
    link->last_error = -ETIMEDOUT;
    return -ETIMEDOUT;
}

/* ---- composed ops: platform power/firmware + SLIMbus transport ---- */

static VqeafBtSlimBridge *bridge_of(void *ctx)
{
    return (VqeafBtSlimBridge *)ctx;
}

static int b_power_on(void *ctx, const char *rail, uint32_t uv, uint32_t ua)
{
    const vqeaf_bt_ops_t *in = bridge_of(ctx)->inner;
    return in->power_on(in->ctx, rail, uv, ua);
}

static void b_power_off(void *ctx, const char *rail)
{
    const vqeaf_bt_ops_t *in = bridge_of(ctx)->inner;
    in->power_off(in->ctx, rail);
}

static int b_slim_open(void *ctx, const uint8_t *peer, const uint8_t *ifd)
{
    return vqeaf_bt_slim_open(&bridge_of(ctx)->link, peer, ifd);
}

static void b_slim_close(void *ctx)
{
    vqeaf_bt_slim_close(&bridge_of(ctx)->link);
}

static int b_write(void *ctx, const uint8_t *buf, size_t len)
{
    return vqeaf_bt_slim_write(&bridge_of(ctx)->link, buf, len);
}

static int b_read(void *ctx, uint8_t *buf, size_t cap, size_t *got,
                  uint32_t timeout_ms)
{
    return vqeaf_bt_slim_read(&bridge_of(ctx)->link, buf, cap, got, timeout_ms);
}

static int b_firmware(void *ctx, const char *name, uint8_t *buf, size_t cap,
                      size_t *len)
{
    const vqeaf_bt_ops_t *in = bridge_of(ctx)->inner;
    return in->firmware_read(in->ctx, name, buf, cap, len);
}

static void b_log(void *ctx, const char *line)
{
    const vqeaf_bt_ops_t *in = bridge_of(ctx)->inner;
    if(in->log != NULL) in->log(in->ctx, line);
}

int vqeaf_bt_slim_compose(VqeafBtSlimBridge *bridge,
                          const vqeaf_bt_ops_t *inner, VqeafSlimNgd *ngd)
{
    if(bridge == NULL || inner == NULL || ngd == NULL) return -EINVAL;
    if(inner->power_on == NULL || inner->power_off == NULL ||
       inner->firmware_read == NULL)
        return -EINVAL;

    memset(bridge, 0, sizeof(*bridge));
    bridge->inner = inner;
    vqeaf_bt_slim_init(&bridge->link, ngd);

    bridge->ops.ctx = bridge;
    bridge->ops.power_on = b_power_on;
    bridge->ops.power_off = b_power_off;
    bridge->ops.slim_open = b_slim_open;
    bridge->ops.slim_close = b_slim_close;
    bridge->ops.write = b_write;
    bridge->ops.read = b_read;
    bridge->ops.firmware_read = b_firmware;
    bridge->ops.log = b_log;
    return 0;
}
