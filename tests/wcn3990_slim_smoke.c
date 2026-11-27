/* SPDX-License-Identifier: MIT */
/* tests/wcn3990_slim_smoke.c - host gate for the btfmslim link.
 *
 * The NGD client is the real driver over a faked register block / BAM / IRQ /
 * DMA, so what is asserted is the actual wiring the Bluetooth driver sees:
 * open only on a controller that is UP, one message per HCI frame with the
 * TX_MSG_SENT wait, -EMSGSIZE instead of a truncated long frame, and RX through
 * rx_pop. The last group runs the real Bluetooth driver over the composed ops
 * and pins down the honest boundary: the message transport works end to end,
 * but HCI parsing fails because the btfmslim channel header is not implemented,
 * so the driver must report FAILED and never hci-ready. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vqeaf/slim_qcom_ngd.h"
#include "vqeaf/wcn3990_bt.h"
#include "vqeaf/wcn3990_slim.h"

#define MAX_REGS 32

typedef struct {
    uint32_t offset, value;
} FakeReg;

typedef struct {
    FakeReg regs[MAX_REGS];
    int nregs;
    uint8_t rx_ring[VQEAF_SLIM_NGD_RING_BYTES];
    uint8_t tx_ring[VQEAF_SLIM_NGD_RING_BYTES];
    int allocs;
    int rx_inits, tx_inits, terminates;
    void (*handler)(void *);
    void *handler_ctx;
    uint32_t irq;
    int irq_enabled, irq_freed;

    uint32_t wait_calls;
    uint32_t power_ons, power_offs;
    size_t fw_reads;
} Fake;

static FakeReg *slot(Fake *f, uint32_t offset)
{
    int i;
    for(i = 0; i < f->nregs; i++)
        if(f->regs[i].offset == offset) return &f->regs[i];
    assert(f->nregs < MAX_REGS);
    f->regs[f->nregs].offset = offset;
    f->regs[f->nregs].value = 0;
    return &f->regs[f->nregs++];
}

static int f_read(void *ctx, uint32_t offset, uint32_t *value)
{
    Fake *f = ctx;
    *value = slot(f, offset)->value;
    return 0;
}

static int f_write(void *ctx, uint32_t offset, uint32_t value)
{
    Fake *f = ctx;
    slot(f, offset)->value = value;
    return 0;
}

static void f_barrier(void *ctx) { (void)ctx; }

static int f_pipe_init(void *ctx, vqeaf_slim_dir_t dir)
{
    Fake *f = ctx;
    if(dir == VQEAF_SLIM_DIR_RX) f->rx_inits++; else f->tx_inits++;
    return 0;
}

static int f_queue(void *ctx, vqeaf_slim_dir_t dir, uint64_t dma, uint32_t len)
{
    (void)ctx; (void)dir; (void)dma; (void)len;
    return 0;
}

static int f_issue(void *ctx, vqeaf_slim_dir_t dir)
{
    (void)ctx; (void)dir;
    return 0;
}

static int f_terminate(void *ctx, vqeaf_slim_dir_t dir)
{
    Fake *f = ctx;
    (void)dir;
    f->terminates++;
    return 0;
}

static int f_irq_request(void *ctx, uint32_t irq, void (*handler)(void *),
                         void *handler_ctx)
{
    Fake *f = ctx;
    f->irq = irq;
    f->handler = handler;
    f->handler_ctx = handler_ctx;
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
    f->irq_freed = 1;
}

static int f_dma_alloc(void *ctx, size_t bytes, size_t align, void **cpu,
                       uint64_t *dma)
{
    Fake *f = ctx;
    assert(bytes == VQEAF_SLIM_NGD_RING_BYTES && align == 64u);
    if(f->allocs == 0) { *cpu = f->rx_ring; *dma = 0x20000000ull; }
    else { *cpu = f->tx_ring; *dma = 0x20002000ull; }
    f->allocs++;
    return 0;
}

static void f_dma_free(void *ctx, void *cpu, uint64_t dma, size_t bytes)
{
    (void)ctx; (void)cpu; (void)dma; (void)bytes;
}

static const VqeafSlimNgdBamOps BAM = {
    NULL, f_pipe_init, f_queue, f_issue, f_terminate
};
static const VqeafSlimNgdIrqOps IRQ = {
    NULL, f_irq_request, f_irq_enable, f_irq_free
};
static const VqeafSlimNgdDmaOps DMA = { NULL, f_dma_alloc, f_dma_free };

static void fill_desc(Fake *f, VqeafSlimNgdDesc *desc)
{
    memset(desc, 0, sizeof(*desc));
    desc->compatible = VQEAF_SLIM_NGD_COMPATIBLE;
    desc->ctrl_base = VQEAF_SLIM_NGD_CTRL_BASE;
    desc->ctrl_size = VQEAF_SLIM_NGD_CTRL_SIZE;
    desc->bam_base = VQEAF_SLIM_NGD_BAM_BASE;
    desc->bam_size = VQEAF_SLIM_NGD_BAM_SIZE;
    desc->irq = VQEAF_SLIM_NGD_IRQ;
    desc->bam_irq = VQEAF_SLIM_NGD_BAM_IRQ;
    desc->ngd_offset = VQEAF_SLIM_NGD_REG_OFFSET;
    desc->mmio.ctx = f;
    desc->mmio.read32 = f_read;
    desc->mmio.write32 = f_write;
    desc->mmio.barrier = f_barrier;
    desc->bam = BAM;
    desc->bam.ctx = f;
    desc->irq_ops = IRQ;
    desc->irq_ops.ctx = f;
    desc->dma = DMA;
    desc->dma.ctx = f;
}

static void start_client(Fake *f, VqeafSlimNgd *ngd)
{
    VqeafSlimNgdDesc desc;

    memset(f, 0, sizeof(*f));
    fill_desc(f, &desc);

    assert(vqeaf_slim_ngd_init(ngd, &desc) == 0);
    assert(vqeaf_slim_ngd_start(ngd) == 0);
    assert(ngd->state == VQEAF_SLIM_NGD_UP);
    assert(f->irq == 291u && f->irq_enabled == 1);
}

static void raise_irq(Fake *f, uint32_t bits)
{
    slot(f, VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_INT_STAT)->value = bits;
    assert(f->handler != NULL);
    f->handler(f->handler_ctx);
}

/* Controller stand-in: first wait acknowledges TX, second stages one event. */
static int ev_wait(void *ctx, uint32_t timeout_ms)
{
    Fake *f = ctx;
    (void)timeout_ms;
    f->wait_calls++;
    if(f->wait_calls == 1) {
        raise_irq(f, VQEAF_SLIM_NGD_INT_TX_MSG_SENT);
        return 0;
    }
    if(f->wait_calls == 2) {
        VqeafSlimNgd *ngd = NULL;
        /* The link owns the client; reach it through the fake's handler ctx,
         * which the client installed. */
        ngd = (VqeafSlimNgd *)f->handler_ctx;
        {
            uint8_t *dst = ngd->rx_cpu + (size_t)ngd->rx_head * VQEAF_SLIM_NGD_MSG_MAX;
            static const uint8_t event[7] = {0x04, 0x0E, 0x04, 0x01, 0x03, 0x0C, 0x00};
            memcpy(dst, event, sizeof(event));
        }
        raise_irq(f, VQEAF_SLIM_NGD_INT_RX_MSG_RCVD);
        return 0;
    }
    return 0;
}

static void test_link_state_machine(void)
{
    static VqeafSlimNgd ngd;
    static Fake f;
    VqeafBtSlimLink link;
    const uint8_t peer[6] = {0x00, 0x01, 0x20, 0x02, 0x17, 0x02};
    const uint8_t ifd[6] = {0x00, 0x00, 0x20, 0x02, 0x17, 0x02};
    uint8_t frame[VQEAF_SLIM_NGD_MSG_MAX + 1];
    uint8_t out[VQEAF_SLIM_NGD_MSG_MAX];
    size_t got = 0;

    /* No client yet: every entry point refuses instead of crashing. */
    vqeaf_bt_slim_init(&link, NULL);
    assert(vqeaf_bt_slim_write(&link, frame, 4) == -ENOSYS);
    assert(vqeaf_bt_slim_read(&link, out, sizeof(out), &got, 10) == -ENOSYS);

    memset(&ngd, 0, sizeof(ngd));
    vqeaf_bt_slim_init(&link, &ngd);
    /* A zeroed client is not even a contract instance. */
    assert(vqeaf_bt_slim_open(&link, peer, ifd) == -ENOSYS);
    {
        VqeafSlimNgdDesc desc;
        fill_desc(&f, &desc);
        assert(vqeaf_slim_ngd_init(&ngd, &desc) == 0);
        /* Initialised but never started: no frame can be carried. */
        assert(vqeaf_bt_slim_open(&link, peer, ifd) == -ENODEV);
    }

    start_client(&f, &ngd);
    vqeaf_bt_slim_init(&link, &ngd);

    memset(frame, 0, sizeof(frame));
    frame[0] = 0x01; frame[1] = 0x03; frame[2] = 0x0C; frame[3] = 0x00;

    /* Addresses are validated before the link is marked attached. */
    {
        const uint8_t zero[6] = {0};
        assert(vqeaf_bt_slim_open(&link, zero, ifd) == -EINVAL);
        assert(vqeaf_bt_slim_open(&link, peer, peer) == -EINVAL);
        assert(link.attached == 0);
    }

    assert(vqeaf_bt_slim_open(&link, peer, ifd) == 0);
    assert(link.attached == 1 && link.peer_set == 1);
    assert(memcmp(link.peer_ea, peer, 6) == 0);
    assert(memcmp(link.ifd_ea, ifd, 6) == 0);
    assert(vqeaf_bt_slim_open(&link, peer, ifd) == -EBUSY);

    /* Long frame: refused, never truncated. */
    assert(vqeaf_bt_slim_write(&link, frame, sizeof(frame)) == -EMSGSIZE);
    assert(link.tx_failures == 1);

    /* No wait hook: the client accepted the message, so it is reported as
     * queued (not sent), and the next one is refused while TX is busy. */
    assert(vqeaf_bt_slim_write(&link, frame, 4) == 0);
    assert(link.tx_queued == 1 && link.tx_sent == 0);
    assert(ngd.tx_busy == 1);
    assert(vqeaf_bt_slim_write(&link, frame, 4) == -EBUSY);

    /* TX_MSG_SENT clears the slot; the next write is accepted again. */
    raise_irq(&f, VQEAF_SLIM_NGD_INT_TX_MSG_SENT);
    assert(ngd.tx_busy == 0);
    assert(vqeaf_bt_slim_write(&link, frame, 4) == 0);
    raise_irq(&f, VQEAF_SLIM_NGD_INT_TX_MSG_SENT);

    /* Nothing received and no wait hook: a timeout, not an empty success. */
    got = 99;
    assert(vqeaf_bt_slim_read(&link, out, sizeof(out), &got, 10) == -ETIMEDOUT);
    assert(got == 0);
    assert(link.rx_timeouts == 1);

    /* One delivered message is copied through rx_pop, RL from the header. */
    {
        uint8_t *dst = ngd.rx_cpu + (size_t)ngd.rx_head * VQEAF_SLIM_NGD_MSG_MAX;
        dst[0] = 0x04; dst[1] = 0x0E; dst[2] = 0x04; dst[3] = 0x01;
    }
    raise_irq(&f, VQEAF_SLIM_NGD_INT_RX_MSG_RCVD);
    assert(ngd.rx_pending == 1);
    assert(vqeaf_bt_slim_read(&link, out, sizeof(out), &got, 10) == 0);
    assert(got == 4 && out[0] == 0x04 && out[1] == 0x0E);
    assert(link.msgs_in == 1);

    /* A too-small buffer is a caller error, not a truncated message. */
    {
        uint8_t *dst = ngd.rx_cpu + (size_t)ngd.rx_head * VQEAF_SLIM_NGD_MSG_MAX;
        dst[0] = 0x08;
    }
    raise_irq(&f, VQEAF_SLIM_NGD_INT_RX_MSG_RCVD);
    assert(vqeaf_bt_slim_read(&link, out, 2u, &got, 10) == -ENOSPC);

    vqeaf_bt_slim_close(&link);
    assert(link.attached == 0);
    assert(vqeaf_bt_slim_write(&link, frame, 4) == -ENODEV);
}

/* ---- end to end: the Bluetooth driver over this transport ---- */

static int inner_power_on(void *ctx, const char *rail, uint32_t uv, uint32_t ua)
{
    Fake *f = ctx;
    assert(rail != NULL && uv != 0 && ua != 0);
    f->power_ons++;
    return 0;
}

static void inner_power_off(void *ctx, const char *rail)
{
    Fake *f = ctx;
    assert(rail != NULL);
    f->power_offs++;
}

static int inner_firmware(void *ctx, const char *name, uint8_t *buf,
                          size_t cap, size_t *len)
{
    static const uint8_t image[8] = {'B', 'T', 'F', 'M', 1, 2, 3, 4};
    Fake *f = ctx;
    (void)name;
    if(cap < sizeof(image)) return -ENOSPC;
    memcpy(buf, image, sizeof(image));
    *len = sizeof(image);
    f->fw_reads++;
    return 0;
}

static void inner_log(void *ctx, const char *line)
{
    (void)ctx; (void)line;
}

static void test_bt_end_to_end(void)
{
    static VqeafSlimNgd ngd;
    static Fake f;
    static VqeafBtSlimBridge bridge;
    static uint8_t fw[64];
    vqeaf_bt_ops_t inner;
    vqeaf_bt_t bt;

    start_client(&f, &ngd);

    memset(&inner, 0, sizeof(inner));
    inner.ctx = &f;
    inner.power_on = inner_power_on;
    inner.power_off = inner_power_off;
    inner.firmware_read = inner_firmware;
    inner.log = inner_log;

    /* Compose refuses a platform that cannot power or read firmware. */
    {
        vqeaf_bt_ops_t broken = inner;
        broken.power_on = NULL;
        assert(vqeaf_bt_slim_compose(&bridge, &broken, &ngd) == -EINVAL);
    }
    assert(vqeaf_bt_slim_compose(&bridge, &inner, &ngd) == 0);
    assert(bridge.ops.ctx == &bridge);
    assert(bridge.ops.write != NULL && bridge.ops.read != NULL);

    bridge.link.wait_ready = ev_wait;
    bridge.link.wait_ctx = &f;

    vqeaf_bt_init(&bt, &bridge.ops);
    /* The event staged by ev_wait() is a real HCI Command Complete preceded by
     * the SLIMbus message header, which this bridge does not know how to
     * strip. The driver must therefore stop at EPROTO, not claim success. */
    assert(vqeaf_bt_start(&bt, "BTFM.bin", fw, sizeof(fw)) == -EPROTO);
    assert(bt.stage == VQEAF_BT_FAILED);
    assert(bt.hci_ready == 0);

    /* Everything below the framing layer really ran. */
    assert(f.power_ons == 3);
    assert(f.fw_reads == 1);
    assert(bridge.link.tx_sent == 1);
    assert(bridge.link.msgs_in == 1);
    assert(bridge.link.msgs_out == 1);
    /* Failure unwound the transport and every rail. */
    assert(bridge.link.attached == 0);
    assert(f.power_offs == 3);

    vqeaf_bt_stop(&bt);
    assert(bt.stage == VQEAF_BT_STOPPED);
    assert(bt.hci_ready == 0);
    assert(f.power_offs == 3);
}

int main(void)
{
    test_link_state_machine();
    test_bt_end_to_end();
    printf("wcn3990_slim: 2 groups of link checks passed\n");
    return 0;
}
