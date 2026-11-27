/* SPDX-License-Identifier: MIT */
/* kernel/include/vqeaf/wcn3990_slim.h - btfmslim channel over the NGD client.
 *
 * The Bluetooth bring-up in kernel/drivers/bluetooth/wcn3990_bt.c knows only
 * vqeaf_bt_ops_t. This bridge is the concrete transport behind slim_open /
 * slim_close / write / read: it drives the qcom,slim-ngd client
 * (vqeaf_slim_ngd_xfer / vqeaf_slim_ngd_rx_pop) and turns the client's
 * non-blocking TX into a real "one message left the host" answer.
 *
 * Fail-closed:
 *   - The link opens only on a client that reports UP; an unstarted controller
 *     is -ENODEV, never a silent success.
 *   - 0 is returned by the client when a message is merely queued. Without a
 *     wait hook the bridge cannot prove TX_MSG_SENT, so it counts the message
 *     as queued and says so instead of claiming delivery.
 *   - A frame longer than one SLIMbus message is refused with -EMSGSIZE. The
 *     btfmslim logical-channel header that would let an HCI frame be split
 *     across messages is not established by any evidence in this tree, so it
 *     is not guessed here.
 *   - The bridge is a *message* transport: it neither adds nor removes the
 *     SLIMbus message header, and rx_pop returns the message exactly as the
 *     controller delivered it. HCI-level framing (the btfmslim channel header
 *     around the HCI packet) is therefore still missing, which is why an HCI
 *     exchange on this path ends at the transport stage and never at
 *     hci-ready. The boundary is asserted by tests/wcn3990_slim_smoke.c. */
#ifndef VQEAF_WCN3990_SLIM_H
#define VQEAF_WCN3990_SLIM_H

#include <stddef.h>
#include <stdint.h>

#include "slim_qcom_ngd.h"
#include "wcn3990_bt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VQEAF_BT_SLIM_TX_RETRIES 8u
#define VQEAF_BT_SLIM_RX_RETRIES 2u
#define VQEAF_BT_SLIM_TX_TIMEOUT_MS 100u

typedef struct {
    VqeafSlimNgd *ngd;
    uint8_t peer_ea[VQEAF_BT_SLIM_EA_LEN];
    uint8_t ifd_ea[VQEAF_BT_SLIM_EA_LEN];
    uint8_t attached;
    uint8_t peer_set;              /* the addresses below are recorded */
    /* Optional: block until the controller signals progress (TX_MSG_SENT or a
     * received message). NULL means "poll once and give up". */
    int  (*wait_ready)(void *ctx, uint32_t timeout_ms);
    void *wait_ctx;
    /* Optional: program the peer elemental address / enumerate the
     * btfmslim slave. The client contract has no enumeration verb yet, so a
     * provider that can do it installs this hook; without it the bridge only
     * records the addresses. */
    int  (*attach)(void *ctx, const uint8_t peer[VQEAF_BT_SLIM_EA_LEN],
                   const uint8_t ifd[VQEAF_BT_SLIM_EA_LEN]);
    void *attach_ctx;
    uint32_t tx_retries, rx_retries, tx_timeout_ms;
    uint32_t tx_sent, tx_queued, tx_failures, rx_timeouts;
    uint32_t msgs_out, msgs_in;
    int last_error;
} VqeafBtSlimLink;

/* A composed vqeaf_bt_ops_t: rails/firmware/log come from the platform, the
 * four SLIMbus callbacks from the bridge. ops.ctx points at the bridge, so the
 * driver's ops->ctx and the inner platform context are never confused. */
typedef struct {
    VqeafBtSlimLink link;
    const vqeaf_bt_ops_t *inner;
    vqeaf_bt_ops_t ops;
} VqeafBtSlimBridge;

void vqeaf_bt_slim_init(VqeafBtSlimLink *link, VqeafSlimNgd *ngd);

int  vqeaf_bt_slim_open(VqeafBtSlimLink *link,
                        const uint8_t peer[VQEAF_BT_SLIM_EA_LEN],
                        const uint8_t ifd[VQEAF_BT_SLIM_EA_LEN]);
void vqeaf_bt_slim_close(VqeafBtSlimLink *link);

/* One SLIMbus message (1..40 bytes). 0 only when the client accepted it and,
 * with a wait hook, TX_MSG_SENT cleared the TX slot. */
int vqeaf_bt_slim_write(VqeafBtSlimLink *link, const uint8_t *buf, size_t len);

/* Returns one received message. -ETIMEDOUT when nothing arrives. */
int vqeaf_bt_slim_read(VqeafBtSlimLink *link, uint8_t *buf, size_t cap,
                       size_t *got, uint32_t timeout_ms);

/* Build bridge->ops from the platform's ops and a started client.
 * 0 only when inner supplies power_on, power_off and firmware_read. */
int vqeaf_bt_slim_compose(VqeafBtSlimBridge *bridge,
                          const vqeaf_bt_ops_t *inner, VqeafSlimNgd *ngd);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_WCN3990_SLIM_H */
