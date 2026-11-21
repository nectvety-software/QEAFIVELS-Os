/* QEMU ICH9 AHCI (8086:2922) SATA driver.
 * Legacy INTx is not wired for this controller in QEMU (IRQ line 255, MSI
 * based), so commands are completed by polling PxCI/PxIS with bounded
 * timeouts. VirtIO remains the interrupt-driven reference path. Each attached
 * SATA disk gets its own block device id; reads/writes are exposed by disk
 * index because the block-layer registration stays read-only. */
#ifndef VQEAF_AHCI_H
#define VQEAF_AHCI_H
#include <stdint.h>
#define VQEAF_AHCI_MAX_PORTS 4u
int vqeaf_ahci_init(void);
int vqeaf_ahci_ready(void);
uint32_t vqeaf_ahci_disk_count(void);
uint32_t vqeaf_ahci_dev_id(uint32_t index);   /* 0xFFFFFFFF when absent */
uint64_t vqeaf_ahci_capacity(uint32_t index);
int vqeaf_ahci_read_sync(uint32_t index,uint64_t lba,uint32_t sectors,void *buffer);
int vqeaf_ahci_write_sync(uint32_t index,uint64_t lba,uint32_t sectors,const void *buffer);
#endif
