/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "vqeaf/wcn3990_ce.h"

#define TOUCH_INTR 0x01u
#define TOUCH_SRC  0x02u
#define TOUCH_DST  0x04u
#define TOUCH_CTRL 0x08u

static int attached(const VqeafWcn3990Ce *ce)
{
    return ce && ce->abi == VQEAF_WCN3990_CE_ABI &&
           ce->size == sizeof(*ce) && ce->window_size != 0 &&
           ce->mmio.read32 && ce->mmio.write32 && ce->mmio.barrier;
}

static int valid_id(uint8_t id)
{
    return id < VQEAF_WCN3990_CE_MAX;
}

static uint32_t ce_base(uint8_t id)
{
    return VQEAF_WCN3990_CE0_BASE + (uint32_t)id * VQEAF_WCN3990_CE_STRIDE;
}

static void fence(VqeafWcn3990Ce *ce)
{
    ce->mmio.barrier(ce->mmio.ctx);
}

static int read_reg(VqeafWcn3990Ce *ce, uint32_t offset, uint32_t *value)
{
    int rc;
    if(offset > ce->window_size || ce->window_size - offset < 4u)
        return -ERANGE;
    fence(ce);
    rc = ce->mmio.read32(ce->mmio.ctx, offset, value);
    fence(ce);
    return rc;
}

static int write_reg(VqeafWcn3990Ce *ce, uint8_t id, uint32_t offset,
                     uint32_t value, uint8_t touch)
{
    int rc;
    if(offset > ce->window_size || ce->window_size - offset < 4u)
        return -ERANGE;
    if(touch) ce->_touched[id] = (uint8_t)(ce->_touched[id] | touch);
    fence(ce);
    rc = ce->mmio.write32(ce->mmio.ctx, offset, value);
    fence(ce);
    return rc;
}

static int modify_reg(VqeafWcn3990Ce *ce, uint8_t id, uint32_t offset,
                      uint32_t mask, uint32_t value, uint8_t touch)
{
    uint32_t old;
    int rc = read_reg(ce, offset, &old);
    if(rc) return rc;
    return write_reg(ce, id, offset, (old & ~mask) | (value & mask), touch);
}

static int interrupt_disable(VqeafWcn3990Ce *ce, uint8_t id)
{
    uint32_t base = ce_base(id);
    int rc = modify_reg(ce, id, base + VQEAF_WCN3990_CE_HOST_INTR_ENABLE,
                        VQEAF_WCN3990_CE_INTR_HOST_MASK, 0, TOUCH_INTR);
    if(rc) return rc;
    return modify_reg(ce, id, base + VQEAF_WCN3990_CE_MISC_INTR_ENABLE,
                      VQEAF_WCN3990_CE_INTR_ERROR_MASK, 0, TOUCH_INTR);
}

static int try_modify(VqeafWcn3990Ce *ce, uint8_t id, uint32_t offset,
                      uint32_t mask, uint32_t value)
{
    return modify_reg(ce, id, offset, mask, value, 0);
}

static int try_write(VqeafWcn3990Ce *ce, uint32_t offset, uint32_t value)
{
    return write_reg(ce, 0, offset, value, 0);
}

static void retain_error(int *first, int rc)
{
    if(rc && !*first) *first = rc;
}

static void zero_rri(volatile uint32_t *rri)
{
    uint32_t i;
    for(i = 0; i < VQEAF_WCN3990_CE_MAX; i++) rri[i] = 0;
}

static int deinit(VqeafWcn3990Ce *ce, uint8_t id, uint8_t touched)
{
    uint32_t base = ce_base(id);
    uint16_t bit = (uint16_t)(1u << id);
    int first = 0;
    if(touched & TOUCH_INTR) {
        retain_error(&first, try_modify(ce, id,
                   base + VQEAF_WCN3990_CE_HOST_INTR_ENABLE,
                   VQEAF_WCN3990_CE_INTR_HOST_MASK, 0));
        retain_error(&first, try_modify(ce, id,
                   base + VQEAF_WCN3990_CE_MISC_INTR_ENABLE,
                   VQEAF_WCN3990_CE_INTR_ERROR_MASK, 0));
    }
    if(touched & TOUCH_SRC) {
        retain_error(&first, try_write(ce,
                   base + VQEAF_WCN3990_CE_SRC_BASE, 0));
        retain_error(&first, try_write(ce,
                   base + VQEAF_WCN3990_CE_SRC_SIZE, 0));
        if(touched & TOUCH_CTRL)
            retain_error(&first, try_modify(ce, id,
                       base + VQEAF_WCN3990_CE_CTRL1,
                       VQEAF_WCN3990_CE_CTRL1_MAX_TRANSFER, 0));
        retain_error(&first, try_modify(ce, id,
                   base + VQEAF_WCN3990_CE_SRC_WATERMARK, 0xffffffffu, 0));
    }
    if(touched & TOUCH_DST) {
        retain_error(&first, try_write(ce,
                   base + VQEAF_WCN3990_CE_DST_BASE, 0));
        retain_error(&first, try_write(ce,
                   base + VQEAF_WCN3990_CE_DST_SIZE, 0));
        retain_error(&first, try_modify(ce, id,
                   base + VQEAF_WCN3990_CE_DST_WATERMARK, 0xffffffffu, 0));
    }
    if(first) return first;
    ce->active_mask = (uint16_t)(ce->active_mask & ~bit);
    ce->_poll_mask = (uint16_t)(ce->_poll_mask & ~bit);
    ce->_quiesce_mask = (uint16_t)(ce->_quiesce_mask & ~bit);
    ce->_src_entries[id] = 0;
    ce->_dst_entries[id] = 0;
    ce->_touched[id] = 0;
    fence(ce);
    return 0;
}

static int ring_address_valid(uint16_t entries, uint64_t address)
{
    uint64_t bytes, limit = 1ull << 32;
    if(!entries) return address == 0;
    if((entries & (entries - 1u)) != 0 || !address ||
       (address & (VQEAF_WCN3990_CE_RING_ALIGN - 1u)) != 0)
        return 0;
    bytes = (uint64_t)entries * sizeof(VqeafWcn3990CeDesc);
    return address < limit && bytes <= limit - address;
}

static int config_valid(const VqeafWcn3990CeConfig *cfg)
{
    return cfg && valid_id(cfg->id) && cfg->max_transfer &&
           !(cfg->flags & ~(uint32_t)VQEAF_WCN3990_CE_ATTR_DIS_INTR) &&
           (cfg->src_entries || cfg->dst_entries);
}

static int indices_online(VqeafWcn3990Ce *ce, uint32_t base,
                          uint32_t read_offset, uint32_t write_offset,
                          int read_index_on_ddr)
{
    uint32_t value;
    int rc;
    if(!read_index_on_ddr) {
        rc = read_reg(ce, base + read_offset, &value);
        if(rc) return rc;
        if(value == UINT32_MAX) return -ENODEV;
    }
    rc = read_reg(ce, base + write_offset, &value);
    if(rc) return rc;
    return value == UINT32_MAX ? -ENODEV : 0;
}

static int init_source(VqeafWcn3990Ce *ce, const VqeafWcn3990CeConfig *cfg,
                       uint64_t address)
{
    uint32_t base = ce_base(cfg->id);
    int rc;
    if(!cfg->src_entries) return 0;
    rc = indices_online(ce, base, VQEAF_WCN3990_CE_SRC_READ_INDEX,
                        VQEAF_WCN3990_CE_SRC_WRITE_INDEX,
                        (cfg->flags & VQEAF_WCN3990_CE_ATTR_DIS_INTR) != 0);
    if(rc) return rc;
    rc = write_reg(ce, cfg->id, base + VQEAF_WCN3990_CE_SRC_BASE,
                   (uint32_t)address, TOUCH_SRC);
    if(rc) return rc;
    rc = write_reg(ce, cfg->id, base + VQEAF_WCN3990_CE_SRC_SIZE,
                   cfg->src_entries, TOUCH_SRC);
    if(rc) return rc;
    rc = modify_reg(ce, cfg->id, base + VQEAF_WCN3990_CE_CTRL1,
                    VQEAF_WCN3990_CE_CTRL1_MAX_TRANSFER |
                    VQEAF_WCN3990_CE_CTRL1_SRC_SWAP,
                    cfg->max_transfer, TOUCH_CTRL);
    if(rc) return rc;
    return modify_reg(ce, cfg->id, base + VQEAF_WCN3990_CE_SRC_WATERMARK,
                      0xffffffffu, cfg->src_entries, TOUCH_SRC);
}

static int init_destination(VqeafWcn3990Ce *ce,
                            const VqeafWcn3990CeConfig *cfg,
                            uint64_t address)
{
    uint32_t base = ce_base(cfg->id);
    int rc;
    if(!cfg->dst_entries) return 0;
    rc = indices_online(ce, base, VQEAF_WCN3990_CE_DST_READ_INDEX,
                        VQEAF_WCN3990_CE_DST_WRITE_INDEX,
                        (cfg->flags & VQEAF_WCN3990_CE_ATTR_DIS_INTR) != 0);
    if(rc) return rc;
    rc = write_reg(ce, cfg->id, base + VQEAF_WCN3990_CE_DST_BASE,
                   (uint32_t)address, TOUCH_DST);
    if(rc) return rc;
    rc = write_reg(ce, cfg->id, base + VQEAF_WCN3990_CE_DST_SIZE,
                   cfg->dst_entries, TOUCH_DST);
    if(rc) return rc;
    rc = modify_reg(ce, cfg->id, base + VQEAF_WCN3990_CE_CTRL1,
                    VQEAF_WCN3990_CE_CTRL1_DST_SWAP, 0, TOUCH_CTRL);
    if(rc) return rc;
    return modify_reg(ce, cfg->id, base + VQEAF_WCN3990_CE_DST_WATERMARK,
                      0xffffffffu, cfg->dst_entries, TOUCH_DST);
}

int vqeaf_wcn3990_ce_init(VqeafWcn3990Ce *ce,
                          const VqeafWcn3990CeMmio *mmio)
{
    uint32_t required_high;
    if(!ce || !mmio || !mmio->window_base ||
       mmio->window_base > UINT64_MAX - mmio->window_size ||
       mmio->window_size < VQEAF_WCN3990_CE_REGISTER_END ||
       !mmio->mmio.read32 || !mmio->mmio.write32 || !mmio->mmio.barrier)
        return -EINVAL;
    if(mmio->dma_bits < 32u || mmio->dma_bits > VQEAF_WCN3990_DMA_BITS)
        return -ERANGE;
    required_high = mmio->dma_bits > 32u ?
                    (1u << (mmio->dma_bits - 32u)) - 1u : 0u;
    if((mmio->address_high_mask & required_high) != required_high)
        return -ERANGE;
    if(ce->abi == VQEAF_WCN3990_CE_ABI && ce->size == sizeof(*ce) &&
       (ce->active_mask || ce->_quiesce_mask || ce->_rri_ready))
        return -EBUSY;
    memset(ce, 0, sizeof(*ce));
    ce->abi = VQEAF_WCN3990_CE_ABI;
    ce->size = sizeof(*ce);
    ce->window_base = mmio->window_base;
    ce->window_size = mmio->window_size;
    ce->dma_bits = mmio->dma_bits;
    ce->address_high_mask = mmio->address_high_mask;
    ce->mmio = mmio->mmio;
    return 0;
}

int vqeaf_wcn3990_ce_configure(void *ctx,
                               const VqeafWcn3990CeConfig *cfg,
                               uint64_t src, uint64_t dst)
{
    VqeafWcn3990Ce *ce = ctx;
    int rc, cleanup;
    if(!attached(ce)) return -ENOSYS;
    if(!config_valid(cfg)) return -EINVAL;
    if(!ring_address_valid(cfg->src_entries, src) ||
       !ring_address_valid(cfg->dst_entries, dst)) return -ERANGE;
    if((ce->active_mask | ce->_quiesce_mask) & (1u << cfg->id)) return -EBUSY;
    ce->_touched[cfg->id] = 0;
    fence(ce);
    rc = interrupt_disable(ce, cfg->id);
    if(rc) goto fail;
    rc = init_source(ce, cfg, src);
    if(rc) goto fail;
    rc = init_destination(ce, cfg, dst);
    if(rc) goto fail;
    ce->_src_entries[cfg->id] = cfg->src_entries;
    ce->_dst_entries[cfg->id] = cfg->dst_entries;
    ce->active_mask = (uint16_t)(ce->active_mask | (1u << cfg->id));
    if(cfg->flags & VQEAF_WCN3990_CE_ATTR_DIS_INTR)
        ce->_poll_mask = (uint16_t)(ce->_poll_mask | (1u << cfg->id));
    fence(ce);
    return 0;
fail:
    cleanup = deinit(ce, cfg->id, ce->_touched[cfg->id]);
    if(cleanup) {
        ce->_quiesce_mask = (uint16_t)(ce->_quiesce_mask | (1u << cfg->id));
        return cleanup;
    }
    return rc;
}

int vqeaf_wcn3990_ce_disable(void *ctx, uint8_t id)
{
    VqeafWcn3990Ce *ce = ctx;
    uint16_t bit;
    if(!attached(ce)) return -ENOSYS;
    if(!valid_id(id)) return -EINVAL;
    bit = (uint16_t)(1u << id);
    if(!(ce->active_mask & bit) && !(ce->_quiesce_mask & bit)) return 0;
    return deinit(ce, id, ce->_touched[id]);
}

int vqeaf_wcn3990_ce_rri_enable(VqeafWcn3990Ce *ce, void *rri_cpu,
                                uint64_t rri_dma)
{
    uint32_t high_mask, updated = 0, id;
    int rc, cleanup;
    if(!attached(ce)) return -ENOSYS;
    if(!rri_cpu || !rri_dma) return -EINVAL;
    if(ce->_rri_ready) return -EBUSY;
    if(ce->active_mask || ce->_quiesce_mask) return -EBUSY;
    high_mask = (1u << (VQEAF_WCN3990_DMA_BITS - 32u)) - 1u;
    if((rri_dma & (VQEAF_WCN3990_CE_RRI_ALIGN - 1u)) != 0 ||
       rri_dma >= (1ull << VQEAF_WCN3990_DMA_BITS) ||
       VQEAF_WCN3990_CE_RRI_BYTES >
           (1ull << VQEAF_WCN3990_DMA_BITS) - rri_dma)
        return -ERANGE;
    fence(ce);
    rc = write_reg(ce, 0, VQEAF_WCN3990_CE_RRI_LOW, (uint32_t)rri_dma, 0);
    if(!rc)
        rc = write_reg(ce, 0, VQEAF_WCN3990_CE_RRI_HIGH,
                       (uint32_t)((rri_dma >> 32) & high_mask), 0);
    for(id = 0; !rc && id < VQEAF_WCN3990_CE_MAX; id++) {
        rc = modify_reg(ce, (uint8_t)id, ce_base((uint8_t)id) +
                        VQEAF_WCN3990_CE_CTRL1, VQEAF_WCN3990_CE_CTRL1_UPD,
                        VQEAF_WCN3990_CE_CTRL1_UPD, 0);
        if(!rc) updated++;
    }
    if(rc) {
        cleanup = 0;
        for(id = 0; id < updated; id++)
            retain_error(&cleanup, try_modify(ce, (uint8_t)id,
                       ce_base((uint8_t)id) + VQEAF_WCN3990_CE_CTRL1,
                       VQEAF_WCN3990_CE_CTRL1_UPD, 0));
        retain_error(&cleanup, try_write(ce, VQEAF_WCN3990_CE_RRI_HIGH, 0));
        retain_error(&cleanup, try_write(ce, VQEAF_WCN3990_CE_RRI_LOW, 0));
        return cleanup ? cleanup : rc;
    }
    zero_rri((volatile uint32_t *)rri_cpu);
    ce->rri_cpu = (volatile uint32_t *)rri_cpu;
    ce->rri_dma = rri_dma;
    ce->_rri_ready = 1;
    fence(ce);
    return 0;
}

int vqeaf_wcn3990_ce_rri_disable(VqeafWcn3990Ce *ce)
{
    uint8_t id;
    int first = 0;
    if(!attached(ce)) return -ENOSYS;
    if(!ce->_rri_ready) return 0;
    if(ce->active_mask || ce->_quiesce_mask) return -EBUSY;
    for(id = 0; id < VQEAF_WCN3990_CE_MAX; id++)
        retain_error(&first, try_modify(ce, id, ce_base(id) +
                   VQEAF_WCN3990_CE_CTRL1, VQEAF_WCN3990_CE_CTRL1_UPD, 0));
    retain_error(&first, try_write(ce, VQEAF_WCN3990_CE_RRI_HIGH, 0));
    retain_error(&first, try_write(ce, VQEAF_WCN3990_CE_RRI_LOW, 0));
    if(first) return first;
    zero_rri(ce->rri_cpu);
    ce->rri_cpu = 0;
    ce->rri_dma = 0;
    ce->_rri_ready = 0;
    fence(ce);
    return 0;
}

static int read_index(VqeafWcn3990Ce *ce, uint32_t offset, uint16_t entries,
                      uint32_t *out)
{
    uint32_t value;
    int rc = read_reg(ce, offset, &value);
    if(rc) return rc;
    if(value == UINT32_MAX) return -ENODEV;
    *out = value & (entries - 1u);
    return 0;
}

int vqeaf_wcn3990_ce_indices(void *ctx, uint8_t id, uint32_t *src_read,
                             uint32_t *src_write, uint32_t *dst_read,
                             uint32_t *dst_write)
{
    VqeafWcn3990Ce *ce = ctx;
    uint32_t base;
    int rc;
    int poll;
    if(!attached(ce)) return -ENOSYS;
    if(!valid_id(id)) return -EINVAL;
    if(!(ce->active_mask & (1u << id))) return -EINVAL;
    poll = (ce->_poll_mask & (1u << id)) != 0;
    if(poll && (!ce->_rri_ready || !ce->rri_cpu)) return -EOPNOTSUPP;
    base = ce_base(id);
    if(src_read) *src_read = 0;
    if(src_write) *src_write = 0;
    if(dst_read) *dst_read = 0;
    if(dst_write) *dst_write = 0;
    if(ce->_src_entries[id]) {
        if(src_read) {
            if(poll) {
                fence(ce);
                *src_read = ce->rri_cpu[id] & VQEAF_WCN3990_CE_RRI_SRC_MASK;
                *src_read &= ce->_src_entries[id] - 1u;
            } else {
                rc = read_index(ce, base + VQEAF_WCN3990_CE_SRC_READ_INDEX,
                                ce->_src_entries[id], src_read);
                if(rc) return rc;
            }
        }
        if(src_write) {
            rc = read_index(ce, base + VQEAF_WCN3990_CE_SRC_WRITE_INDEX,
                            ce->_src_entries[id], src_write);
            if(rc) return rc;
        }
    }
    if(ce->_dst_entries[id]) {
        if(dst_read) {
            if(poll) {
                fence(ce);
                *dst_read = (ce->rri_cpu[id] >>
                             VQEAF_WCN3990_CE_RRI_DST_SHIFT) &
                            VQEAF_WCN3990_CE_RRI_SRC_MASK;
                *dst_read &= ce->_dst_entries[id] - 1u;
            } else {
                rc = read_index(ce, base + VQEAF_WCN3990_CE_DST_READ_INDEX,
                                ce->_dst_entries[id], dst_read);
                if(rc) return rc;
            }
        }
        if(dst_write) {
            rc = read_index(ce, base + VQEAF_WCN3990_CE_DST_WRITE_INDEX,
                            ce->_dst_entries[id], dst_write);
            if(rc) return rc;
        }
    }
    return 0;
}

int vqeaf_wcn3990_ce_interrupt_disable(void *ctx, uint8_t id)
{
    VqeafWcn3990Ce *ce = ctx;
    if(!attached(ce)) return -ENOSYS;
    if(!valid_id(id)) return -EINVAL;
    return interrupt_disable(ce, id);
}

int vqeaf_wcn3990_ce_interrupt_enable(void *ctx, uint8_t id,
                                      uint32_t host_mask, uint32_t misc_mask)
{
    VqeafWcn3990Ce *ce = ctx;
    uint32_t base;
    int rc, cleanup;
    if(!attached(ce)) return -ENOSYS;
    if(!valid_id(id)) return -EINVAL;
    if(!(ce->active_mask & (1u << id))) return -EINVAL;
    if((host_mask & ~VQEAF_WCN3990_CE_INTR_HOST_MASK) ||
       (misc_mask & ~VQEAF_WCN3990_CE_INTR_ERROR_MASK))
        return -EINVAL;
    if(ce->_poll_mask & (1u << id)) return -EOPNOTSUPP;
    base = ce_base(id);
    rc = modify_reg(ce, id, base + VQEAF_WCN3990_CE_MISC_INTR_ENABLE,
                    misc_mask, misc_mask, TOUCH_INTR);
    if(rc) return rc;
    rc = modify_reg(ce, id, base + VQEAF_WCN3990_CE_HOST_INTR_ENABLE,
                    host_mask, host_mask, TOUCH_INTR);
    if(rc) {
        cleanup = try_modify(ce, id, base + VQEAF_WCN3990_CE_HOST_INTR_ENABLE,
                             host_mask, 0);
        retain_error(&cleanup, try_modify(ce, id,
                   base + VQEAF_WCN3990_CE_MISC_INTR_ENABLE, misc_mask, 0));
        return cleanup ? cleanup : rc;
    }
    return 0;
}

int vqeaf_wcn3990_ce_acknowledge(void *ctx, uint8_t id, uint32_t mask)
{
    VqeafWcn3990Ce *ce = ctx;
    if(!attached(ce)) return -ENOSYS;
    if(!valid_id(id)) return -EINVAL;
    if(mask & ~VQEAF_WCN3990_CE_INTR_HOST_MASK) return -EINVAL;
    if(!mask) return 0;
    return write_reg(ce, id, ce_base(id) + VQEAF_WCN3990_CE_STATUS_ACK,
                     mask, 0);
}
