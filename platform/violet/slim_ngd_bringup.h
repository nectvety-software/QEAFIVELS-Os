/* SPDX-License-Identifier: MIT */
/* platform/violet/slim_ngd_bringup.h - freestanding-reference Violet provider.
 *
 * This MMIO-capable provider is retained for host/reference tests only. It is
 * excluded from the Linux-core PID 1 build, where the kernel owns SLIMbus.
 *
 * The portable client is kernel/drivers/slimbus/slim_qcom_ngd.c and takes an
 * injected VqeafSlimNgdDesc. This file is the Violet half: it fills that
 * descriptor with the values in sm6150.dtsi, maps 0x62e40000 (controller) and
 * 0x62e04000 (BAM), and forwards the two hardware primitives this PID 1
 * cannot own - DMA rings and GIC interrupts 291/292 - to a hook supplied by
 * whoever does own them (the freestanding kernel).
 *
 * Fail-closed by construction:
 *   - MMIO stays unmapped unless a hook maps it or /dev/mem is explicitly
 *     allowed through VIO_SLIM_NGD_DEVMEM=1; the stock kernel owns the block
 *     and a blind userspace access could SIGBUS.
 *   - Without a BAM backend, DMA backend or IRQ backend the client is started
 *     anyway and fails at the first missing primitive, so the report shows the
 *     real blocking step instead of a hardcoded "blocked".
 *   - Nothing here is called "ready" without ngd state UP and all three
 *     backends present. */
#ifndef VIO_SLIM_NGD_BRINGUP_H
#define VIO_SLIM_NGD_BRINGUP_H

#include <stddef.h>
#include <stdint.h>

#include "vqeaf/slim_qcom_ngd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Live device-tree node for slim_qca in the board source. */
#define VIO_SLIM_NGD_DT_NODE "/proc/device-tree/soc/slim@62e40000"

/* Hardware primitives the provider cannot implement itself. Every member is
 * optional; a NULL member keeps the documented default (unsupported). `bam`
 * and `irq` are whole backends because the client calls them per direction. */
typedef struct {
    void *ctx;
    void *(*map)(void *ctx, uint64_t phys, uint64_t size);
    void  (*unmap)(void *ctx, void *addr, uint64_t size);
    int   (*dma_alloc)(void *ctx, size_t bytes, size_t align, void **cpu,
                       uint64_t *dma);
    void  (*dma_free)(void *ctx, void *cpu, uint64_t dma, size_t bytes);
    const VqeafSlimNgdBamOps *bam;
    const VqeafSlimNgdIrqOps *irq;
} VioSlimNgdHooks;

/* What the provider can prove about this handset right now. */
typedef struct {
    int      dt_node_present;         /* /proc/device-tree node was read */
    char     dt_compatible[64];       /* its compatible property, verbatim */
    int      dt_match;                /* equals "qcom,slim-ngd" */
    int      dt_check;                /* vqeaf_slim_ngd_check_violet_dt() */
    int      mmio_error;              /* why the controller window is unmapped */
    int      mapped;                  /* controller window mapped */
    int      bam_mapped;              /* BAM window mapped */
    uint32_t version;                 /* 16 MSBs of controller register 0 */
    uint32_t status;                  /* NGD_STATUS as read, 0 when unmapped */
    uint32_t irq, bam_irq;            /* 291 / 292 from sm6150.dtsi */
    int      hooks_installed;
    int      start_attempted;
    int      last_error;
} VioSlimNgdProvider;

/* Fill the descriptor from sm6150.dtsi, read the live DT compatible, map the
 * two windows and read the NGD version/status. Idempotent: the first call
 * wins, later calls return its result and install no new hooks. Returns the
 * result of the descriptor check (0 only for the exact violet numbers). */
int vio_slim_ngd_probe(const VioSlimNgdHooks *hooks);

const VqeafSlimNgdDesc *vio_slim_ngd_desc(void);
const VioSlimNgdProvider *vio_slim_ngd_provider(void);

/* The one client instance owned by this platform layer, so the Bluetooth
 * bridge and the `slim` verb report the same object. NULL before a successful
 * probe. */
VqeafSlimNgd *vio_slim_ngd_client(void);

/* init + start; 0 only when the client reports UP. On failure last_error
 * carries the first missing primitive (typically -ENOSYS for BAM/DMA/IRQ). */
int vio_slim_ngd_start(void);

/* stop, unmap both windows, forget the hooks and the probe result. */
void vio_slim_ngd_shutdown(void);

/* Interrupt entry point for a backend that routes the controller IRQ here
 * instead of carrying the client pointer itself. */
void vio_slim_ngd_irq(void);

/* Multi-line, read-only status for the `slim` verb. */
void vio_slim_ngd_report(char *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* VIO_SLIM_NGD_BRINGUP_H */
