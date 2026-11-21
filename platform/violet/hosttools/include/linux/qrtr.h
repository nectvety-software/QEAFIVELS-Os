/* linux/qrtr.h for the host tools: the kernel UAPI copy kept in temp/.
 * Upstream libqrtr includes <linux/qrtr.h>; musl does not ship kernel UAPI
 * headers, so the project supplies this one. */
#ifndef _LINUX_QRTR_H
#define _LINUX_QRTR_H

#include <linux/socket.h>
#include <linux/types.h>

/* Zig's musl headers do not define the Linux UAPI spelling. Without this,
 * `} __packed;` in struct qrtr_ctrl_pkt declares a global named __packed. */
#ifndef __packed
#define __packed __attribute__((packed))
#endif

#define QRTR_NODE_BCAST 0xffffffffu
#define QRTR_PORT_CTRL 0xfffffffeu

struct sockaddr_qrtr {
	__kernel_sa_family_t sq_family;
	__u32 sq_node;
	__u32 sq_port;
};

enum qrtr_pkt_type {
	QRTR_TYPE_DATA = 1,
	QRTR_TYPE_HELLO = 2,
	QRTR_TYPE_BYE = 3,
	QRTR_TYPE_NEW_SERVER = 4,
	QRTR_TYPE_DEL_SERVER = 5,
	QRTR_TYPE_DEL_CLIENT = 6,
	QRTR_TYPE_RESUME_TX = 7,
	QRTR_TYPE_EXIT = 8,
	QRTR_TYPE_PING = 9,
	QRTR_TYPE_NEW_LOOKUP = 10,
	QRTR_TYPE_DEL_LOOKUP = 11,
};
#define QRTR_TYPE_DEL_PROC 13

struct qrtr_ctrl_pkt {
	__le32 cmd;
	union {
		struct {
			__le32 service;
			__le32 instance;
			__le32 node;
			__le32 port;
		} server;
		struct {
			__le32 node;
			__le32 port;
		} client;
		struct {
			__le32 rsvd;
			__le32 node;
		} proc;
	};
} __packed;

#endif
