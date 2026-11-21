/* QEMU virt AArch64 VirtIO-MMIO block driver (modern v2 + legacy v1).
 * See kernel/drivers/virtio_mmio_blk.c. Transports live at 0x0a000000 + n*0x200
 * on the virt machine; DeviceID 2 is block. I/O is synchronous (poll the used
 * ring with a bounded timeout) -- no polling loop pretends to be async. READY
 * is published only after a real probe (DRIVER_OK + capacity) and a scratch
 * sector write/read self-test verifies the data path. */
#ifndef VQEAF_VIRTIO_MMIO_BLK_H
#define VQEAF_VIRTIO_MMIO_BLK_H
#include <stdint.h>
#define VQEAF_VMMIO_MAX_SECTORS 8u
#define VQEAF_VMMIO_SECTOR_SIZE 512u
int vqeaf_virtio_mmio_blk_init(void);
int vqeaf_virtio_mmio_blk_ready(void);
uint64_t vqeaf_virtio_mmio_blk_capacity(void);
uint32_t vqeaf_virtio_mmio_blk_dev_id(void);
uint32_t vqeaf_virtio_mmio_blk_instance(void);
uint64_t vqeaf_virtio_mmio_blk_base(void);
uint32_t vqeaf_virtio_mmio_blk_version(void);
int vqeaf_virtio_mmio_blk_read_sync(uint64_t lba, uint32_t sectors, void *buffer);
int vqeaf_virtio_mmio_blk_write_sync(uint64_t lba, uint32_t sectors, const void *buffer);
int vqeaf_virtio_mmio_blk_self_test(void);
#endif