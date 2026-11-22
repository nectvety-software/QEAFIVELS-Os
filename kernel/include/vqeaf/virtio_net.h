/* SPDX-License-Identifier: MIT */
/* QEAF native VirtIO 0.9 (legacy PCI) network driver.
 * M1 only: PCI discovery, capability read and device config dump.
 * No virtqueue, no IRQ, no packet TX/RX yet. */
#ifndef VQEAF_VIRTIO_NET_H
#define VQEAF_VIRTIO_NET_H
#include <stdint.h>

#define VQEAF_VNET_MAC_LEN 6u

int  vqeaf_virtio_net_init(void);   /* 0 on a real device, negative otherwise */
int  vqeaf_virtio_net_ready(void);
void vqeaf_virtio_net_mac(uint8_t out[VQEAF_VNET_MAC_LEN]);
uint32_t vqeaf_virtio_net_irq_count(void);
uint32_t vqeaf_virtio_net_dev_id(void);

#endif
