/* SPDX-License-Identifier: MIT */
/* tests/slim_qcom_ngd_smoke.c - host gate for the qcom,slim-ngd client.
 *
 * The MMIO block, BAM pipes, IRQ and DMA are faked; that is the point of the
 * injected contract. What is asserted is the real state machine: the register
 * sequence with its read-backs, reverse-order unwinding on every failure,
 * TX never reported delivered without TX_MSG_SENT, and RX descriptor re-arm.
 * No value here is hardware evidence. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vqeaf/slim_qcom_ngd.h"

#define MAX_REGS 32
#define MAX_QUEUE 64

typedef struct {
    uint32_t offset, value;
} FakeReg;

typedef struct {
    FakeReg regs[MAX_REGS];
    int nregs;
    int fail_read, fail_write;        /* absolute offset, -1 none */
    int stuck_int_en;                 /* INT_EN read-back does not stick */
    uint32_t reads, writes;

    int rx_inited, tx_inited, rx_terminated, tx_terminated;
    int fail_rx_init, fail_tx_init, fail_rx_terminate;
    uint64_t rx_queue_dma[MAX_QUEUE];
    uint32_t rx_queue_len[MAX_QUEUE];
    int rx_queued, rx_issues, fail_rx_queue_at, fail_rx_issue;
    uint64_t tx_queue_dma[MAX_QUEUE];
    uint32_t tx_queue_len[MAX_QUEUE];
    int tx_queued, tx_issues, fail_tx_queue_at;

    void (*handler)(void *);
    void *handler_ctx;
    uint32_t irq_requested;
    int irq_enabled, irq_freed;

    uint8_t rx_buf[VQEAF_SLIM_NGD_RING_BYTES];
    uint8_t tx_buf[VQEAF_SLIM_NGD_RING_BYTES];
    uint64_t rx_dma, tx_dma;
    int allocs, frees;
} Fake;

static FakeReg *reg_slot(Fake *f, uint32_t offset)
{
    int i;
    for(i = 0; i < f->nregs; i++)
        if(f->regs[i].offset == offset) return &f->regs[i];
    assert(f->nregs < MAX_REGS);
    f->regs[f->nregs].offset = offset;
    f->regs[f->nregs].value = 0;
    return &f->regs[f->nregs++];
}

static uint32_t reg_value(Fake *f, uint32_t offset)
{
    return reg_slot(f, offset)->value;
}

static int f_read(void *ctx, uint32_t offset, uint32_t *value)
{
    Fake *f = ctx;
    f->reads++;
    if(f->fail_read >= 0 && (uint32_t)f->fail_read == offset) return -EIO;
    *value = reg_slot(f, offset)->value;
    return 0;
}

static int f_write(void *ctx, uint32_t offset, uint32_t value)
{
    Fake *f = ctx;
    f->writes++;
    if(f->fail_write >= 0 && (uint32_t)f->fail_write == offset) return -EIO;
    if(offset == VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_INT_EN &&
       f->stuck_int_en) {
        reg_slot(f, offset)->value = value & ~VQEAF_SLIM_NGD_INT_MASK;
        return 0;
    }
    reg_slot(f, offset)->value = value;
    return 0;
}

static void f_barrier(void *ctx)
{
    (void)ctx;
}

static int f_pipe_init(void *ctx, vqeaf_slim_dir_t dir)
{
    Fake *f = ctx;
    if(dir == VQEAF_SLIM_DIR_RX) {
        if(f->fail_rx_init) return -EIO;
        f->rx_inited++;
    } else {
        if(f->fail_tx_init) return -EIO;
        f->tx_inited++;
    }
    return 0;
}

static int f_queue(void *ctx, vqeaf_slim_dir_t dir, uint64_t dma, uint32_t len)
{
    Fake *f = ctx;
    if(dir == VQEAF_SLIM_DIR_RX) {
        if(f->fail_rx_queue_at > 0 &&
           f->rx_queued + 1 == f->fail_rx_queue_at) return -EIO;
        assert(f->rx_queued < MAX_QUEUE);
        f->rx_queue_dma[f->rx_queued] = dma;
        f->rx_queue_len[f->rx_queued] = len;
        f->rx_queued++;
    } else {
        if(f->fail_tx_queue_at > 0 &&
           f->tx_queued + 1 == f->fail_tx_queue_at) return -EIO;
        assert(f->tx_queued < MAX_QUEUE);
        f->tx_queue_dma[f->tx_queued] = dma;
        f->tx_queue_len[f->tx_queued] = len;
        f->tx_queued++;
    }
    return 0;
}

static int f_issue(void *ctx, vqeaf_slim_dir_t dir)
{
    Fake *f = ctx;
    if(dir == VQEAF_SLIM_DIR_RX) {
        if(f->fail_rx_issue) return -EIO;
        f->rx_issues++;
    } else {
        f->tx_issues++;
    }
    return 0;
}

static int f_terminate(void *ctx, vqeaf_slim_dir_t dir)
{
    Fake *f = ctx;
    if(dir == VQEAF_SLIM_DIR_RX) {
        if(f->fail_rx_terminate) return -EIO;
        f->rx_terminated++;
    } else {
        f->tx_terminated++;
    }
    return 0;
}

static int f_irq_request(void *ctx, uint32_t irq, void (*handler)(void *),
                         void *handler_ctx)
{
    Fake *f = ctx;
    assert(irq == VQEAF_SLIM_NGD_IRQ || irq == VQEAF_SLIM_NGD_BAM_IRQ);
    if(irq != VQEAF_SLIM_NGD_IRQ) return -EINVAL;
    f->handler = handler;
    f->handler_ctx = handler_ctx;
    f->irq_requested = irq;
    return 0;
}

static int f_irq_enable(void *ctx, uint32_t irq, int on)
{
    Fake *f = ctx;
    (void)irq;
    f->irq_enabled = on;
    return 0;
}

static void f_irq_free(void *ctx, uint32_t irq)
{
    Fake *f = ctx;
    (void)irq;
    f->irq_freed++;
    f->irq_requested = 0;
}

static int f_dma_alloc(void *ctx, size_t bytes, size_t align, void **cpu,
                       uint64_t *dma)
{
    Fake *f = ctx;
    assert(bytes == VQEAF_SLIM_NGD_RING_BYTES);
    assert(align >= 4u);
    if(f->allocs == 0) {
        *cpu = f->rx_buf;
        *dma = 0x8a000000ull;
    } else if(f->allocs == 1) {
        *cpu = f->tx_buf;
        *dma = 0x8a010000ull;
    } else {
        return -ENOMEM;
    }
    f->allocs++;
    return 0;
}

static void f_dma_release(void *ctx, void *cpu, uint64_t dma, size_t bytes)
{
    Fake *f = ctx;
    assert(bytes == VQEAF_SLIM_NGD_RING_BYTES);
    if(cpu == f->rx_buf) f->rx_dma = dma;
    else if(cpu == f->tx_buf) f->tx_dma = dma;
    else assert(0);
    f->frees++;
}

static void fake_init(Fake *f)
{
    memset(f, 0, sizeof(*f));
    f->fail_read = -1;
    f->fail_write = -1;
}

static VqeafSlimNgdDesc desc_of(Fake *f)
{
    VqeafSlimNgdDesc d;
    memset(&d, 0, sizeof(d));
    d.compatible = VQEAF_SLIM_NGD_COMPATIBLE;
    d.ctrl_base = VQEAF_SLIM_NGD_CTRL_BASE;
    d.ctrl_size = VQEAF_SLIM_NGD_CTRL_SIZE;
    d.bam_base = VQEAF_SLIM_NGD_BAM_BASE;
    d.bam_size = VQEAF_SLIM_NGD_BAM_SIZE;
    d.irq = VQEAF_SLIM_NGD_IRQ;
    d.bam_irq = VQEAF_SLIM_NGD_BAM_IRQ;
    d.ngd_offset = VQEAF_SLIM_NGD_REG_OFFSET;
    d.mmio.ctx = f;
    d.mmio.read32 = f_read;
    d.mmio.write32 = f_write;
    d.mmio.barrier = f_barrier;
    d.bam.ctx = f;
    d.bam.pipe_init = f_pipe_init;
    d.bam.queue = f_queue;
    d.bam.issue = f_issue;
    d.bam.terminate = f_terminate;
    d.irq_ops.ctx = f;
    d.irq_ops.request = f_irq_request;
    d.irq_ops.enable = f_irq_enable;
    d.irq_ops.free = f_irq_free;
    d.dma.ctx = f;
    d.dma.alloc = f_dma_alloc;
    d.dma.release = f_dma_release;
    return d;
}

static void start_ok(Fake *f, VqeafSlimNgd *ngd, VqeafSlimNgdDesc *d)
{
    fake_init(f);
    reg_slot(f, 0u)->value = 0x1234u << 16;
    *d = desc_of(f);
    memset(ngd, 0, sizeof(*ngd));
    assert(vqeaf_slim_ngd_init(ngd, d) == 0);
    assert(vqeaf_slim_ngd_start(ngd) == 0);
    assert(ngd->state == VQEAF_SLIM_NGD_UP && ngd->started == 1);
}

static void test_missing_contract(void)
{
    VqeafSlimNgd ngd;
    VqeafSlimNgdDesc d;
    Fake f;

    memset(&ngd, 0, sizeof(ngd));
    assert(vqeaf_slim_ngd_start(&ngd) == -ENOSYS);
    assert(vqeaf_slim_ngd_xfer(&ngd, (const uint8_t *)"x", 1) == -ENOSYS);

    fake_init(&f);
    d = desc_of(&f);
    d.mmio.write32 = NULL;
    memset(&ngd, 0, sizeof(ngd));
    assert(vqeaf_slim_ngd_init(&ngd, &d) == -EINVAL);

    d = desc_of(&f);
    d.compatible = "qcom,slim-ngd-v1.5.0";
    assert(vqeaf_slim_ngd_init(&ngd, &d) == -EINVAL);
    assert(vqeaf_slim_ngd_check_violet_dt(&d) == -EINVAL);

    d = desc_of(&f);
    d.ctrl_size = 0x1000u;                     /* NGD block does not fit */
    assert(vqeaf_slim_ngd_init(&ngd, &d) == -EINVAL);

    d = desc_of(&f);
    d.irq = d.bam_irq;
    assert(vqeaf_slim_ngd_init(&ngd, &d) == -EINVAL);

    d = desc_of(&f);
    d.dma.release = NULL;
    assert(vqeaf_slim_ngd_init(&ngd, &d) == -EINVAL);
}

static void test_violet_dt(void)
{
    Fake f;
    VqeafSlimNgdDesc d;

    fake_init(&f);
    d = desc_of(&f);
    assert(vqeaf_slim_ngd_check_violet_dt(&d) == 0);

    d.ctrl_base = 0x62dc0000ull;               /* slim_aud, not slim_qca */
    assert(vqeaf_slim_ngd_check_violet_dt(&d) == -EINVAL);
    d = desc_of(&f);
    d.irq = 163u;
    assert(vqeaf_slim_ngd_check_violet_dt(&d) == -EINVAL);
    d = desc_of(&f);
    d.bam_size = 0x2a000u;
    assert(vqeaf_slim_ngd_check_violet_dt(&d) == -EINVAL);
    d = desc_of(&f);
    d.ngd_offset = 0u;
    assert(vqeaf_slim_ngd_check_violet_dt(&d) == -EINVAL);
}

static void test_start_programs_and_verifies(void)
{
    Fake f;
    VqeafSlimNgd ngd;
    VqeafSlimNgdDesc d;
    uint32_t i;

    start_ok(&f, &ngd, &d);
    assert(ngd.version == 0x1234u);
    assert(reg_value(&f, VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_INT_EN) ==
           VQEAF_SLIM_NGD_INT_MASK);
    assert(reg_value(&f, VQEAF_SLIM_NGD_REG_OFFSET +
                     VQEAF_SLIM_NGD_RX_MSGQ_CFG) &
           VQEAF_SLIM_NGD_RX_MSGQ_TIMEOUT);
    assert((reg_value(&f, VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_CFG) &
            (VQEAF_SLIM_NGD_CFG_ENABLE | VQEAF_SLIM_NGD_CFG_RX_MSGQ_EN |
             VQEAF_SLIM_NGD_CFG_TX_MSGQ_EN)) ==
           (VQEAF_SLIM_NGD_CFG_ENABLE | VQEAF_SLIM_NGD_CFG_RX_MSGQ_EN |
            VQEAF_SLIM_NGD_CFG_TX_MSGQ_EN));
    assert(f.rx_inited == 1 && f.tx_inited == 1);
    assert(f.rx_queued == (int)VQEAF_SLIM_NGD_DESC_NUM && f.rx_issues == 1);
    for(i = 0; i < VQEAF_SLIM_NGD_DESC_NUM; i++) {
        assert(f.rx_queue_dma[i] == 0x8a000000ull + i * VQEAF_SLIM_NGD_MSG_MAX);
        assert(f.rx_queue_len[i] == VQEAF_SLIM_NGD_MSG_MAX);
    }
    assert(f.irq_requested == VQEAF_SLIM_NGD_IRQ && f.irq_enabled == 1);
    assert(f.handler != NULL && f.handler_ctx == &ngd);
    assert(ngd.last_error == 0);

    vqeaf_slim_ngd_stop(&ngd);
    assert(ngd.state == VQEAF_SLIM_NGD_STOPPED);
    assert(f.irq_enabled == 0 && f.irq_freed == 1);
    assert(f.rx_terminated == 1 && f.tx_terminated == 1);
    assert(f.frees == 2);
    vqeaf_slim_ngd_stop(&ngd);                 /* idempotent */
    assert(f.irq_freed == 1 && f.frees == 2);
}

static void test_start_failures_unwind(void)
{
    Fake f;
    VqeafSlimNgd ngd;
    VqeafSlimNgdDesc d;

    /* Register block unreadable: nothing acquired. */
    fake_init(&f);
    f.fail_read = 0;
    d = desc_of(&f);
    memset(&ngd, 0, sizeof(ngd));
    assert(vqeaf_slim_ngd_init(&ngd, &d) == 0);
    assert(vqeaf_slim_ngd_start(&ngd) == -EIO);
    assert(ngd.state == VQEAF_SLIM_NGD_FAILED && ngd.last_error == -EIO);
    assert(f.allocs == 0 && f.irq_requested == 0);

    /* RX pipe init fails: rings released, no IRQ taken. */
    fake_init(&f);
    f.fail_rx_init = 1;
    d = desc_of(&f);
    memset(&ngd, 0, sizeof(ngd));
    assert(vqeaf_slim_ngd_init(&ngd, &d) == 0);
    assert(vqeaf_slim_ngd_start(&ngd) == -EIO);
    assert(f.frees == 2 && f.irq_requested == 0);
    assert(f.rx_inited == 0 && f.tx_inited == 0);

    /* 7th RX descriptor cannot be queued: both pipes terminated, rings freed. */
    fake_init(&f);
    f.fail_rx_queue_at = 7;
    d = desc_of(&f);
    memset(&ngd, 0, sizeof(ngd));
    assert(vqeaf_slim_ngd_init(&ngd, &d) == 0);
    assert(vqeaf_slim_ngd_start(&ngd) == -EIO);
    assert(f.rx_terminated == 1 && f.tx_terminated == 1);
    assert(f.frees == 2 && f.irq_requested == 0);

    /* INT_EN does not stick: read-back catches it, IRQ is released again. */
    fake_init(&f);
    f.stuck_int_en = 1;
    d = desc_of(&f);
    memset(&ngd, 0, sizeof(ngd));
    assert(vqeaf_slim_ngd_init(&ngd, &d) == 0);
    assert(vqeaf_slim_ngd_start(&ngd) == -EIO);
    assert(f.irq_enabled == 0 && f.irq_freed == 1);
    assert(f.rx_terminated == 1 && f.tx_terminated == 1 && f.frees == 2);
    assert(ngd.state == VQEAF_SLIM_NGD_FAILED);

    /* RX_MSGQ_CFG write fails late in the sequence. */
    fake_init(&f);
    f.fail_write = (int)(VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_RX_MSGQ_CFG);
    d = desc_of(&f);
    memset(&ngd, 0, sizeof(ngd));
    assert(vqeaf_slim_ngd_init(&ngd, &d) == 0);
    assert(vqeaf_slim_ngd_start(&ngd) == -EIO);
    assert(f.irq_freed == 1 && f.frees == 2);
    assert((reg_value(&f, VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_INT_EN)) == 0);
}

static void test_tx_needs_sent_interrupt(void)
{
    Fake f;
    VqeafSlimNgd ngd;
    VqeafSlimNgdDesc d;
    const uint8_t msg[4] = {0x01, 0x03, 0x0C, 0x00};

    start_ok(&f, &ngd, &d);
    assert(vqeaf_slim_ngd_xfer(&ngd, msg, 4) == 0);
    assert(f.tx_queued == 1 && f.tx_issues == 1);
    assert(f.tx_queue_dma[0] == 0x8a010000ull && f.tx_queue_len[0] == 4u);
    assert(memcmp(f.tx_buf, msg, 4) == 0);
    assert(ngd.tx_busy == 1 && ngd.tx_done_count == 0);
    assert(vqeaf_slim_ngd_xfer(&ngd, msg, 4) == -EBUSY);

    /* A spurious interrupt without TX_MSG_SENT must not free the pipe. */
    reg_slot(&f, VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_INT_STAT)->value =
        VQEAF_SLIM_NGD_INT_RX_MSG_RCVD;
    vqeaf_slim_ngd_irq(&ngd);
    assert(ngd.tx_busy == 1);
    assert(ngd.rx_pending == 1 && ngd.rx_count == 1);

    reg_slot(&f, VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_INT_STAT)->value =
        VQEAF_SLIM_NGD_INT_TX_MSG_SENT;
    vqeaf_slim_ngd_irq(&ngd);
    assert(ngd.tx_busy == 0 && ngd.tx_done_count == 1);
    assert(vqeaf_slim_ngd_xfer(&ngd, msg, 4) == 0);
    assert(f.tx_queue_dma[1] == 0x8a010000ull + VQEAF_SLIM_NGD_MSG_MAX);

    /* Limits are enforced before touching the pipe. */
    assert(vqeaf_slim_ngd_xfer(&ngd, msg, 0) == -EINVAL);
    assert(vqeaf_slim_ngd_xfer(&ngd, msg, VQEAF_SLIM_NGD_MSG_MAX + 1u) == -EINVAL);
    assert(vqeaf_slim_ngd_xfer(&ngd, NULL, 4) == -EINVAL);
    vqeaf_slim_ngd_stop(&ngd);
    assert(vqeaf_slim_ngd_xfer(&ngd, msg, 4) == -ENODEV);
}

static void test_irq_paths(void)
{
    Fake f;
    VqeafSlimNgd ngd;
    VqeafSlimNgdDesc d;

    start_ok(&f, &ngd, &d);

    /* Error bits are counted and keep the controller up. */
    reg_slot(&f, VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_INT_STAT)->value =
        VQEAF_SLIM_NGD_INT_DEV_ERR;
    vqeaf_slim_ngd_irq(&ngd);
    assert(ngd.error_count == 1 && ngd.last_error == -EIO);
    assert(ngd.state == VQEAF_SLIM_NGD_UP);

    /* Status read failure is real, not a silent no-op. */
    f.fail_read = (int)(VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_INT_STAT);
    ngd.irq_count = 0;
    vqeaf_slim_ngd_irq(&ngd);
    assert(ngd.irq_count == 0 && ngd.last_error == -EIO);
    f.fail_read = -1;

    /* Acknowledgement failure must not double-count the event. */
    reg_slot(&f, VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_INT_STAT)->value =
        VQEAF_SLIM_NGD_INT_RX_MSG_RCVD;
    f.fail_write = (int)(VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_INT_CLR);
    ngd.rx_count = 0;
    vqeaf_slim_ngd_irq(&ngd);
    assert(ngd.rx_count == 0 && ngd.last_error == -EIO);
    f.fail_write = -1;

    vqeaf_slim_ngd_stop(&ngd);
    /* After stop the handler is inert. */
    reg_slot(&f, VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_INT_STAT)->value =
        VQEAF_SLIM_NGD_INT_RX_MSG_RCVD;
    ngd.rx_count = 0;
    vqeaf_slim_ngd_irq(&ngd);
    assert(ngd.rx_count == 0);
}

static void test_rx_pop(void)
{
    Fake f;
    VqeafSlimNgd ngd;
    VqeafSlimNgdDesc d;
    uint8_t out[VQEAF_SLIM_NGD_MSG_MAX];
    uint32_t len = 0;
    uint32_t i;

    start_ok(&f, &ngd, &d);
    assert(vqeaf_slim_ngd_rx_pop(&ngd, out, sizeof(out), &len) == -EAGAIN);

    /* Two received messages: header RL=8 then RL=12. */
    f.rx_buf[0] = 0x08;
    for(i = 1; i < 8; i++) f.rx_buf[i] = (uint8_t)(0x40 + i);
    f.rx_buf[VQEAF_SLIM_NGD_MSG_MAX] = 0x0C;
    f.rx_buf[VQEAF_SLIM_NGD_MSG_MAX + 1] = 0x7A;
    reg_slot(&f, VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_INT_STAT)->value =
        VQEAF_SLIM_NGD_INT_RX_MSG_RCVD;
    vqeaf_slim_ngd_irq(&ngd);
    vqeaf_slim_ngd_irq(&ngd);
    assert(ngd.rx_pending == 2);

    f.rx_queued = 0;
    assert(vqeaf_slim_ngd_rx_pop(&ngd, out, sizeof(out), &len) == 0);
    assert(len == 8u && out[0] == 0x08 && out[7] == 0x47);
    assert(f.rx_queued == 1 && f.rx_queue_dma[0] == 0x8a000000ull);
    assert(ngd.rx_head == 1 && ngd.rx_pending == 1);

    /* Buffer too small: the message stays pending, nothing re-armed. */
    f.rx_queued = 0;
    assert(vqeaf_slim_ngd_rx_pop(&ngd, out, 4, &len) == -ENOSPC);
    assert(ngd.rx_head == 1 && ngd.rx_pending == 1 && f.rx_queued == 0);

    assert(vqeaf_slim_ngd_rx_pop(&ngd, out, sizeof(out), &len) == 0);
    assert(len == 12u && out[1] == 0x7A);
    assert(f.rx_queue_dma[0] ==
           0x8a000000ull + VQEAF_SLIM_NGD_MSG_MAX);
    assert(ngd.rx_pending == 0);

    /* Zero-length header is a protocol error and frees the descriptor. */
    f.rx_buf[2 * VQEAF_SLIM_NGD_MSG_MAX] = 0x00;
    reg_slot(&f, VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_INT_STAT)->value =
        VQEAF_SLIM_NGD_INT_RX_MSG_RCVD;
    vqeaf_slim_ngd_irq(&ngd);
    assert(vqeaf_slim_ngd_rx_pop(&ngd, out, sizeof(out), &len) == -EPROTO);
    assert(ngd.rx_pending == 0 && ngd.last_error == -EPROTO);

    /* Re-arm failure is reported, and the message is still delivered. */
    {
        size_t at = (size_t)ngd.rx_head * VQEAF_SLIM_NGD_MSG_MAX;
        f.rx_buf[at] = 0x04;
        f.rx_buf[at + 1] = 0xAA;
    }
    reg_slot(&f, VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_INT_STAT)->value =
        VQEAF_SLIM_NGD_INT_RX_MSG_RCVD;
    vqeaf_slim_ngd_irq(&ngd);
    f.fail_rx_issue = 1;
    assert(vqeaf_slim_ngd_rx_pop(&ngd, out, sizeof(out), &len) == -EIO);
    assert(len == 4u && out[1] == 0xAA && ngd.last_error == -EIO);
    assert(ngd.rx_pending == 0);
    vqeaf_slim_ngd_stop(&ngd);
}

int main(void)
{
    test_missing_contract();
    test_violet_dt();
    test_start_programs_and_verifies();
    test_start_failures_unwind();
    test_tx_needs_sent_interrupt();
    test_irq_paths();
    test_rx_pop();
    printf("slim_qcom_ngd: 7 groups of lifecycle checks passed\n");
    return 0;
}
