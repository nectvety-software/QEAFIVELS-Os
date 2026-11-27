/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_WCN3990_PHASE2_H
#define VQEAF_WCN3990_PHASE2_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define VQEAF_WCN3990_P2_ABI 4u
#define VQEAF_WCN3990_CE_MAX 12u
#define VQEAF_WCN3990_P2_DEFAULT_TIMEOUT_MS 1000u
#define VQEAF_WCN3990_P2_MAX_STEP_ATTEMPTS 1024u
#define VQEAF_WCN3990_DMA_BITS 37u
#define VQEAF_WCN3990_CE_RING_ALIGN 8u

typedef enum {
 VQEAF_WCN_P2_OFF=0, VQEAF_WCN_P2_MAPPED, VQEAF_WCN_P2_REGULATORS,
 VQEAF_WCN_P2_CLOCKS, VQEAF_WCN_P2_ICNSS, VQEAF_WCN_P2_MSA,
 VQEAF_WCN_P2_IRQS, VQEAF_WCN_P2_CE, VQEAF_WCN_P2_FAILED,
 VQEAF_WCN_P2_REMOVED
} VqeafWcn3990P2State;

/* WCN3990 CE descriptor format: 64-bit address, 16-bit byte count,
 * 16-bit flags, 32-bit metadata. DMA addresses are restricted to 37 bits. */
typedef struct __attribute__((packed, aligned(8))) {
 uint64_t address;
 uint16_t nbytes;
 uint16_t flags;
 uint32_t metadata;
} VqeafWcn3990CeDesc;

typedef struct {
 uint8_t id;
 uint16_t src_entries, dst_entries;
 uint16_t max_transfer;
 uint32_t flags;
} VqeafWcn3990CeConfig;

typedef struct {
 uint64_t snoc_base, snoc_size;
 uint64_t msa_base, msa_size;
 uint32_t ce_irq[VQEAF_WCN3990_CE_MAX];
 uint32_t ce_irq_count;
} VqeafWcn3990P2Resources;

typedef struct {
 void *ctx;
 int (*map_snoc)(void *,uint64_t,uint64_t);
 void (*unmap_snoc)(void *);
 int (*regulators_enable)(void *);
 void (*regulators_disable)(void *);
 int (*clocks_enable)(void *);
 void (*clocks_disable)(void *);
 int (*icnss_power_on)(void *);
 void (*icnss_power_off)(void *);
 int (*msa_prepare)(void *,uint64_t,uint64_t);
 void (*msa_release)(void *);
 int (*request_irq)(void *,uint32_t,uint8_t);
 void (*free_irq)(void *,uint32_t,uint8_t);
 int (*dma_alloc)(void *,uint64_t,uint64_t,void **,uint64_t *);
 void (*dma_free)(void *,uint64_t,void *,uint64_t);
 int (*ce_configure)(void *,const VqeafWcn3990CeConfig *,
                     uint64_t,uint64_t);
 /* ABI v3: return 0 only when the CE no longer references its DMA rings.
  * A failure keeps quiesce pending and forbids freeing or reusing them. */
 int (*ce_disable)(void *,uint8_t);
 /* ABI v4, optional: program the DDR RRI array poll-mode CEs read indices
  * from. Without it phase2 skips RRI and poll CE index reads stay
  * -EOPNOTSUPP. rri_disable is required whenever ce_rri is supplied and must
  * return 0 only when no CE still publishes indices into the array; on
  * failure phase2 must keep the DMA block allocated. */
 int (*ce_rri)(void *,void *,uint64_t);
 int (*ce_rri_disable)(void *);
 /* Optional timeout support introduced in ABI v2 and retained in v3:
  * a step returning -EAGAIN is retried until step_timeout_ms elapses, the
  * clock stops advancing, or the attempt cap is reached, then fails with
  * -ETIMEDOUT. Without both clock and delay, -EAGAIN is propagated. */
 uint64_t (*now_ms)(void *);
 void (*delay_ms)(void *,uint32_t);
} VqeafWcn3990P2Ops;

typedef struct {
 uint32_t abi,size;
 VqeafWcn3990P2Resources resources;
 VqeafWcn3990P2Ops ops;
 uint32_t step_timeout_ms;   /* 0 = VQEAF_WCN3990_P2_DEFAULT_TIMEOUT_MS */
} VqeafWcn3990P2Device;

int vqeaf_wcn3990_phase2_register(void);
int vqeaf_wcn3990_phase2_bind(VqeafWcn3990P2Device *,uint32_t *);
int vqeaf_wcn3990_phase2_state(void);
int vqeaf_wcn3990_phase2_error(void);
const VqeafWcn3990CeConfig *vqeaf_wcn3990_ce_config(uint32_t *);
#ifdef __cplusplus
}
#endif
#endif
