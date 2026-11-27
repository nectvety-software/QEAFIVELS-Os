/* SPDX-License-Identifier: MIT */
/* kernel/include/vqeaf/slim_qcom_ngd.h - Qualcomm NGD SLIMbus controller client.
 *
 * Port of the transport core of the upstream driver
 * drivers/slimbus/qcom-ngd-ctrl.c (register block, message-queue bring-up,
 * interrupt acknowledge, TX/RX descriptor rings) into the QEAFIVELS driver
 * model. No Linux code is copied: the register accesses, BAM pipes, IRQ and
 * DMA all arrive through injected operations, exactly like the WCN3990 Copy
 * Engine contract, so the state machine runs unchanged in host tests.
 *
 * Hardware facts come from violet's board source
 * platform/violet/device-tree/sources/guaiyihu-sm6150/arch/arm64/boot/dts/qcom/sm6150.dtsi:
 *   slim_qca: slim@62e40000 {
 *     compatible = "qcom,slim-ngd";
 *     reg = <0x62e40000 0x2c000>,      // slimbus_physical
 *           <0x62e04000 0x20000>;      // slimbus_bam_physical
 *     interrupts = <0 291 0>, <0 292 0>; // slimbus_irq, slimbus_bam_irq
 *     status = "ok"; qcom,iommu-s1-bypass;
 *     wcn3990 { compatible = "qcom,btfmslim_slave"; ... }
 *   };
 *
 * The NGD register block sits at offset 0x1000 inside the controller window
 * (upstream ngd_v1_5_offset_info). Register layout, message limits and
 * interrupt bits match that upstream file; they are hardware facts, not
 * values measured on this handset. NOT verified on hardware: that the stock
 * kernel's view of this block can be taken over by another OS, and the BAM
 * pipe numbering.
 *
 * Fail-closed: every register value is read back before the controller is
 * reported up, and no message is ever reported as sent without the
 * TX_MSG_SENT interrupt. */
#ifndef VQEAF_SLIM_QCOM_NGD_H
#define VQEAF_SLIM_QCOM_NGD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VQEAF_SLIM_NGD_ABI 1u
#define VQEAF_SLIM_NGD_COMPATIBLE "qcom,slim-ngd"

/* violet slim_qca, verbatim from sm6150.dtsi. */
#define VQEAF_SLIM_NGD_CTRL_BASE 0x62e40000ull
#define VQEAF_SLIM_NGD_CTRL_SIZE 0x2c000u
#define VQEAF_SLIM_NGD_BAM_BASE  0x62e04000ull
#define VQEAF_SLIM_NGD_BAM_SIZE  0x20000u
#define VQEAF_SLIM_NGD_IRQ       291u
#define VQEAF_SLIM_NGD_BAM_IRQ   292u

/* NGD (non-ported generic device) register block. */
#define VQEAF_SLIM_NGD_REG_OFFSET 0x1000u
#define VQEAF_SLIM_NGD_CFG        0x00u
#define VQEAF_SLIM_NGD_STATUS     0x04u
#define VQEAF_SLIM_NGD_RX_MSGQ_CFG 0x08u
#define VQEAF_SLIM_NGD_INT_EN     0x10u
#define VQEAF_SLIM_NGD_INT_STAT   0x14u
#define VQEAF_SLIM_NGD_INT_CLR    0x18u
#define VQEAF_SLIM_NGD_REG_END    (VQEAF_SLIM_NGD_INT_CLR + 4u)

#define VQEAF_SLIM_NGD_CFG_ENABLE    0x00000001u
#define VQEAF_SLIM_NGD_CFG_RX_MSGQ_EN 0x00000002u
#define VQEAF_SLIM_NGD_CFG_TX_MSGQ_EN 0x00000004u
#define VQEAF_SLIM_NGD_LADDR         0x00000002u

#define VQEAF_SLIM_NGD_INT_RECFG_DONE  0x01000000u
#define VQEAF_SLIM_NGD_INT_TX_NACKED_2 0x02000000u
#define VQEAF_SLIM_NGD_INT_MSG_BUF_CONTE 0x04000000u
#define VQEAF_SLIM_NGD_INT_MSG_TX_INVAL 0x08000000u
#define VQEAF_SLIM_NGD_INT_IE_VE_CHG   0x10000000u
#define VQEAF_SLIM_NGD_INT_DEV_ERR     0x20000000u
#define VQEAF_SLIM_NGD_INT_RX_MSG_RCVD 0x40000000u
#define VQEAF_SLIM_NGD_INT_TX_MSG_SENT 0x80000000u

/* Upstream DEF_NGD_INT_MASK: the recovery/config-done and bus-change events
 * are polled differently, so they stay out of the enabled set. */
#define VQEAF_SLIM_NGD_INT_MASK \
    (VQEAF_SLIM_NGD_INT_TX_NACKED_2 | VQEAF_SLIM_NGD_INT_MSG_TX_INVAL | \
     VQEAF_SLIM_NGD_INT_DEV_ERR | VQEAF_SLIM_NGD_INT_RX_MSG_RCVD)
#define VQEAF_SLIM_NGD_INT_ERROR \
    (VQEAF_SLIM_NGD_INT_TX_NACKED_2 | VQEAF_SLIM_NGD_INT_MSG_BUF_CONTE | \
     VQEAF_SLIM_NGD_INT_MSG_TX_INVAL | VQEAF_SLIM_NGD_INT_DEV_ERR)

#define VQEAF_SLIM_NGD_RX_MSGQ_TIMEOUT 0x00010000u
/* Per spec, a received message is at most 40 bytes; the TX path refuses
 * anything longer (upstream SLIM_MSGQ_BUF_LEN). */
#define VQEAF_SLIM_NGD_MSG_MAX 40u
#define VQEAF_SLIM_NGD_DESC_NUM 32u
#define VQEAF_SLIM_NGD_RING_BYTES \
    (VQEAF_SLIM_NGD_DESC_NUM * VQEAF_SLIM_NGD_MSG_MAX)

typedef enum {
    VQEAF_SLIM_DIR_RX = 0,
    VQEAF_SLIM_DIR_TX = 1
} vqeaf_slim_dir_t;

typedef struct {
    void *ctx;
    /* Offsets are relative to the controller window base. */
    int  (*read32)(void *ctx, uint32_t offset, uint32_t *value);
    int  (*write32)(void *ctx, uint32_t offset, uint32_t value);
    void (*barrier)(void *ctx);
} VqeafSlimNgdMmioOps;

/* One BAM pipe per direction. The platform owns the BAM instance; the driver
 * never claims a pipe number it cannot see in the device tree. */
typedef struct {
    void *ctx;
    int  (*pipe_init)(void *ctx, vqeaf_slim_dir_t dir);
    int  (*queue)(void *ctx, vqeaf_slim_dir_t dir, uint64_t dma, uint32_t len);
    int  (*issue)(void *ctx, vqeaf_slim_dir_t dir);
    int  (*terminate)(void *ctx, vqeaf_slim_dir_t dir);
} VqeafSlimNgdBamOps;

typedef struct {
    void *ctx;
    int  (*request)(void *ctx, uint32_t irq, void (*handler)(void *),
                    void *handler_ctx);
    int  (*enable)(void *ctx, uint32_t irq, int on);
    void (*free)(void *ctx, uint32_t irq);
} VqeafSlimNgdIrqOps;

typedef struct {
    void *ctx;
    int  (*alloc)(void *ctx, size_t bytes, size_t align, void **cpu,
                  uint64_t *dma);
    void (*release)(void *ctx, void *cpu, uint64_t dma, size_t bytes);
} VqeafSlimNgdDmaOps;

typedef struct {
    const char *compatible;
    uint64_t ctrl_base, ctrl_size;
    uint64_t bam_base, bam_size;
    uint32_t irq, bam_irq;
    uint32_t ngd_offset;          /* NGD block inside the controller window */
    VqeafSlimNgdMmioOps mmio;
    VqeafSlimNgdBamOps bam;
    VqeafSlimNgdIrqOps irq_ops;
    VqeafSlimNgdDmaOps dma;
} VqeafSlimNgdDesc;

typedef enum {
    VQEAF_SLIM_NGD_STOPPED = 0,
    VQEAF_SLIM_NGD_UP,
    VQEAF_SLIM_NGD_FAILED
} vqeaf_slim_ngd_state_t;

typedef struct {
    uint32_t abi, size;
    VqeafSlimNgdDesc desc;
    vqeaf_slim_ngd_state_t state;
    uint32_t version;            /* 16 MSBs of controller register 0 */
    uint32_t status;             /* last NGD_STATUS read */
    uint8_t *rx_cpu, *tx_cpu;
    uint64_t rx_dma, tx_dma;
    uint16_t rx_head;            /* next RX descriptor to consume */
    uint16_t tx_tail;            /* next TX descriptor to fill */
    uint8_t  rx_pending;         /* descriptors the controller reported full */
    uint8_t  tx_busy;            /* TX queued, TX_MSG_SENT not seen yet */
    uint8_t  started;
    uint8_t  irq_requested;
    uint8_t  bam_mask;           /* bit0 = RX pipe initialised, bit1 = TX pipe */
    uint8_t  rx_armed;           /* RX descriptors submitted */
    uint8_t  int_en_written;     /* INT_EN was programmed and must be masked */
    uint32_t irq_count, rx_count, tx_done_count, error_count;
    int last_error;
} VqeafSlimNgd;

/* Validate the descriptor and copy it. Does not touch the hardware. */
int vqeaf_slim_ngd_init(VqeafSlimNgd *ngd, const VqeafSlimNgdDesc *desc);

/* Full bring-up: DMA rings, BAM pipes, IRQ, then INT_EN/RX_MSGQ_CFG/CFG with a
 * read-back of each. On any failure everything already enabled is unwound and
 * the state stays FAILED with the first real errno. */
int vqeaf_slim_ngd_start(VqeafSlimNgd *ngd);

/* Masks interrupts, frees the IRQ, terminates both BAM pipes and releases the
 * rings. Idempotent; a failure to mask is reported through last_error. */
void vqeaf_slim_ngd_stop(VqeafSlimNgd *ngd);

/* Hands one SLIMbus message (1..40 bytes) to the TX BAM pipe. Non-blocking:
 * 0 means queued, not delivered. -EBUSY while the previous TX is unacknowledged. */
int vqeaf_slim_ngd_xfer(VqeafSlimNgd *ngd, const uint8_t *msg, uint32_t len);

/* Copies the next received message and re-arms its descriptor. -EAGAIN when
 * nothing is pending. A negative return after the copy means the message was
 * delivered to the caller but its descriptor could not be re-armed. */
int vqeaf_slim_ngd_rx_pop(VqeafSlimNgd *ngd, uint8_t *out, uint32_t cap,
                          uint32_t *len);

/* Interrupt entry point; the IRQ provider calls this with the stored context. */
void vqeaf_slim_ngd_irq(VqeafSlimNgd *ngd);

/* 0 only when the descriptor carries exactly the violet sm6150.dtsi numbers. */
int vqeaf_slim_ngd_check_violet_dt(const VqeafSlimNgdDesc *desc);

const char *vqeaf_slim_ngd_state_name(vqeaf_slim_ngd_state_t state);
const char *vqeaf_slim_ngd_error_name(int err);

#ifdef __cplusplus
}
#endif
#endif /* VQEAF_SLIM_QCOM_NGD_H */
