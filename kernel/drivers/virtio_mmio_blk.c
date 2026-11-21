/* QEMU virt AArch64 VirtIO-MMIO block driver -- modern (v2) + legacy (v1).
 *
 * The virt machine exposes up to 32 virtio-mmio transports at
 * 0x0a000000 + n*0x200 (DeviceID 2 = block). This driver scans for the first
 * block transport, maps it as device memory, negotiates features, brings up one
 * virtqueue and performs synchronous sector I/O by polling the used ring with a
 * bounded timeout. The sync path is honest: it blocks until the device posts a
 * used element, it does not pretend to be interrupt-driven.
 *
 * Driver model (v0.2): registered with a probe/remove pair; the capability
 * registry only sees READY after DRIVER_OK + a non-zero capacity AND a scratch
 * sector write/read self-test that proves the data path end to end.
 *
 * EMULATED device (QEMU virt). No physical violet peripheral is touched. */
#include <stdint.h>
#include <stddef.h>
#include "vqeaf/virtio_mmio_blk.h"
#include "vqeaf/uart_aarch64.h"
#include "vqeaf/memory.h"
#include "vqeaf/vm.h"
#include "../../system/core/include/vqeaf/driver.h"
#include "../../system/core/include/vqeaf/capability.h"
#include "../../system/core/include/vqeaf/block.h"

#define VMMIO_REGION_BASE 0x0a000000ull
#define VMMIO_STRIDE      0x200ull
#define VMMIO_TRANSPORTS  32u
#define VMMIO_MAGIC       0x74726976u
#define VMMIO_DEV_BLOCK   2u

/* Common registers */
#define R_MAGIC           0x000u
#define R_VERSION         0x004u
#define R_DEVICE_ID       0x008u
/* Modern (v2) */
#define M_DEV_FEATURES    0x010u
#define M_DEV_FEATURES_SEL 0x014u
#define M_DRV_FEATURES    0x020u
#define M_DRV_FEATURES_SEL 0x024u
#define M_QUEUE_SEL       0x030u
#define M_QUEUE_NUM_MAX   0x034u
#define M_QUEUE_NUM       0x038u
#define M_QUEUE_READY     0x044u
#define M_QUEUE_NOTIFY    0x050u
#define M_INTERRUPT_STATUS 0x060u
#define M_INTERRUPT_ACK   0x064u
#define M_STATUS          0x070u
#define M_QUEUE_DESC_LO   0x080u
#define M_QUEUE_DESC_HI   0x084u
#define M_QUEUE_AVAIL_LO  0x090u
#define M_QUEUE_AVAIL_HI  0x094u
#define M_QUEUE_USED_LO   0x0a0u
#define M_QUEUE_USED_HI   0x0a4u
#define M_CONFIG_GEN      0x0fcu
/* Legacy (v1) on QEMU virt. QEMU keeps the SAME register offsets in legacy
 * mode (include/standard-headers/linux/virtio_mmio.h): what changes is that
 * GuestPageSize/QueueAlign/QueuePFN replace the descriptor/avail/used address
 * registers, and feature negotiation uses the 0x010/0x020 pair instead of the
 * modern 0x010/0x014 + 0x020/0x024 selection window. The virtio 1.0 spec's
 * "legacy MMIO" diagram (GuestPageSize at 0x020) is a different, older
 * layout that QEMU does not implement; reading 0x020 there yields 0 as
 * QueueNumMax and the queue never comes up. */
#define L_HOST_FEATURES   0x010u
#define L_HOST_FEATURES_SEL 0x014u
#define L_GUEST_FEATURES  0x020u
#define L_GUEST_FEATURES_SEL 0x024u
#define L_GUEST_PAGE_SIZE 0x028u
#define L_QUEUE_SEL       0x030u
#define L_QUEUE_NUM_MAX   0x034u
#define L_QUEUE_NUM       0x038u
#define L_QUEUE_ALIGN     0x03cu
#define L_QUEUE_PFN       0x040u
#define L_QUEUE_NOTIFY    0x050u
#define L_INTERRUPT_STATUS 0x060u
#define L_INTERRUPT_ACK   0x064u
#define L_STATUS          0x070u
/* Config space (both): block capacity is the first 8 bytes. */
#define R_CONFIG          0x100u

#define ST_ACKNOWLEDGE 1u
#define ST_DRIVER      2u
#define ST_DRIVER_OK   4u
#define ST_FEATURES_OK 8u
#define ST_FAILED      128u

#define BLK_T_IN   0u
#define BLK_T_OUT  1u
#define BLK_S_OK   0u

#define QMAX       256u
#define QUEUE_PAGES 4u
#define POLL_LIMIT 20000000u

struct vring_desc { uint64_t addr; uint32_t len; uint16_t flags, next; };
struct vring_avail { uint16_t flags, idx; uint16_t ring[]; };
struct vring_used_elem { uint32_t id, len; };
struct vring_used { uint16_t flags, idx; struct vring_used_elem ring[]; };
struct blk_req { uint32_t type, ioprio; uint64_t sector; };

#define DESC_F_NEXT  0x1u
#define DESC_F_WRITE 0x2u

static uint64_t mmio_base;
static uint32_t mmio_version;
static uint64_t queue_phys, req_phys;
static struct vring_desc *desc;
static struct vring_avail *avail;
static struct vring_used *used;
static uint16_t qnum, used_cons, avail_idx;
static uint64_t capacity;
static int ready;
static uint32_t block_id, instance_id;
static struct VqeafBlockDevice block_dev;

static inline uint32_t rd(uint32_t off)
{
    return *(volatile uint32_t *)(uintptr_t)(mmio_base + off);
}
static inline void wr(uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(uintptr_t)(mmio_base + off) = v;
}
static inline void barrier(void) { __asm__ volatile("dsb sy" ::: "memory"); }

static uint32_t status_reg(void)
{
    return mmio_version >= 2 ? M_STATUS : L_STATUS;
}
static uint32_t notify_reg(void)
{
    return mmio_version >= 2 ? M_QUEUE_NOTIFY : L_QUEUE_NOTIFY;
}

/* Synchronous single-request submission. Only one request is in flight at a
 * time, so the head descriptor is always 0 and the used consumer advances by
 * one. Returns 0 on BLK_S_OK, negative on error/timeout. */
static int submit(uint32_t type, uint64_t lba, uint32_t sectors, void *buffer)
{
    if (!ready || !buffer) return -1;
    if (sectors == 0 || sectors > VQEAF_VMMIO_MAX_SECTORS) return -2;
    if (lba + sectors > capacity) return -3;

    struct blk_req *r = (struct blk_req *)(uintptr_t)req_phys;
    volatile uint8_t *status = (volatile uint8_t *)(uintptr_t)(req_phys + 16u);
    r->type = type; r->ioprio = 0; r->sector = lba;
    *status = 0xFFu;

    uint64_t buf_pa = (uint64_t)(uintptr_t)buffer; /* identity-mapped kernel RAM */
    desc[0].addr = req_phys; desc[0].len = sizeof(struct blk_req);
    desc[0].flags = DESC_F_NEXT; desc[0].next = 1;
    desc[1].addr = buf_pa; desc[1].len = sectors * VQEAF_VMMIO_SECTOR_SIZE;
    desc[1].flags = DESC_F_NEXT | (type == BLK_T_IN ? DESC_F_WRITE : 0u);
    desc[1].next = 2;
    desc[2].addr = req_phys + 16u; desc[2].len = 1;
    desc[2].flags = DESC_F_WRITE; desc[2].next = 0;

    avail->ring[avail_idx % qnum] = 0;
    barrier();
    avail->idx = (uint16_t)(avail_idx + 1u);
    avail_idx++;
    barrier();
    wr(notify_reg(), 0u);

    uint32_t spins = 0;
    while ((uint16_t)(used->idx - used_cons) == 0) {
        if (++spins > POLL_LIMIT) return -4; /* device timeout */
        /* WFI, not a hot spin: QEMU virt runs this vCPU in the main thread, and
         * a request that never leaves guest code starves the I/O completion
         * bottom half, so the used ring would never advance. WFI parks the PE
         * long enough for the transport to finish and signal, exactly like a
         * real driver waiting on the completion IRQ. */
        __asm__ volatile("wfi" ::: "memory");
    }
    barrier();
    used_cons++;
    uint8_t st = *status;
    if (st == BLK_S_OK) return 0;
    return st == 1u ? -5 : -6; /* IOERR / UNSUPP */
}

int vqeaf_virtio_mmio_blk_read_sync(uint64_t lba, uint32_t sectors, void *buffer)
{
    return submit(BLK_T_IN, lba, sectors, buffer);
}
int vqeaf_virtio_mmio_blk_write_sync(uint64_t lba, uint32_t sectors, const void *buffer)
{
    return submit(BLK_T_OUT, lba, sectors, (void *)buffer);
}

/* Write a known pattern to the last sector, read it back into a different
 * buffer and compare. Proves the write AND read data paths, not just probe. */
static uint8_t scratch_w[512] __attribute__((aligned(16)));
static uint8_t scratch_r[512] __attribute__((aligned(16)));
int vqeaf_virtio_mmio_blk_self_test(void)
{
    if (!ready || capacity == 0) return -1;
    uint64_t lba = capacity - 1u;
    for (uint32_t i = 0; i < 512u; i++) scratch_w[i] = (uint8_t)(i * 7u + 13u);
    for (uint32_t i = 0; i < 512u; i++) scratch_r[i] = 0;
    int w = submit(BLK_T_OUT, lba, 1, scratch_w);
    if (w != 0) return -2;
    int r = submit(BLK_T_IN, lba, 1, scratch_r);
    if (r != 0) return -3;
    for (uint32_t i = 0; i < 512u; i++)
        if (scratch_r[i] != scratch_w[i]) return -4;
    return 0;
}

static int setup_queue(void)
{
    uint32_t max;
    if (mmio_version >= 2) {
        wr(M_QUEUE_SEL, 0u);
        max = rd(M_QUEUE_NUM_MAX);
    } else {
        wr(L_QUEUE_SEL, 0u);
        max = rd(L_QUEUE_NUM_MAX);
    }
    if (max == 0) return -1;
    qnum = (uint16_t)(max < QMAX ? max : QMAX);
    if (qnum < 4) return -2;

    queue_phys = vqeaf_pmm_alloc_pages(QUEUE_PAGES);
    req_phys = vqeaf_pmm_alloc_pages(1u);
    if (!queue_phys || !req_phys) return -3;

    desc = (struct vring_desc *)(uintptr_t)queue_phys;
    uint64_t avail_off = (uint64_t)qnum * 16u;
    avail = (struct vring_avail *)(uintptr_t)(queue_phys + avail_off);
    uint64_t avail_end_off = avail_off + 4u + 2u * qnum;
    uint64_t used_off = (avail_end_off + 4095ull) & ~4095ull;
    used = (struct vring_used *)(uintptr_t)(queue_phys + used_off);
    for (uint64_t i = 0; i < QUEUE_PAGES * 4096ull; i++)
        ((volatile uint8_t *)queue_phys)[i] = 0;
    used_cons = 0; avail_idx = 0;

    if (mmio_version >= 2) {
        wr(M_QUEUE_NUM, qnum);
        wr(M_QUEUE_DESC_LO, (uint32_t)queue_phys);
        wr(M_QUEUE_DESC_HI, (uint32_t)(queue_phys >> 32));
        uint64_t avail_pa = (uint64_t)(uintptr_t)avail;
        wr(M_QUEUE_AVAIL_LO, (uint32_t)avail_pa);
        wr(M_QUEUE_AVAIL_HI, (uint32_t)(avail_pa >> 32));
        uint64_t used_pa = (uint64_t)(uintptr_t)used;
        wr(M_QUEUE_USED_LO, (uint32_t)used_pa);
        wr(M_QUEUE_USED_HI, (uint32_t)(used_pa >> 32));
        barrier();
        wr(M_QUEUE_READY, 1u);
    } else {
        /* Legacy PFN transport: the queue must be selected before it is
         * programmed, and GuestPageSize must be non-zero before QEMU accepts
         * the PFN. Order matters: sel, page size, num, align, pfn. */
        wr(L_QUEUE_SEL, 0u);
        wr(L_GUEST_PAGE_SIZE, (uint32_t)VQEAF_PAGE_SIZE);
        wr(L_QUEUE_NUM, qnum);
        wr(L_QUEUE_ALIGN, (uint32_t)VQEAF_PAGE_SIZE);
        wr(L_QUEUE_PFN, (uint32_t)(queue_phys / VQEAF_PAGE_SIZE));
    }
    return 0;
}

static int negotiate_features(void)
{
    if (mmio_version >= 2) {
        /* Acknowledge only VIRTIO_F_VERSION_1 (bit 32); basic single-segment
         * block I/O needs no optional feature. */
        wr(M_DEV_FEATURES_SEL, 0u); (void)rd(M_DEV_FEATURES);
        wr(M_DEV_FEATURES_SEL, 1u); (void)rd(M_DEV_FEATURES);
        wr(M_DRV_FEATURES_SEL, 0u); wr(M_DRV_FEATURES, 0u);
        wr(M_DRV_FEATURES_SEL, 1u); wr(M_DRV_FEATURES, 1u); /* VERSION_1 */
        uint32_t st = rd(M_STATUS);
        wr(M_STATUS, st | ST_FEATURES_OK);
        if (!(rd(M_STATUS) & ST_FEATURES_OK)) return -1;
    } else {
        wr(L_HOST_FEATURES_SEL, 0u); (void)rd(L_HOST_FEATURES);
        wr(L_GUEST_FEATURES_SEL, 0u); wr(L_GUEST_FEATURES, 0u);
    }
    return 0;
}

static uint64_t read_capacity(void)
{
    if (mmio_version >= 2) {
        uint32_t g1, g2;
        uint64_t lo, hi;
        do {
            g1 = rd(M_CONFIG_GEN);
            lo = rd(R_CONFIG);
            hi = rd(R_CONFIG + 4u);
            g2 = rd(M_CONFIG_GEN);
        } while (g1 != g2);
        return lo | (hi << 32);
    }
    uint64_t lo = rd(R_CONFIG);
    uint64_t hi = rd(R_CONFIG + 4u);
    return lo | (hi << 32);
}

/* v0.2 probe: device argument is the transport MMIO base. */
static int mmio_blk_probe(void *device)
{
    mmio_base = (uint64_t)(uintptr_t)device;
    mmio_version = rd(R_VERSION);
    if (mmio_version != 1u && mmio_version != 2u) return -1;
    if (rd(R_DEVICE_ID) != VMMIO_DEV_BLOCK) return -2;

    /* Do not second-guess the Version register: QEMU reports 1 for legacy and
     * 2 for modern, and both use the same offset table (see the L_* map). A
     * heuristic here would send a real legacy device down the modern path and
     * fail its queue setup. */
    wr(status_reg(), 0u);                 /* reset */
    uint32_t spins = 0;
    while (rd(status_reg()) != 0u) { if (++spins > POLL_LIMIT) return -3; }
    wr(status_reg(), ST_ACKNOWLEDGE | ST_DRIVER);

    if (negotiate_features() != 0) { wr(status_reg(), ST_FAILED); return -4; }
    if (setup_queue() != 0) { wr(status_reg(), ST_FAILED); return -5; }

    capacity = read_capacity();
    if (capacity == 0) { wr(status_reg(), ST_FAILED); return -6; }

    wr(status_reg(), rd(status_reg()) | ST_DRIVER_OK);
    barrier();
    ready = 1;

    /* READY only after the data path is proven, not merely DRIVER_OK. */
    if (vqeaf_virtio_mmio_blk_self_test() != 0) {
        ready = 0;
        wr(status_reg(), ST_FAILED);
        return -7;
    }
    return 0;
}

static int mmio_blk_remove(void *device)
{
    (void)device;
    if (mmio_base) wr(status_reg(), 0u);
    ready = 0;
    if (queue_phys) { vqeaf_pmm_free_page(queue_phys); queue_phys = 0; }
    if (req_phys) { vqeaf_pmm_free_page(req_phys); req_phys = 0; }
    return 0;
}

static const VqeafDriver mmio_blk_driver = {
    .abi = VQEAF_DRIVER_ABI,
    .size = sizeof(VqeafDriver),
    .name = "virtio-blk-mmio",
    .device_class = VQEAF_DEV_STORAGE,
    .probe = mmio_blk_probe,
    .remove = mmio_blk_remove,
};

static int block_read_cb(struct VqeafBlockDevice *dev, uint64_t lba, uint32_t count, void *out)
{
    (void)dev;
    return vqeaf_virtio_mmio_blk_read_sync(lba, count, out);
}
static int block_write_cb(struct VqeafBlockDevice *dev, uint64_t lba, uint32_t count, const void *in)
{
    (void)dev;
    return vqeaf_virtio_mmio_blk_write_sync(lba, count, in);
}

int vqeaf_virtio_mmio_blk_init(void)
{
    /* Map the whole virtio-mmio transport region as device memory. */
    if (vqeaf_vm_map_range_or_update(vqeaf_vm_current_root(),
            VMMIO_REGION_BASE, VMMIO_REGION_BASE,
            (VMMIO_STRIDE * VMMIO_TRANSPORTS) / VQEAF_PAGE_SIZE,
            VQEAF_VM_WRITE | VQEAF_VM_CACHE_DISABLE) != 0) {
        vqeaf_uart_write("VMMIO: region map FAILED\r\n");
        return -1;
    }

    uint64_t found = 0;
    for (uint32_t n = 0; n < VMMIO_TRANSPORTS; n++) {
        uint64_t base = VMMIO_REGION_BASE + (uint64_t)n * VMMIO_STRIDE;
        mmio_base = base;
        if (rd(R_MAGIC) == VMMIO_MAGIC && rd(R_DEVICE_ID) == VMMIO_DEV_BLOCK) {
            found = base;
            break;
        }
    }
    if (!found) {
        vqeaf_uart_write("VMMIO: no block transport present\r\n");
        return -2;
    }

    if (vqeaf_driver_register(&mmio_blk_driver) != 0) {
        vqeaf_uart_write("VMMIO: driver register FAILED\r\n");
        return -3;
    }
    uint32_t inst = 0;
    int pr = vqeaf_driver_bind("virtio-blk-mmio", (void *)(uintptr_t)found, &inst);
    if (pr != 0) {
        vqeaf_uart_write("VMMIO: probe FAILED rc=");
        vqeaf_uart_u64((uint64_t)(int64_t)pr);
        vqeaf_uart_write("\r\n");
        return -4;
    }
    instance_id = inst;

    block_dev.name = "vmmio0";
    block_dev.sector_size = VQEAF_VMMIO_SECTOR_SIZE;
    block_dev.sector_count = capacity;
    block_dev.read_sectors = block_read_cb;
    block_dev.write_sectors = block_write_cb;
    block_dev.ctx = 0;
    int id = vqeaf_block_register(&block_dev);
    if (id < 0) { vqeaf_uart_write("VMMIO: block register FAILED\r\n"); return -5; }
    block_id = (uint32_t)id;

    vqeaf_cap_set("storage.virtio_mmio", VQEAF_CAP_READY, inst, (int32_t)capacity);
    vqeaf_cap_set("storage.block", VQEAF_CAP_READY, inst, (int32_t)block_id);

    vqeaf_uart_write("VMMIO: block transport base=");
    vqeaf_uart_hex64(found);
    vqeaf_uart_write(" version=");
    vqeaf_uart_u64(mmio_version);
    vqeaf_uart_write(" capacity=");
    vqeaf_uart_u64(capacity);
    vqeaf_uart_write(" sectors queue=");
    vqeaf_uart_u64(qnum);
    vqeaf_uart_write("\r\n");
    vqeaf_uart_write("VMMIO: probe READY instance=");
    vqeaf_uart_u64(instance_id);
    vqeaf_uart_write(" block id=");
    vqeaf_uart_u64(block_id);
    vqeaf_uart_write("\r\n");
    vqeaf_uart_write("VMMIO: scratch sector write/read self-test PASS\r\n");
    return 0;
}

int vqeaf_virtio_mmio_blk_ready(void) { return ready; }
uint64_t vqeaf_virtio_mmio_blk_capacity(void) { return capacity; }
uint32_t vqeaf_virtio_mmio_blk_dev_id(void) { return block_id; }
uint32_t vqeaf_virtio_mmio_blk_instance(void) { return instance_id; }
uint64_t vqeaf_virtio_mmio_blk_base(void) { return mmio_base; }
uint32_t vqeaf_virtio_mmio_blk_version(void) { return mmio_version; }