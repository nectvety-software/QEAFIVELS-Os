/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_WCN3990_CE_H
#define VQEAF_WCN3990_CE_H
#include <stdint.h>
#include "wcn3990_phase2.h"
#ifdef __cplusplus
extern "C" {
#endif

#define VQEAF_WCN3990_CE_ABI 3u
#define VQEAF_WCN3990_CE0_BASE 0x00240000u
#define VQEAF_WCN3990_CE_STRIDE 0x1000u
#define VQEAF_WCN3990_CE_WRAPPER_BASE 0x0024c000u
#define VQEAF_WCN3990_CE_REGISTER_END \
    (VQEAF_WCN3990_CE0_BASE + (VQEAF_WCN3990_CE_MAX - 1u) * \
     VQEAF_WCN3990_CE_STRIDE + 0x54u)

/* Both CE ring base registers are 32-bit; CTRL1 swap selectors use bits 17/18. */
#define VQEAF_WCN3990_CE_SRC_BASE 0x00u
#define VQEAF_WCN3990_CE_SRC_SIZE 0x08u
#define VQEAF_WCN3990_CE_DST_BASE 0x0cu
#define VQEAF_WCN3990_CE_DST_SIZE 0x14u
#define VQEAF_WCN3990_CE_CTRL1 0x18u
#define VQEAF_WCN3990_CE_HOST_INTR_ENABLE 0x2cu
#define VQEAF_WCN3990_CE_MISC_INTR_ENABLE 0x34u
#define VQEAF_WCN3990_CE_MISC_STATUS 0x38u
#define VQEAF_WCN3990_CE_STATUS_ACK 0x30u
#define VQEAF_WCN3990_CE_SRC_WRITE_INDEX 0x3cu
#define VQEAF_WCN3990_CE_DST_WRITE_INDEX 0x40u
#define VQEAF_WCN3990_CE_SRC_READ_INDEX 0x44u
#define VQEAF_WCN3990_CE_DST_READ_INDEX 0x48u
#define VQEAF_WCN3990_CE_SRC_WATERMARK 0x4cu
#define VQEAF_WCN3990_CE_DST_WATERMARK 0x50u

#define VQEAF_WCN3990_CE_CTRL1_MAX_TRANSFER 0x0000ffffu
/* WCN3990 keeps the read indices of CE_ATTR_DIS_INTR (poll) pipes in a DDR
 * array the target updates; CE0..11 each get one 32-bit word. Bits 15:0 hold
 * the source read index and bits 31:16 the destination read index. The array
 * address is programmed through the CE wrapper (base 0x0024c000) and CTRL1
 * bit 19 (UPD) tells each pipe to publish its indices there. */
#define VQEAF_WCN3990_CE_RRI_LOW 0x0024c004u
#define VQEAF_WCN3990_CE_RRI_HIGH 0x0024c008u
#define VQEAF_WCN3990_CE_RRI_ALIGN 4u
#define VQEAF_WCN3990_CE_RRI_BYTES (VQEAF_WCN3990_CE_MAX * 4u)
#define VQEAF_WCN3990_CE_RRI_SRC_MASK 0x0000ffffu
#define VQEAF_WCN3990_CE_RRI_DST_SHIFT 16u
#define VQEAF_WCN3990_CE_CTRL1_UPD 0x00080000u
#define VQEAF_WCN3990_CE_CTRL1_SRC_SWAP 0x00020000u
#define VQEAF_WCN3990_CE_CTRL1_DST_SWAP 0x00040000u
#define VQEAF_WCN3990_CE_INTR_COPY_COMPLETE 0x01u
#define VQEAF_WCN3990_CE_INTR_WATERMARK 0x1eu
#define VQEAF_WCN3990_CE_INTR_HOST_MASK 0x1fu
#define VQEAF_WCN3990_CE_INTR_ERROR_MASK 0x3e0u
#define VQEAF_WCN3990_CE_ATTR_DIS_INTR 8u

typedef struct {
    void *ctx;
    int (*read32)(void *, uint32_t, uint32_t *);
    int (*write32)(void *, uint32_t, uint32_t);
    void (*barrier)(void *);
} VqeafWcn3990CeMmioOps;

typedef struct {
    uint64_t window_base, window_size;
    uint8_t dma_bits;
    uint32_t address_high_mask;
    VqeafWcn3990CeMmioOps mmio;
} VqeafWcn3990CeMmio;

typedef struct {
    uint32_t abi, size;
    uint64_t window_base, window_size;
    uint8_t dma_bits;
    uint32_t address_high_mask;
    VqeafWcn3990CeMmioOps mmio;
    uint16_t active_mask;
    uint16_t _poll_mask;
    uint16_t _quiesce_mask;
    uint16_t _src_entries[VQEAF_WCN3990_CE_MAX];
    uint16_t _dst_entries[VQEAF_WCN3990_CE_MAX];
    uint8_t _touched[VQEAF_WCN3990_CE_MAX];
    /* Host view of the 12-word DDR RRI array owned by the DMA provider. */
    volatile uint32_t *rri_cpu;
    uint64_t rri_dma;
    uint8_t _rri_ready;
} VqeafWcn3990Ce;

/* Zero-initialize first; dma_bits/address_high_mask describe descriptor DMA, not CE ring bases. */
int vqeaf_wcn3990_ce_init(VqeafWcn3990Ce *, const VqeafWcn3990CeMmio *);
int vqeaf_wcn3990_ce_configure(void *, const VqeafWcn3990CeConfig *,
                               uint64_t, uint64_t);
/* Returns 0 only when the CE no longer references its DMA rings. On failure
 * the quiesce state is retained and the caller must not free or reuse them. */
int vqeaf_wcn3990_ce_disable(void *, uint8_t);
/* Program the DDR RRI array for poll-mode pipes. The caller keeps ownership of
 * rri_cpu/rri_dma and frees them only after rri_disable returns 0. */
int vqeaf_wcn3990_ce_rri_enable(VqeafWcn3990Ce *, void *, uint64_t);
int vqeaf_wcn3990_ce_rri_disable(VqeafWcn3990Ce *);
int vqeaf_wcn3990_ce_indices(void *, uint8_t, uint32_t *, uint32_t *,
                            uint32_t *, uint32_t *);
int vqeaf_wcn3990_ce_interrupt_disable(void *, uint8_t);
int vqeaf_wcn3990_ce_interrupt_enable(void *, uint8_t, uint32_t, uint32_t);
int vqeaf_wcn3990_ce_acknowledge(void *, uint8_t, uint32_t);
#ifdef __cplusplus
}
#endif
#endif
