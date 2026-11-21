/* QEMU VirtIO 0.9 (legacy PCI) block driver.
 * PCI 1AF4:1001, I/O BAR0, one INTx line, one virtqueue with up to four
 * in-flight requests. Submissions are asynchronous: submit() returns
 * immediately and the completion callback runs from the IRQ handler after the
 * device posts a used-ring element. No polling loop pretends to be async.
 * The block-layer registration is deliberately read-only in this phase; the
 * driver's write path exists and is exercised by a scratch-sector self test. */
#ifndef VQEAF_VIRTIO_BLK_H
#define VQEAF_VIRTIO_BLK_H
#include <stdint.h>
#define VQEAF_VBLK_MAX_SECTORS 8u
#define VQEAF_VBLK_SECTOR_SIZE 512u
typedef void (*vqeaf_virtio_done_fn)(int status,void *ctx);
int vqeaf_virtio_blk_init(void);
int vqeaf_virtio_blk_ready(void);
uint64_t vqeaf_virtio_blk_capacity(void);
uint32_t vqeaf_virtio_blk_dev_id(void);
uint32_t vqeaf_virtio_blk_irq_count(void);
uint32_t vqeaf_virtio_blk_completion_count(void);
int vqeaf_virtio_blk_submit(uint64_t lba,uint32_t sectors,void *buffer,int write,
                            vqeaf_virtio_done_fn done,void *ctx);
int vqeaf_virtio_blk_read_sync(uint64_t lba,uint32_t sectors,void *buffer);
int vqeaf_virtio_blk_write_sync(uint64_t lba,uint32_t sectors,const void *buffer);
int vqeaf_virtio_blk_concurrent_self_test(void);
int vqeaf_virtio_blk_error_self_test(void);
uint64_t vqeaf_virtio_blk_isr(uint64_t frame_base); /* switch.S entry point */
#endif
