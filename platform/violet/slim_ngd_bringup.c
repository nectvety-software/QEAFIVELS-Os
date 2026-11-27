/* SPDX-License-Identifier: MIT */
/* platform/violet/slim_ngd_bringup.c - freestanding-reference Violet provider.
 *
 * Host/reference tests only; excluded from the Linux-core PID 1 build.
 *
 * Fills the injected VqeafSlimNgdDesc with the sm6150.dtsi numbers, maps the
 * controller and BAM windows, and hands DMA and GIC 291/292 to a hook. The
 * client's state machine stays in kernel/drivers/slimbus/slim_qcom_ngd.c; this
 * file contains only Violet facts and the resources the client cannot own.
 *
 * NOT measured on the handset: whether /dev/mem can reach 0x62e40000 while the
 * stock kernel owns the controller (it is refused by default here), the BAM
 * pipe numbering, and whether the btfmslim slave enumerates. Anything not
 * proven stays unset and is reported as such. */
#define _GNU_SOURCE

#include "slim_ngd_bringup.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The /dev/mem window is a POSIX path only. The host gate compiles this file
 * on Windows, where the hook map is the only way to get a window. */
#if defined(__unix__) || defined(__APPLE__)
#define VIO_HAVE_MMAP 1
#include <sys/mman.h>
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#define DEVMEM_PATH "/dev/mem"
#define DEVMEM_ENV  "VIO_SLIM_NGD_DEVMEM"

static VqeafSlimNgd g_client;
static VqeafSlimNgdDesc g_desc;
static VioSlimNgdProvider g_prov;
static VioSlimNgdHooks g_hooks;
static void *g_ctrl_map, *g_bam_map;
static uint64_t g_ctrl_size, g_bam_size;
static int g_probed;

/* ---- MMIO over the mapped controller window ---- */

static int window_ok(const void *map, uint64_t size, uint32_t offset)
{
    return map != NULL && offset <= size && size - offset >= 4u;
}

static int vio_read32(void *ctx, uint32_t offset, uint32_t *value)
{
    (void)ctx;
    if(value == NULL) return -EINVAL;
    if(!window_ok(g_ctrl_map, g_ctrl_size, offset)) return -ENOSYS;
    *value = *(volatile uint32_t *)((uint8_t *)g_ctrl_map + offset);
    return 0;
}

static int vio_write32(void *ctx, uint32_t offset, uint32_t value)
{
    (void)ctx;
    if(!window_ok(g_ctrl_map, g_ctrl_size, offset)) return -ENOSYS;
    *(volatile uint32_t *)((uint8_t *)g_ctrl_map + offset) = value;
    return 0;
}

static void vio_barrier(void *ctx)
{
    (void)ctx;
    __sync_synchronize();
}

/* ---- DMA and BAM: forwarded, never invented ---- */

static int vio_dma_alloc(void *ctx, size_t bytes, size_t align, void **cpu,
                         uint64_t *dma)
{
    (void)ctx;
    if(g_hooks.dma_alloc == NULL) return -ENOSYS;
    return g_hooks.dma_alloc(g_hooks.ctx, bytes, align, cpu, dma);
}

static void vio_dma_free(void *ctx, void *cpu, uint64_t dma, size_t bytes)
{
    (void)ctx;
    if(g_hooks.dma_free != NULL) g_hooks.dma_free(g_hooks.ctx, cpu, dma, bytes);
}

static int no_bam_pipe(void *ctx, vqeaf_slim_dir_t dir)
{
    (void)ctx; (void)dir;
    return -ENOSYS;
}

static int no_bam_queue(void *ctx, vqeaf_slim_dir_t dir, uint64_t dma,
                        uint32_t len)
{
    (void)ctx; (void)dir; (void)dma; (void)len;
    return -ENOSYS;
}

static int no_bam_issue(void *ctx, vqeaf_slim_dir_t dir)
{
    (void)ctx; (void)dir;
    return -ENOSYS;
}

/* Nothing was ever piped, so termination has nothing to undo. */
static int no_bam_terminate(void *ctx, vqeaf_slim_dir_t dir)
{
    (void)ctx; (void)dir;
    return 0;
}

static const VqeafSlimNgdBamOps DEFAULT_BAM = {
    NULL, no_bam_pipe, no_bam_queue, no_bam_issue, no_bam_terminate
};

static int no_irq_request(void *ctx, uint32_t irq, void (*handler)(void *),
                          void *handler_ctx)
{
    (void)ctx; (void)irq; (void)handler; (void)handler_ctx;
    return -ENOSYS;
}

static int no_irq_enable(void *ctx, uint32_t irq, int on)
{
    (void)ctx; (void)irq; (void)on;
    return -ENOSYS;
}

static void no_irq_free(void *ctx, uint32_t irq)
{
    (void)ctx; (void)irq;
}

static const VqeafSlimNgdIrqOps DEFAULT_IRQ = {
    NULL, no_irq_request, no_irq_enable, no_irq_free
};

/* ---- mapping ---- */

static int devmem_allowed(void)
{
    const char *v = getenv(DEVMEM_ENV);
    return v != NULL && v[0] == '1' && v[1] == '\0';
}

/* A hook map wins; /dev/mem is a deliberate opt-in because the stock kernel
 * owns this block and an access it does not expect can SIGBUS this PID 1. */
static void *map_window(uint64_t phys, uint64_t size, int *err)
{
    void *p;
    int fd;

    if(g_hooks.map != NULL) {
        p = g_hooks.map(g_hooks.ctx, phys, size);
        if(p == NULL) *err = -ENOSYS;
        return p;
    }
    if(!devmem_allowed()) {
        *err = -EPERM;
        return NULL;
    }
#ifdef VIO_HAVE_MMAP
    fd = open(DEVMEM_PATH, O_RDWR | O_SYNC | O_CLOEXEC);
    if(fd < 0) {
        *err = -errno;
        return NULL;
    }
    p = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
             (off_t)phys);
    if(p == MAP_FAILED) {
        *err = -errno;
        close(fd);
        return NULL;
    }
    close(fd);
    return p;
#else
    (void)fd;
    *err = -ENOSYS;
    return NULL;
#endif
}

static void unmap_window(void *map, uint64_t size)
{
    if(map == NULL) return;
    if(g_hooks.unmap != NULL) {
        g_hooks.unmap(g_hooks.ctx, map, size);
        return;
    }
#ifdef VIO_HAVE_MMAP
    if(devmem_allowed()) munmap(map, (size_t)size);
#else
    (void)size;
#endif
    /* A hook-mapped window without an unmap stays mapped: dropping the pointer
     * is honest, pretending to release it would not be. */
}

/* ---- live device tree ---- */

static void read_dt_compatible(void)
{
    char path[96];
    ssize_t n;
    int fd;

    g_prov.dt_compatible[0] = '\0';
    snprintf(path, sizeof(path), "%s/compatible", VIO_SLIM_NGD_DT_NODE);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0) return;
    n = read(fd, g_prov.dt_compatible, sizeof(g_prov.dt_compatible) - 1u);
    close(fd);
    if(n <= 0) {
        g_prov.dt_compatible[0] = '\0';
        return;
    }
    g_prov.dt_compatible[n] = '\0';
    while(n > 0 && (g_prov.dt_compatible[n - 1] == '\n' ||
                    g_prov.dt_compatible[n - 1] == '\r'))
        g_prov.dt_compatible[--n] = '\0';
    /* DT strings are NUL separated; the first one is the compatible. */
    g_prov.dt_node_present = 1;
}

/* ---- public API ---- */

int vio_slim_ngd_probe(const VioSlimNgdHooks *hooks)
{
    int err = 0;

    if(g_probed) return g_prov.dt_check;

    memset(&g_prov, 0, sizeof(g_prov));
    memset(&g_hooks, 0, sizeof(g_hooks));
    if(hooks != NULL) g_hooks = *hooks;
    g_prov.hooks_installed =
        (g_hooks.map != NULL || g_hooks.dma_alloc != NULL ||
         g_hooks.bam != NULL || g_hooks.irq != NULL);

    g_desc.compatible = VQEAF_SLIM_NGD_COMPATIBLE;
    g_desc.ctrl_base = VQEAF_SLIM_NGD_CTRL_BASE;
    g_desc.ctrl_size = VQEAF_SLIM_NGD_CTRL_SIZE;
    g_desc.bam_base = VQEAF_SLIM_NGD_BAM_BASE;
    g_desc.bam_size = VQEAF_SLIM_NGD_BAM_SIZE;
    g_desc.irq = VQEAF_SLIM_NGD_IRQ;
    g_desc.bam_irq = VQEAF_SLIM_NGD_BAM_IRQ;
    g_desc.ngd_offset = VQEAF_SLIM_NGD_REG_OFFSET;
    g_desc.mmio.ctx = NULL;
    g_desc.mmio.read32 = vio_read32;
    g_desc.mmio.write32 = vio_write32;
    g_desc.mmio.barrier = vio_barrier;
    g_desc.bam = (g_hooks.bam != NULL) ? *g_hooks.bam : DEFAULT_BAM;
    g_desc.irq_ops = (g_hooks.irq != NULL) ? *g_hooks.irq : DEFAULT_IRQ;
    g_desc.dma.ctx = NULL;
    g_desc.dma.alloc = vio_dma_alloc;
    g_desc.dma.release = vio_dma_free;

    g_prov.irq = g_desc.irq;
    g_prov.bam_irq = g_desc.bam_irq;

    read_dt_compatible();
    g_prov.dt_match = g_prov.dt_node_present &&
                      strcmp(g_prov.dt_compatible, VQEAF_SLIM_NGD_COMPATIBLE) == 0;
    g_prov.dt_check = vqeaf_slim_ngd_check_violet_dt(&g_desc);

    g_ctrl_size = g_desc.ctrl_size;
    g_bam_size = g_desc.bam_size;
    g_ctrl_map = map_window(g_desc.ctrl_base, g_ctrl_size, &err);
    g_prov.mmio_error = g_ctrl_map != NULL ? 0 : err;
    g_prov.mapped = g_ctrl_map != NULL;

    if(g_prov.mapped) {
        uint32_t value = 0;
        /* Two read-only peeks: the controller version and the NGD status. No
         * register is written by the probe. */
        if(g_desc.mmio.read32(NULL, 0u, &value) == 0) g_prov.version = value >> 16;
        (void)g_desc.mmio.read32(NULL,
                                 g_desc.ngd_offset + VQEAF_SLIM_NGD_STATUS,
                                 &g_prov.status);
    }
    g_bam_map = map_window(g_desc.bam_base, g_bam_size, &err);
    g_prov.bam_mapped = g_bam_map != NULL;

    g_probed = 1;
    g_prov.last_error = g_prov.dt_check;
    return g_prov.dt_check;
}

const VqeafSlimNgdDesc *vio_slim_ngd_desc(void)
{
    return g_probed ? &g_desc : NULL;
}

const VioSlimNgdProvider *vio_slim_ngd_provider(void)
{
    return g_probed ? &g_prov : NULL;
}

VqeafSlimNgd *vio_slim_ngd_client(void)
{
    return g_probed ? &g_client : NULL;
}

int vio_slim_ngd_start(void)
{
    int rc;

    if(!g_probed) {
        rc = vio_slim_ngd_probe(NULL);
        if(rc != 0) return rc;
    }
    if(g_client.abi == VQEAF_SLIM_NGD_ABI &&
       g_client.state == VQEAF_SLIM_NGD_UP)
        return -EBUSY;

    rc = vqeaf_slim_ngd_init(&g_client, &g_desc);
    if(rc != 0) {
        g_prov.last_error = rc;
        return rc;
    }
    g_prov.start_attempted = 1;
    rc = vqeaf_slim_ngd_start(&g_client);
    g_prov.last_error = rc;
    return rc;
}

void vio_slim_ngd_shutdown(void)
{
    if(g_client.abi == VQEAF_SLIM_NGD_ABI) vqeaf_slim_ngd_stop(&g_client);
    unmap_window(g_bam_map, g_bam_size);
    unmap_window(g_ctrl_map, g_ctrl_size);
    g_bam_map = NULL;
    g_ctrl_map = NULL;
    g_bam_size = 0;
    g_ctrl_size = 0;
    memset(&g_client, 0, sizeof(g_client));
    memset(&g_desc, 0, sizeof(g_desc));
    memset(&g_prov, 0, sizeof(g_prov));
    memset(&g_hooks, 0, sizeof(g_hooks));
    g_probed = 0;
}

void vio_slim_ngd_irq(void)
{
    if(g_client.abi == VQEAF_SLIM_NGD_ABI &&
       g_client.state == VQEAF_SLIM_NGD_UP)
        vqeaf_slim_ngd_irq(&g_client);
}

/* ---- report ---- */

static const char *mmio_reason(int err)
{
    switch(err) {
    case 0: return "mapped";
    case -EPERM: return "disabled (stock kernel owns the block; set "
                        "VIO_SLIM_NGD_DEVMEM=1 to try /dev/mem)";
    case -ENOSYS: return "no mapping hook";
    case -ENODEV: return "no such device";
    case -EACCES: return "permission denied";
    case -EINVAL: return "invalid argument";
    case -ENOMEM: return "out of memory";
    }
    return "failed";
}

static int ready(const VioSlimNgdProvider *p)
{
    return p->mapped && g_hooks.bam != NULL && g_hooks.irq != NULL &&
           g_hooks.dma_alloc != NULL &&
           g_client.abi == VQEAF_SLIM_NGD_ABI &&
           g_client.state == VQEAF_SLIM_NGD_UP;
}

void vio_slim_ngd_report(char *out, int cap)
{
    char line[224];
    size_t used;

    if(out == NULL || cap <= 0) return;
    out[0] = '\0';

    if(!g_probed) {
        snprintf(line, sizeof(line),
                 "slim: not probed (no descriptor; run `slim` again)\n");
        snprintf(out, (size_t)cap, "%s", line);
        return;
    }

#define APPEND(...)                                                            \
    do {                                                                       \
        used = strlen(out);                                                    \
        if(used + 1u < (size_t)cap)                                            \
            snprintf(out + used, (size_t)cap - used, __VA_ARGS__);              \
    } while(0)

    APPEND("slim: node=%s compatible=%s\n", VIO_SLIM_NGD_DT_NODE,
           g_prov.dt_node_present ? g_prov.dt_compatible : "absent");
    APPEND("slim: dt=%s base=0x%llx+0x%x bam=0x%llx+0x%x irq=%u bam-irq=%u "
           "ngd-off=0x%x\n",
           g_prov.dt_check == 0 ? "ok" : "failed",
           (unsigned long long)g_desc.ctrl_base, (unsigned)g_desc.ctrl_size,
           (unsigned long long)g_desc.bam_base, (unsigned)g_desc.bam_size,
           g_prov.irq, g_prov.bam_irq, g_desc.ngd_offset);
    APPEND("slim: mmio=%s version=0x%04x status=0x%08x bam-window=%s\n",
           mmio_reason(g_prov.mmio_error), g_prov.version, g_prov.status,
           g_prov.bam_mapped ? "mapped" : "unmapped");
    APPEND("slim: bam=%s dma=%s irq=%s hooks=%d\n",
           g_hooks.bam != NULL ? "backend" : "absent",
           g_hooks.dma_alloc != NULL ? "backend" : "absent",
           g_hooks.irq != NULL ? "backend" : "absent",
           g_prov.hooks_installed);
    APPEND("slim: client=%s irq=%u rx=%u tx-done=%u errors=%u last=%s\n",
           g_client.abi == VQEAF_SLIM_NGD_ABI
               ? vqeaf_slim_ngd_state_name(g_client.state) : "not-initialised",
           g_client.irq_count, g_client.rx_count, g_client.tx_done_count,
           g_client.error_count, vqeaf_slim_ngd_error_name(g_prov.last_error));
    APPEND("slim: ready=%d (ready needs mapped mmio + bam + dma + irq backend "
           "and a controller that stayed up)\n", ready(&g_prov));
#undef APPEND
}
