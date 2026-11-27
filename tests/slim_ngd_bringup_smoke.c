/* SPDX-License-Identifier: MIT */
/* tests/slim_ngd_bringup_smoke.c - host gate for the Violet NGD provider.
 *
 * The hardware primitives are faked (that is the injected design); what is
 * asserted is the provider's wiring: the descriptor carries the exact
 * sm6150.dtsi numbers, mapping goes through the hook with the right physical
 * addresses, DMA/BAM/IRQ hooks reach the client, the controller IRQ entry
 * point feeds the client state machine, and without hooks the bring-up fails
 * at the first missing primitive instead of reporting a fake UP. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slim_ngd_bringup.h"
#include "vqeaf/slim_qcom_ngd.h"

#define WINDOW_BYTES VQEAF_SLIM_NGD_CTRL_SIZE

static uint8_t g_window[0x30000];
static uint8_t g_bam_window[0x20000];
static uint8_t g_rx_ring[VQEAF_SLIM_NGD_RING_BYTES];
static uint8_t g_tx_ring[VQEAF_SLIM_NGD_RING_BYTES];

static int maps, unmaps;
static uint64_t mapped_phys[4];
static uint64_t mapped_size[4];
static int dma_allocs, dma_frees;
static int bam_inits, bam_terminates, bam_queues;
static uint32_t irq_requested, irq_enabled_on;
static int irq_requests, irq_enables, irq_frees;
static void (*irq_handler)(void *);
static void *irq_handler_ctx;

static uint32_t r32(uint32_t offset)
{
    return *(uint32_t *)(g_window + offset);
}

static void w32(uint32_t offset, uint32_t value)
{
    *(uint32_t *)(g_window + offset) = value;
}

static void *fake_map(void *ctx, uint64_t phys, uint64_t size)
{
    (void)ctx;
    assert(maps < 4);
    mapped_phys[maps] = phys;
    mapped_size[maps] = size;
    maps++;
    if(phys == VQEAF_SLIM_NGD_CTRL_BASE) {
        assert(size == VQEAF_SLIM_NGD_CTRL_SIZE);
        return g_window;
    }
    if(phys == VQEAF_SLIM_NGD_BAM_BASE) {
        assert(size == VQEAF_SLIM_NGD_BAM_SIZE);
        return g_bam_window;
    }
    assert(0 && "unexpected window");
    return NULL;
}

static void fake_unmap(void *ctx, void *addr, uint64_t size)
{
    (void)ctx; (void)addr; (void)size;
    unmaps++;
}

static int fake_dma_alloc(void *ctx, size_t bytes, size_t align, void **cpu,
                          uint64_t *dma)
{
    (void)ctx;
    assert(bytes == VQEAF_SLIM_NGD_RING_BYTES);
    assert(align == 64u);
    if(dma_allocs == 0) {
        *cpu = g_rx_ring;
        *dma = 0x10000000ull;
    } else {
        *cpu = g_tx_ring;
        *dma = 0x10001000ull;
    }
    dma_allocs++;
    return 0;
}

static void fake_dma_free(void *ctx, void *cpu, uint64_t dma, size_t bytes)
{
    (void)ctx; (void)cpu; (void)dma; (void)bytes;
    dma_frees++;
}

static int fake_bam_pipe(void *ctx, vqeaf_slim_dir_t dir)
{
    (void)ctx;
    assert(dir == VQEAF_SLIM_DIR_RX || dir == VQEAF_SLIM_DIR_TX);
    bam_inits++;
    return 0;
}

static int fake_bam_queue(void *ctx, vqeaf_slim_dir_t dir, uint64_t dma,
                          uint32_t len)
{
    (void)ctx; (void)dir;
    assert(dma != 0);
    assert(len > 0 && len <= VQEAF_SLIM_NGD_MSG_MAX);
    bam_queues++;
    return 0;
}

static int fake_bam_issue(void *ctx, vqeaf_slim_dir_t dir)
{
    (void)ctx; (void)dir;
    return 0;
}

static int fake_bam_terminate(void *ctx, vqeaf_slim_dir_t dir)
{
    (void)ctx; (void)dir;
    bam_terminates++;
    return 0;
}

static int fake_irq_request(void *ctx, uint32_t irq, void (*handler)(void *),
                            void *handler_ctx)
{
    (void)ctx;
    irq_requested = irq;
    irq_handler = handler;
    irq_handler_ctx = handler_ctx;
    irq_requests++;
    return 0;
}

static int fake_irq_enable(void *ctx, uint32_t irq, int on)
{
    (void)ctx;
    irq_enabled_on = on ? irq : 0u;
    irq_enables++;
    return 0;
}

static void fake_irq_free(void *ctx, uint32_t irq)
{
    (void)ctx; (void)irq;
    irq_frees++;
}

static const VqeafSlimNgdBamOps BAM_OPS = {
    NULL, fake_bam_pipe, fake_bam_queue, fake_bam_issue, fake_bam_terminate
};
static const VqeafSlimNgdIrqOps IRQ_OPS = {
    NULL, fake_irq_request, fake_irq_enable, fake_irq_free
};

static void reset_fakes(void)
{
    memset(g_window, 0, sizeof(g_window));
    memset(g_bam_window, 0, sizeof(g_bam_window));
    memset(g_rx_ring, 0, sizeof(g_rx_ring));
    memset(g_tx_ring, 0, sizeof(g_tx_ring));
    maps = unmaps = dma_allocs = dma_frees = 0;
    bam_inits = bam_terminates = bam_queues = 0;
    irq_requested = irq_enabled_on = 0;
    irq_requests = irq_enables = irq_frees = 0;
    irq_handler = NULL;
    irq_handler_ctx = NULL;
}

static void test_descriptor_without_hooks(void)
{
    const VqeafSlimNgdDesc *desc;
    const VioSlimNgdProvider *prov;

    reset_fakes();
    vio_slim_ngd_shutdown();

    assert(vio_slim_ngd_probe(NULL) == 0);
    desc = vio_slim_ngd_desc();
    prov = vio_slim_ngd_provider();
    assert(desc != NULL && prov != NULL);

    /* Every value is the sm6150.dtsi fact, not a measured one. */
    assert(strcmp(desc->compatible, "qcom,slim-ngd") == 0);
    assert(desc->ctrl_base == 0x62e40000ull && desc->ctrl_size == 0x2c000u);
    assert(desc->bam_base == 0x62e04000ull && desc->bam_size == 0x20000u);
    assert(desc->irq == 291u && desc->bam_irq == 292u);
    assert(desc->ngd_offset == 0x1000u);
    assert(vqeaf_slim_ngd_check_violet_dt(desc) == 0);

    assert(prov->irq == 291u && prov->bam_irq == 292u);
    assert(prov->dt_check == 0);
    /* No hooks: /dev/mem is opt-in, so nothing was mapped. */
    assert(prov->mapped == 0 && prov->bam_mapped == 0);
    assert(prov->mmio_error == -EPERM);
    assert(maps == 0);

    /* The bring-up must fail at the first missing primitive, never claim UP. */
    assert(vio_slim_ngd_start() == -ENOSYS);
    assert(vio_slim_ngd_client()->state == VQEAF_SLIM_NGD_FAILED);
    assert(vio_slim_ngd_provider()->last_error == -ENOSYS);

    vio_slim_ngd_shutdown();
    assert(vio_slim_ngd_provider() == NULL);
    assert(vio_slim_ngd_client() == NULL);
}

static void test_bringup_with_hooks(void)
{
    VioSlimNgdHooks hooks;
    VqeafSlimNgd *ngd;
    char report[1024];
    uint8_t msg[4] = {0x01, 0x03, 0x0C, 0x00};

    reset_fakes();
    vio_slim_ngd_shutdown();

    memset(&hooks, 0, sizeof(hooks));
    hooks.map = fake_map;
    hooks.unmap = fake_unmap;
    hooks.dma_alloc = fake_dma_alloc;
    hooks.dma_free = fake_dma_free;
    hooks.bam = &BAM_OPS;
    hooks.irq = &IRQ_OPS;

    /* The probe only reads: seed the version register before it runs. */
    w32(0u, 0x00020000u);
    assert(vio_slim_ngd_probe(&hooks) == 0);
    /* Both windows went through the hook with the DT addresses. */
    assert(maps == 2);
    assert(mapped_phys[0] == VQEAF_SLIM_NGD_CTRL_BASE);
    assert(mapped_phys[1] == VQEAF_SLIM_NGD_BAM_BASE);
    assert(vio_slim_ngd_provider()->mapped == 1);
    assert(vio_slim_ngd_provider()->bam_mapped == 1);
    assert(vio_slim_ngd_provider()->version == 0x0002u);

    assert(vio_slim_ngd_start() == 0);
    ngd = vio_slim_ngd_client();
    assert(ngd != NULL && ngd->state == VQEAF_SLIM_NGD_UP);
    assert(irq_requests == 1 && irq_requested == 291u);
    assert(irq_enabled_on == 291u);
    assert(irq_handler != NULL && irq_handler_ctx == ngd);
    assert(bam_inits == 2 && bam_queues == VQEAF_SLIM_NGD_DESC_NUM);
    assert(dma_allocs == 2);
    /* The client programmed INT_EN through the provider's MMIO ops. */
    assert(r32(VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_INT_EN) ==
           VQEAF_SLIM_NGD_INT_MASK);

    /* The controller IRQ entry point reaches the client: one received message. */
    w32(VQEAF_SLIM_NGD_REG_OFFSET + VQEAF_SLIM_NGD_INT_STAT,
        VQEAF_SLIM_NGD_INT_RX_MSG_RCVD);
    vio_slim_ngd_irq();
    assert(ngd->irq_count == 1);
    assert(ngd->rx_pending == 1);

    /* TX goes through the client with the message semantics intact. */
    assert(vqeaf_slim_ngd_xfer(ngd, msg, sizeof(msg)) == 0);
    assert(ngd->tx_busy == 1);
    assert(ngd->tx_tail == 1);

    vio_slim_ngd_report(report, (int)sizeof(report));
    assert(strstr(report, "irq=291") != NULL);
    assert(strstr(report, "bam=backend") != NULL);
    assert(strstr(report, "dma=backend") != NULL);
    assert(strstr(report, "ready=1") != NULL);

    vio_slim_ngd_shutdown();
    assert(unmaps >= 1);
    assert(irq_frees == 1);
    assert(bam_terminates == 2);
    assert(dma_frees == 2);
    assert(vio_slim_ngd_provider() == NULL);
}

static void test_not_ready_report_without_hooks(void)
{
    char report[1024];

    reset_fakes();
    vio_slim_ngd_shutdown();
    assert(vio_slim_ngd_probe(NULL) == 0);
    (void)vio_slim_ngd_start();
    vio_slim_ngd_report(report, (int)sizeof(report));
    assert(strstr(report, "ready=0") != NULL);
    assert(strstr(report, "bam=absent") != NULL);
    assert(strstr(report, "irq=291") != NULL);
    vio_slim_ngd_shutdown();
}

int main(void)
{
    test_descriptor_without_hooks();
    test_bringup_with_hooks();
    test_not_ready_report_without_hooks();
    printf("slim_ngd_bringup: 3 groups of provider checks passed\n");
    return 0;
}
