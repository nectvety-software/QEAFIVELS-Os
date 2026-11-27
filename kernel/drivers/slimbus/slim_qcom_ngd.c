/* SPDX-License-Identifier: MIT */
/* kernel/drivers/slimbus/slim_qcom_ngd.c - Qualcomm NGD SLIMbus controller.
 *
 * Port of the upstream transport core (drivers/slimbus/qcom-ngd-ctrl.c):
 * the NGD register block, the message-queue enable sequence, the interrupt
 * status/acknowledge pair, and the 32 x 40-byte TX/RX descriptor rings. What
 * is ported is the sequence and its checks; MMIO, BAM pipes, IRQ and DMA are
 * injected through VqeafSlimNgdDesc, so this file contains no platform code.
 *
 * Every register write that brings the controller up is read back, and no TX
 * is reported as delivered without TX_MSG_SENT. Any failure unwinds exactly
 * the resources already acquired, in reverse order. */
#include "vqeaf/slim_qcom_ngd.h"

#include <errno.h>
#include <string.h>

#define BAM_RX 0x1u
#define BAM_TX 0x2u

static int attached(const VqeafSlimNgd *ngd)
{
    return ngd != NULL && ngd->abi == VQEAF_SLIM_NGD_ABI &&
           ngd->size == sizeof(*ngd);
}

static int desc_valid(const VqeafSlimNgdDesc *d)
{
    if(d == NULL || d->compatible == NULL ||
       strcmp(d->compatible, VQEAF_SLIM_NGD_COMPATIBLE) != 0)
        return 0;
    if(d->ctrl_base == 0 || d->ctrl_size < d->ngd_offset + VQEAF_SLIM_NGD_REG_END)
        return 0;
    if(d->ctrl_base > UINT64_MAX - d->ctrl_size)
        return 0;
    if(d->bam_base == 0 || d->bam_size == 0 || d->bam_base > UINT64_MAX - d->bam_size)
        return 0;
    if(d->irq == 0 || d->bam_irq == 0 || d->irq == d->bam_irq)
        return 0;
    if(!d->mmio.read32 || !d->mmio.write32 || !d->mmio.barrier)
        return 0;
    if(!d->bam.pipe_init || !d->bam.queue || !d->bam.issue || !d->bam.terminate)
        return 0;
    if(!d->irq_ops.request || !d->irq_ops.enable || !d->irq_ops.free)
        return 0;
    if(!d->dma.alloc || !d->dma.release)
        return 0;
    return 1;
}

static void fence(VqeafSlimNgd *ngd)
{
    ngd->desc.mmio.barrier(ngd->desc.mmio.ctx);
}

static int reg_read(VqeafSlimNgd *ngd, uint32_t offset, uint32_t *value)
{
    int rc;
    if(offset > ngd->desc.ctrl_size || ngd->desc.ctrl_size - offset < 4u)
        return -ERANGE;
    fence(ngd);
    rc = ngd->desc.mmio.read32(ngd->desc.mmio.ctx, offset, value);
    fence(ngd);
    return rc;
}

static int reg_write(VqeafSlimNgd *ngd, uint32_t offset, uint32_t value)
{
    int rc;
    if(offset > ngd->desc.ctrl_size || ngd->desc.ctrl_size - offset < 4u)
        return -ERANGE;
    fence(ngd);
    rc = ngd->desc.mmio.write32(ngd->desc.mmio.ctx, offset, value);
    fence(ngd);
    return rc;
}

static int ngd_read(VqeafSlimNgd *ngd, uint32_t reg, uint32_t *value)
{
    return reg_read(ngd, ngd->desc.ngd_offset + reg, value);
}

static int ngd_write(VqeafSlimNgd *ngd, uint32_t reg, uint32_t value)
{
    return reg_write(ngd, ngd->desc.ngd_offset + reg, value);
}

static void release_dma(VqeafSlimNgd *ngd, uint8_t *cpu, uint64_t dma)
{
    if(cpu != NULL)
        ngd->desc.dma.release(ngd->desc.dma.ctx, cpu, dma,
                              VQEAF_SLIM_NGD_RING_BYTES);
}

/* Reverse-order unwinding, reporting the first cleanup failure to the caller
 * so a half-released controller is never reported as merely "failed". */
static int unwound(VqeafSlimNgd *ngd, int rc)
{
    int cleanup = 0;

    /* Mask whenever the enable register was actually written, even when the
     * bring-up failed before the controller reached UP: leaving the mask on
     * while the IRQ is released would be a level-interrupt storm. */
    if(ngd->started || ngd->int_en_written) {
        int mask = ngd_write(ngd, VQEAF_SLIM_NGD_INT_EN, 0);
        if(mask && !cleanup) cleanup = mask;
        ngd->int_en_written = 0;
        ngd->started = 0;
    }
    if(ngd->irq_requested) {
        int off = ngd->desc.irq_ops.enable(ngd->desc.irq_ops.ctx, ngd->desc.irq, 0);
        if(off && !cleanup) cleanup = off;
        ngd->desc.irq_ops.free(ngd->desc.irq_ops.ctx, ngd->desc.irq);
        ngd->irq_requested = 0;
    }
    if(ngd->bam_mask & BAM_TX)
        if(ngd->desc.bam.terminate(ngd->desc.bam.ctx, VQEAF_SLIM_DIR_TX) && !cleanup)
            cleanup = -EIO;
    if(ngd->bam_mask & BAM_RX)
        if(ngd->desc.bam.terminate(ngd->desc.bam.ctx, VQEAF_SLIM_DIR_RX) && !cleanup)
            cleanup = -EIO;
    ngd->bam_mask = 0;
    ngd->rx_armed = 0;
    if(ngd->tx_cpu != NULL) {
        release_dma(ngd, ngd->tx_cpu, ngd->tx_dma);
        ngd->tx_cpu = NULL;
        ngd->tx_dma = 0;
    }
    if(ngd->rx_cpu != NULL) {
        release_dma(ngd, ngd->rx_cpu, ngd->rx_dma);
        ngd->rx_cpu = NULL;
        ngd->rx_dma = 0;
    }
    ngd->rx_pending = 0;
    ngd->tx_busy = 0;
    ngd->rx_head = 0;
    ngd->tx_tail = 0;
    ngd->state = VQEAF_SLIM_NGD_FAILED;
    ngd->last_error = cleanup ? cleanup : rc;
    return ngd->last_error;
}

int vqeaf_slim_ngd_init(VqeafSlimNgd *ngd, const VqeafSlimNgdDesc *desc)
{
    if(ngd == NULL) return -EINVAL;
    if(!desc_valid(desc)) return -EINVAL;
    if(ngd->abi == VQEAF_SLIM_NGD_ABI && ngd->size == sizeof(*ngd) &&
       (ngd->started || ngd->irq_requested || ngd->bam_mask ||
        ngd->int_en_written))
        return -EBUSY;
    memset(ngd, 0, sizeof(*ngd));
    ngd->abi = VQEAF_SLIM_NGD_ABI;
    ngd->size = sizeof(*ngd);
    ngd->desc = *desc;
    ngd->state = VQEAF_SLIM_NGD_STOPPED;
    return 0;
}

/* The IRQ provider takes an untyped context, so the public entry point is
 * reached through this thunk instead of casting the function pointer. */
static void irq_thunk(void *ctx)
{
    vqeaf_slim_ngd_irq((VqeafSlimNgd *)ctx);
}

static int arm_rx_pipe(VqeafSlimNgd *ngd)
{
    uint32_t i;
    int rc;

    for(i = 0; i < VQEAF_SLIM_NGD_DESC_NUM; i++) {
        rc = ngd->desc.bam.queue(ngd->desc.bam.ctx, VQEAF_SLIM_DIR_RX,
                                 ngd->rx_dma +
                                     (uint64_t)i * VQEAF_SLIM_NGD_MSG_MAX,
                                 VQEAF_SLIM_NGD_MSG_MAX);
        if(rc) return rc;
    }
    rc = ngd->desc.bam.issue(ngd->desc.bam.ctx, VQEAF_SLIM_DIR_RX);
    if(rc) return rc;
    ngd->rx_armed = 1;
    return 0;
}

static int rings_alloc(VqeafSlimNgd *ngd)
{
    int rc;
    void *cpu = NULL;
    uint64_t dma = 0;

    rc = ngd->desc.dma.alloc(ngd->desc.dma.ctx, VQEAF_SLIM_NGD_RING_BYTES, 64u,
                             &cpu, &dma);
    if(rc < 0) return rc;
    if(cpu == NULL || dma == 0 || (dma & 3u) != 0) {
        if(cpu != NULL)
            ngd->desc.dma.release(ngd->desc.dma.ctx, cpu, dma,
                                  VQEAF_SLIM_NGD_RING_BYTES);
        return -EINVAL;
    }
    ngd->rx_cpu = (uint8_t *)cpu;
    ngd->rx_dma = dma;

    cpu = NULL;
    dma = 0;
    rc = ngd->desc.dma.alloc(ngd->desc.dma.ctx, VQEAF_SLIM_NGD_RING_BYTES, 64u,
                             &cpu, &dma);
    if(rc < 0) return rc;
    if(cpu == NULL || dma == 0 || (dma & 3u) != 0) {
        if(cpu != NULL)
            ngd->desc.dma.release(ngd->desc.dma.ctx, cpu, dma,
                                  VQEAF_SLIM_NGD_RING_BYTES);
        return -EINVAL;
    }
    ngd->tx_cpu = (uint8_t *)cpu;
    ngd->tx_dma = dma;
    return 0;
}

/* INT_EN, RX_MSGQ_CFG and CFG, each read back. The read-back is what makes
 * "controller up" a measured fact instead of a successful write. */
static int program_registers(VqeafSlimNgd *ngd)
{
    uint32_t readback = 0, rx_cfg = 0, cfg = 0, expect;
    int rc;

    rc = ngd_write(ngd, VQEAF_SLIM_NGD_INT_EN, VQEAF_SLIM_NGD_INT_MASK);
    if(rc) return rc;
    ngd->int_en_written = 1;
    rc = ngd_read(ngd, VQEAF_SLIM_NGD_INT_EN, &readback);
    if(rc) return rc;
    if(readback != VQEAF_SLIM_NGD_INT_MASK) return -EIO;

    rc = ngd_read(ngd, VQEAF_SLIM_NGD_RX_MSGQ_CFG, &rx_cfg);
    if(rc) return rc;
    rc = ngd_write(ngd, VQEAF_SLIM_NGD_RX_MSGQ_CFG,
                   rx_cfg | VQEAF_SLIM_NGD_RX_MSGQ_TIMEOUT);
    if(rc) return rc;
    rc = ngd_read(ngd, VQEAF_SLIM_NGD_RX_MSGQ_CFG, &readback);
    if(rc) return rc;
    if((readback & VQEAF_SLIM_NGD_RX_MSGQ_TIMEOUT) == 0) return -EIO;

    rc = ngd_read(ngd, VQEAF_SLIM_NGD_CFG, &cfg);
    if(rc) return rc;
    expect = cfg | VQEAF_SLIM_NGD_CFG_ENABLE | VQEAF_SLIM_NGD_CFG_RX_MSGQ_EN |
             VQEAF_SLIM_NGD_CFG_TX_MSGQ_EN;
    rc = ngd_write(ngd, VQEAF_SLIM_NGD_CFG, expect);
    if(rc) return rc;
    rc = ngd_read(ngd, VQEAF_SLIM_NGD_CFG, &readback);
    if(rc) return rc;
    if((readback & (VQEAF_SLIM_NGD_CFG_ENABLE | VQEAF_SLIM_NGD_CFG_RX_MSGQ_EN |
                    VQEAF_SLIM_NGD_CFG_TX_MSGQ_EN)) !=
       (VQEAF_SLIM_NGD_CFG_ENABLE | VQEAF_SLIM_NGD_CFG_RX_MSGQ_EN |
        VQEAF_SLIM_NGD_CFG_TX_MSGQ_EN))
        return -EIO;
    return 0;
}

int vqeaf_slim_ngd_start(VqeafSlimNgd *ngd)
{
    uint32_t value = 0;
    int rc;

    if(!attached(ngd)) return -ENOSYS;
    if(ngd->started) return -EBUSY;
    ngd->last_error = 0;
    ngd->irq_count = 0;
    ngd->rx_count = 0;
    ngd->tx_done_count = 0;
    ngd->error_count = 0;
    ngd->rx_pending = 0;
    ngd->tx_busy = 0;
    ngd->rx_head = 0;
    ngd->tx_tail = 0;

    rc = reg_read(ngd, 0u, &value);
    if(rc) return unwound(ngd, rc);
    ngd->version = value >> 16;

    rc = ngd_read(ngd, VQEAF_SLIM_NGD_STATUS, &ngd->status);
    if(rc) return unwound(ngd, rc);

    rc = rings_alloc(ngd);
    if(rc) return unwound(ngd, rc);

    rc = ngd->desc.bam.pipe_init(ngd->desc.bam.ctx, VQEAF_SLIM_DIR_RX);
    if(rc) return unwound(ngd, rc);
    ngd->bam_mask |= BAM_RX;
    rc = ngd->desc.bam.pipe_init(ngd->desc.bam.ctx, VQEAF_SLIM_DIR_TX);
    if(rc) return unwound(ngd, rc);
    ngd->bam_mask |= BAM_TX;

    rc = arm_rx_pipe(ngd);
    if(rc) return unwound(ngd, rc);

    rc = ngd->desc.irq_ops.request(ngd->desc.irq_ops.ctx, ngd->desc.irq,
                                   irq_thunk, ngd);
    if(rc) return unwound(ngd, rc);
    ngd->irq_requested = 1;
    rc = ngd->desc.irq_ops.enable(ngd->desc.irq_ops.ctx, ngd->desc.irq, 1);
    if(rc) return unwound(ngd, rc);

    rc = program_registers(ngd);
    if(rc) return unwound(ngd, rc);

    ngd->started = 1;
    ngd->state = VQEAF_SLIM_NGD_UP;
    return 0;
}

int vqeaf_slim_ngd_xfer(VqeafSlimNgd *ngd, const uint8_t *msg, uint32_t len)
{
    uint64_t dma;
    int rc;

    if(!attached(ngd)) return -ENOSYS;
    if(!ngd->started) return -ENODEV;
    if(msg == NULL || len == 0 || len > VQEAF_SLIM_NGD_MSG_MAX) return -EINVAL;
    if(ngd->tx_busy) return -EBUSY;

    memcpy(ngd->tx_cpu + (size_t)ngd->tx_tail * VQEAF_SLIM_NGD_MSG_MAX, msg, len);
    dma = ngd->tx_dma + (uint64_t)ngd->tx_tail * VQEAF_SLIM_NGD_MSG_MAX;
    rc = ngd->desc.bam.queue(ngd->desc.bam.ctx, VQEAF_SLIM_DIR_TX, dma, len);
    if(rc) return rc;
    rc = ngd->desc.bam.issue(ngd->desc.bam.ctx, VQEAF_SLIM_DIR_TX);
    if(rc) return rc;
    ngd->tx_tail = (uint16_t)((ngd->tx_tail + 1u) % VQEAF_SLIM_NGD_DESC_NUM);
    ngd->tx_busy = 1;
    return 0;
}

void vqeaf_slim_ngd_irq(VqeafSlimNgd *ngd)
{
    uint32_t stat = 0;
    int rc;

    if(!attached(ngd) || !ngd->started) return;
    rc = ngd_read(ngd, VQEAF_SLIM_NGD_INT_STAT, &stat);
    if(rc) {
        ngd->last_error = rc;
        ngd->error_count++;
        return;
    }
    ngd->irq_count++;
    rc = ngd_write(ngd, VQEAF_SLIM_NGD_INT_CLR, stat);
    if(rc) {
        /* Not acknowledging would re-enter the interrupt forever; record it
         * and stay up, but the platform sees a real error. */
        ngd->last_error = rc;
        ngd->error_count++;
        return;
    }
    if(stat & VQEAF_SLIM_NGD_INT_ERROR) {
        ngd->error_count++;
        ngd->last_error = -EIO;
    }
    if(stat & VQEAF_SLIM_NGD_INT_RX_MSG_RCVD) {
        if(ngd->rx_pending < (uint8_t)VQEAF_SLIM_NGD_DESC_NUM) ngd->rx_pending++;
        ngd->rx_count++;
    }
    if(stat & VQEAF_SLIM_NGD_INT_TX_MSG_SENT) {
        ngd->tx_busy = 0;
        ngd->tx_done_count++;
    }
}

int vqeaf_slim_ngd_rx_pop(VqeafSlimNgd *ngd, uint8_t *out, uint32_t cap,
                          uint32_t *len)
{
    const uint8_t *src;
    uint32_t rl;
    uint64_t dma;
    int rc;

    if(!attached(ngd)) return -ENOSYS;
    if(!ngd->started) return -ENODEV;
    if(out == NULL || len == NULL || cap == 0) return -EINVAL;
    if(ngd->rx_pending == 0) return -EAGAIN;

    src = ngd->rx_cpu + (size_t)ngd->rx_head * VQEAF_SLIM_NGD_MSG_MAX;
    /* SLIMbus message header, first word little-endian: RL in bits 4:0. */
    rl = (uint32_t)src[0] & 0x1fu;
    if(rl == 0 || rl > VQEAF_SLIM_NGD_MSG_MAX) {
        ngd->error_count++;
        ngd->last_error = -EPROTO;
        ngd->rx_pending--;
        ngd->rx_head = (uint16_t)((ngd->rx_head + 1u) % VQEAF_SLIM_NGD_DESC_NUM);
        return -EPROTO;
    }
    if(rl > cap) return -ENOSPC;

    memcpy(out, src, rl);
    *len = rl;

    dma = ngd->rx_dma + (uint64_t)ngd->rx_head * VQEAF_SLIM_NGD_MSG_MAX;
    rc = ngd->desc.bam.queue(ngd->desc.bam.ctx, VQEAF_SLIM_DIR_RX, dma,
                             VQEAF_SLIM_NGD_MSG_MAX);
    if(!rc) rc = ngd->desc.bam.issue(ngd->desc.bam.ctx, VQEAF_SLIM_DIR_RX);
    ngd->rx_pending--;
    ngd->rx_head = (uint16_t)((ngd->rx_head + 1u) % VQEAF_SLIM_NGD_DESC_NUM);
    if(rc) {
        ngd->error_count++;
        ngd->last_error = rc;
    }
    return rc;
}

void vqeaf_slim_ngd_stop(VqeafSlimNgd *ngd)
{
    if(!attached(ngd)) return;
    if(!ngd->started && !ngd->irq_requested && !ngd->bam_mask &&
       !ngd->int_en_written && ngd->rx_cpu == NULL && ngd->tx_cpu == NULL) {
        ngd->state = VQEAF_SLIM_NGD_STOPPED;
        return;
    }
    unwound(ngd, ngd->last_error);
    ngd->state = VQEAF_SLIM_NGD_STOPPED;
    ngd->last_error = 0;
}

int vqeaf_slim_ngd_check_violet_dt(const VqeafSlimNgdDesc *d)
{
    if(d == NULL || d->compatible == NULL ||
       strcmp(d->compatible, VQEAF_SLIM_NGD_COMPATIBLE) != 0)
        return -EINVAL;
    if(d->ctrl_base != VQEAF_SLIM_NGD_CTRL_BASE ||
       d->ctrl_size != VQEAF_SLIM_NGD_CTRL_SIZE)
        return -EINVAL;
    if(d->bam_base != VQEAF_SLIM_NGD_BAM_BASE ||
       d->bam_size != VQEAF_SLIM_NGD_BAM_SIZE)
        return -EINVAL;
    if(d->irq != VQEAF_SLIM_NGD_IRQ || d->bam_irq != VQEAF_SLIM_NGD_BAM_IRQ)
        return -EINVAL;
    if(d->ngd_offset != VQEAF_SLIM_NGD_REG_OFFSET)
        return -EINVAL;
    return 0;
}

const char *vqeaf_slim_ngd_state_name(vqeaf_slim_ngd_state_t state)
{
    switch(state) {
    case VQEAF_SLIM_NGD_STOPPED: return "stopped";
    case VQEAF_SLIM_NGD_UP: return "up";
    case VQEAF_SLIM_NGD_FAILED: return "failed";
    }
    return "?";
}

const char *vqeaf_slim_ngd_error_name(int err)
{
    switch(err) {
    case 0: return "none";
    case -EINVAL: return "EINVAL";
    case -ENOSYS: return "ENOSYS";
    case -ENODEV: return "ENODEV";
    case -EBUSY: return "EBUSY";
    case -EAGAIN: return "EAGAIN";
    case -ENOSPC: return "ENOSPC";
    case -ERANGE: return "ERANGE";
    case -EIO: return "EIO";
    case -EPROTO: return "EPROTO";
    }
    return "errno";
}
