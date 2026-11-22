/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_QRTR_TRANSPORT_H
#define VQEAF_QRTR_TRANSPORT_H
#include <stdint.h>
#include <stddef.h>

/* Portable QRTR client transport. The freestanding kernel has no AF_QIPCRTR
 * socket, so the actual socket I/O is supplied by callbacks; the wire format,
 * endpoint addressing and service table are implemented here, once. */

#define VQEAF_QRTR_NODE_BCAST   0xffffffffu
#define VQEAF_QRTR_PORT_CTRL    0xfffffffeu

enum {
    VQEAF_QRTR_DATA       = 1,
    VQEAF_QRTR_HELLO      = 2,
    VQEAF_QRTR_BYE        = 3,
    VQEAF_QRTR_NEW_SERVER = 4,
    VQEAF_QRTR_DEL_SERVER = 5,
    VQEAF_QRTR_DEL_CLIENT = 6,
    VQEAF_QRTR_RESUME_TX  = 7,
    VQEAF_QRTR_EXIT       = 8,
    VQEAF_QRTR_PING       = 9,
    VQEAF_QRTR_NEW_LOOKUP = 10,
    VQEAF_QRTR_DEL_LOOKUP = 11
};

#define VQEAF_QRTR_CTRL_SIZE 20u  /* cmd + server{service,instance,node,port} */

typedef struct {
    uint32_t node, port;
} vqeaf_qrtr_addr_t;

typedef struct {
    uint32_t service;
    uint32_t instance;      /* upper byte of the verinst word */
    uint32_t version;
    uint32_t node, port;
} vqeaf_qrtr_server_t;

/* Decoded QRTR control packet (all fields come from the wire, none invented). */
typedef struct {
    uint32_t cmd;
    uint32_t service, verinst;
    uint32_t node, port;
} vqeaf_qrtr_ctrl_t;

typedef struct {
    void *ctx;
    uint32_t local_node;     /* node id from getsockname, never guessed */
    int (*send)(void *ctx, const vqeaf_qrtr_addr_t *dst,
                const uint8_t *buf, size_t len);
    int (*recv)(void *ctx, uint8_t *buf, size_t cap, size_t *out_len,
                vqeaf_qrtr_addr_t *src, uint32_t timeout_ms);
    uint64_t (*now_ms)(void *ctx);
} vqeaf_qrtr_io_t;

/* Explicit little-endian control packet codec. Returns 0 or -EINVAL. */
int vqeaf_qrtr_ctrl_encode(uint8_t *buf, size_t cap,
                           const vqeaf_qrtr_ctrl_t *in, size_t *out_len);
int vqeaf_qrtr_ctrl_decode(const uint8_t *buf, size_t len,
                           vqeaf_qrtr_ctrl_t *out);

/* Server table: only packets actually received can add/remove entries. */
void vqeaf_qrtr_table_reset(void);
int  vqeaf_qrtr_table_add(const vqeaf_qrtr_ctrl_t *pkt);
int  vqeaf_qrtr_table_drop(const vqeaf_qrtr_ctrl_t *pkt);
int  vqeaf_qrtr_table_find(uint32_t service, uint32_t instance,
                           vqeaf_qrtr_server_t *out);

/* Real service discovery: sends NEW_LOOKUP, ingests received control packets
 * until a matching NEW_SERVER appears or the deadline expires.
 * Returns 0 (found), -ENOENT (not published within timeout), -EINVAL or a
 * transport error. Never fabricates a server. */
int vqeaf_qrtr_discover(const vqeaf_qrtr_io_t *io, uint32_t service,
                        uint32_t instance, uint32_t version,
                        uint32_t timeout_ms, vqeaf_qrtr_server_t *out);

#endif
