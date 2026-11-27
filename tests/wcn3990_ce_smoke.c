#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "vqeaf/wcn3990_ce.h"

#define MAX_REGS 256u
typedef struct {
    uint32_t offset, value, writes, reads;
} FakeReg;
typedef struct {
    FakeReg regs[MAX_REGS];
    uint32_t count;
    uint32_t barriers, read_calls, write_calls;
    int fail_read_at, fail_write_at;
    int fail_read_forever, fail_write_forever;
} Fake;

static FakeReg *fake_reg(Fake *f, uint32_t offset)
{
    uint32_t i;
    for(i = 0; i < f->count; i++)
        if(f->regs[i].offset == offset) return &f->regs[i];
    assert(f->count < MAX_REGS);
    f->regs[f->count].offset = offset;
    return &f->regs[f->count++];
}

static uint32_t value(Fake *f, uint32_t offset)
{
    return fake_reg(f, offset)->value;
}

static void set_value(Fake *f, uint32_t offset, uint32_t v)
{
    fake_reg(f, offset)->value = v;
}

static uint32_t writes(Fake *f, uint32_t offset)
{
    return fake_reg(f, offset)->writes;
}

static uint32_t reads(Fake *f, uint32_t offset)
{
    return fake_reg(f, offset)->reads;
}

static int fake_read(void *ctx, uint32_t offset, uint32_t *out)
{
    Fake *f = ctx;
    FakeReg *r;
    f->read_calls++;
    if(f->fail_read_at == (int)f->read_calls) return -EIO;
    if(f->fail_read_forever && f->read_calls >= (uint32_t)f->fail_read_at)
        return -EIO;
    r = fake_reg(f, offset);
    r->reads++;
    *out = r->value;
    return 0;
}

static int fake_write(void *ctx, uint32_t offset, uint32_t v)
{
    Fake *f = ctx;
    FakeReg *r;
    f->write_calls++;
    if(f->fail_write_at == (int)f->write_calls) return -EIO;
    if(f->fail_write_forever && f->write_calls >= (uint32_t)f->fail_write_at)
        return -EIO;
    r = fake_reg(f, offset);
    r->value = v;
    r->writes++;
    return 0;
}

static void fake_barrier(void *ctx)
{
    ((Fake *)ctx)->barriers++;
}

static VqeafWcn3990CeMmio mmio_desc(Fake *f)
{
    VqeafWcn3990CeMmio m;
    memset(&m, 0, sizeof(m));
    m.window_base = 0x18800000ull;
    m.window_size = 0x800000ull;
    m.dma_bits = 37u;
    m.address_high_mask = 0x1fu;
    m.mmio.ctx = f;
    m.mmio.read32 = fake_read;
    m.mmio.write32 = fake_write;
    m.mmio.barrier = fake_barrier;
    return m;
}

static void init_ce(Fake *f, VqeafWcn3990Ce *ce)
{
    VqeafWcn3990CeMmio m = mmio_desc(f);
    memset(f, 0, sizeof(*f));
    memset(ce, 0, sizeof(*ce));
    assert(vqeaf_wcn3990_ce_init(ce, &m) == 0);
    assert(f->read_calls == 0 && f->write_calls == 0);
}

static VqeafWcn3990CeConfig config(uint8_t id, uint16_t src, uint16_t dst,
                                   uint16_t max_transfer, uint32_t flags)
{
    VqeafWcn3990CeConfig c;
    memset(&c, 0, sizeof(c));
    c.id = id;
    c.src_entries = src;
    c.dst_entries = dst;
    c.max_transfer = max_transfer;
    c.flags = flags;
    return c;
}

static uint32_t reg(uint8_t id, uint32_t offset)
{
    return VQEAF_WCN3990_CE0_BASE + (uint32_t)id * VQEAF_WCN3990_CE_STRIDE + offset;
}

static void test_missing_provider(void)
{
    VqeafWcn3990Ce ce;
    VqeafWcn3990CeConfig c = config(0, 16, 0, 2048, 0);
    memset(&ce, 0, sizeof(ce));
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x1000, 0) == -ENOSYS);
    assert(vqeaf_wcn3990_ce_interrupt_disable(&ce, 0) == -ENOSYS);
    assert(vqeaf_wcn3990_ce_acknowledge(&ce, 0, 1) == -ENOSYS);
    assert(vqeaf_wcn3990_ce_indices(&ce, 0, 0, 0, 0, 0) == -ENOSYS);
    assert(vqeaf_wcn3990_ce_disable(&ce, 0) == -ENOSYS);
}

static void test_init_validation(void)
{
    Fake f;
    VqeafWcn3990Ce ce;
    VqeafWcn3990CeMmio m = mmio_desc(&f);
    memset(&f, 0, sizeof(f));
    memset(&ce, 0, sizeof(ce));
    assert(vqeaf_wcn3990_ce_init(&ce, 0) == -EINVAL);
    m.window_size = VQEAF_WCN3990_CE_REGISTER_END - 1u;
    assert(vqeaf_wcn3990_ce_init(&ce, &m) == -EINVAL);
    m = mmio_desc(&f);
    m.window_size = UINT64_MAX;
    assert(vqeaf_wcn3990_ce_init(&ce, &m) == -EINVAL);
    m = mmio_desc(&f);
    m.dma_bits = 31u;
    assert(vqeaf_wcn3990_ce_init(&ce, &m) == -ERANGE);
    m = mmio_desc(&f);
    m.dma_bits = VQEAF_WCN3990_DMA_BITS + 1u;
    assert(vqeaf_wcn3990_ce_init(&ce, &m) == -ERANGE);
    m = mmio_desc(&f);
    m.address_high_mask = 0x1eu;
    assert(vqeaf_wcn3990_ce_init(&ce, &m) == -ERANGE);
    m = mmio_desc(&f);
    m.mmio.barrier = 0;
    assert(vqeaf_wcn3990_ce_init(&ce, &m) == -EINVAL);
    m = mmio_desc(&f);
    assert(vqeaf_wcn3990_ce_init(&ce, &m) == 0);
    assert(f.read_calls == 0 && f.write_calls == 0 && f.barriers == 0);
}

static void test_source_only_and_disable(void)
{
    Fake f;
    VqeafWcn3990Ce ce;
    VqeafWcn3990CeConfig c = config(0, 16, 0, 2048, 0);
    uint32_t sr, sw, dr, dw;
    init_ce(&f, &ce);
    set_value(&f, reg(0, VQEAF_WCN3990_CE_SRC_READ_INDEX), 0x25u);
    set_value(&f, reg(0, VQEAF_WCN3990_CE_SRC_WRITE_INDEX), 0x33u);
    set_value(&f, reg(0, VQEAF_WCN3990_CE_HOST_INTR_ENABLE), UINT32_MAX);
    set_value(&f, reg(0, VQEAF_WCN3990_CE_MISC_INTR_ENABLE), UINT32_MAX);
    set_value(&f, reg(0, VQEAF_WCN3990_CE_CTRL1), 0x80022000u);
    set_value(&f, reg(0, VQEAF_WCN3990_CE_SRC_WATERMARK), 0xabcd1234u);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x12345678u, 0) == 0);
    assert(value(&f, reg(0, VQEAF_WCN3990_CE_SRC_BASE)) == 0x12345678u);
    assert(value(&f, reg(0, VQEAF_WCN3990_CE_SRC_SIZE)) == 16u);
    assert(value(&f, reg(0, VQEAF_WCN3990_CE_CTRL1)) == 0x80000800u);
    assert(value(&f, reg(0, VQEAF_WCN3990_CE_SRC_WATERMARK)) == 16u);
    assert(value(&f, reg(0, VQEAF_WCN3990_CE_HOST_INTR_ENABLE)) == 0xffffffe0u);
    assert(value(&f, reg(0, VQEAF_WCN3990_CE_MISC_INTR_ENABLE)) == 0xfffffc1fu);
    assert(writes(&f, reg(0, VQEAF_WCN3990_CE_DST_BASE)) == 0);
    assert(writes(&f, reg(0, 0x10u)) == 0 && reads(&f, reg(0, 0x10u)) == 0);
    assert(writes(&f, reg(0, 0x12u)) == 0 && reads(&f, reg(0, 0x12u)) == 0);
    assert(writes(&f, reg(0, VQEAF_WCN3990_CE_SRC_READ_INDEX)) == 0);
    assert(writes(&f, reg(0, VQEAF_WCN3990_CE_SRC_WRITE_INDEX)) == 0);
    assert(f.barriers != 0);
    {
        uint32_t before = f.write_calls;
        assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x12345678u, 0) == -EBUSY);
        assert(f.write_calls == before);
    }
    set_value(&f, reg(0, VQEAF_WCN3990_CE_SRC_READ_INDEX), 0x2du);
    set_value(&f, reg(0, VQEAF_WCN3990_CE_SRC_WRITE_INDEX), 0x35u);
    assert(vqeaf_wcn3990_ce_indices(&ce, 0, &sr, &sw, &dr, &dw) == 0);
    assert(sr == 13u && sw == 5u && dr == 0u && dw == 0u);
    assert(vqeaf_wcn3990_ce_disable(&ce, 0) == 0);
    assert(value(&f, reg(0, VQEAF_WCN3990_CE_SRC_BASE)) == 0);
    assert(value(&f, reg(0, VQEAF_WCN3990_CE_SRC_SIZE)) == 0);
    assert(value(&f, reg(0, VQEAF_WCN3990_CE_CTRL1)) == 0x80000000u);
    assert(value(&f, reg(0, VQEAF_WCN3990_CE_SRC_WATERMARK)) == 0);
    assert(value(&f, reg(0, VQEAF_WCN3990_CE_HOST_INTR_ENABLE)) == 0xffffffe0u);
    assert(value(&f, reg(0, VQEAF_WCN3990_CE_MISC_INTR_ENABLE)) == 0xfffffc1fu);
}

static void test_destination_only_and_interrupts(void)
{
    Fake f;
    VqeafWcn3990Ce ce;
    VqeafWcn3990CeConfig c = config(1, 0, 512, 2048, 0);
    uint32_t sr, sw, dr, dw;
    init_ce(&f, &ce);
    set_value(&f, reg(1, VQEAF_WCN3990_CE_DST_READ_INDEX), 0x40u);
    set_value(&f, reg(1, VQEAF_WCN3990_CE_DST_WRITE_INDEX), 0x80u);
    set_value(&f, reg(1, 0x10u), 0xa0000000u);
    set_value(&f, reg(1, 0x12u), 0x5a5a5a5au);
    set_value(&f, reg(1, VQEAF_WCN3990_CE_CTRL1), 0x80040055u);
    set_value(&f, reg(1, VQEAF_WCN3990_CE_DST_WATERMARK), 0xabcd1234u);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0, 0x89abcde0u) == 0);
    assert(value(&f, reg(1, VQEAF_WCN3990_CE_DST_BASE)) == 0x89abcde0u);
    assert(value(&f, reg(1, VQEAF_WCN3990_CE_DST_SIZE)) == 512u);
    assert(value(&f, reg(1, VQEAF_WCN3990_CE_CTRL1)) == 0x80000055u);
    assert(value(&f, reg(1, VQEAF_WCN3990_CE_DST_WATERMARK)) == 512u);
    assert(writes(&f, reg(1, VQEAF_WCN3990_CE_SRC_BASE)) == 0);
    assert(reads(&f, reg(1, VQEAF_WCN3990_CE_SRC_READ_INDEX)) == 0);
    assert(writes(&f, reg(1, 0x10u)) == 0 && reads(&f, reg(1, 0x10u)) == 0);
    assert(writes(&f, reg(1, 0x12u)) == 0 && reads(&f, reg(1, 0x12u)) == 0);
    set_value(&f, reg(1, VQEAF_WCN3990_CE_DST_READ_INDEX), 0x999u);
    set_value(&f, reg(1, VQEAF_WCN3990_CE_DST_WRITE_INDEX), 0x555u);
    assert(vqeaf_wcn3990_ce_indices(&ce, 1, &sr, &sw, &dr, &dw) == 0);
    assert(sr == 0 && sw == 0 && dr == 0x199u && dw == 0x155u);
    assert(vqeaf_wcn3990_ce_acknowledge(&ce, 1, 0x1fu) == 0);
    assert(value(&f, reg(1, VQEAF_WCN3990_CE_STATUS_ACK)) == 0x1fu);
    {
        uint32_t before = f.write_calls;
        assert(vqeaf_wcn3990_ce_acknowledge(&ce, 1, 0x20u) == -EINVAL);
        assert(f.write_calls == before);
    }
    set_value(&f, reg(1, VQEAF_WCN3990_CE_HOST_INTR_ENABLE), 0x100u);
    set_value(&f, reg(1, VQEAF_WCN3990_CE_MISC_INTR_ENABLE), 0x80000000u);
    assert(vqeaf_wcn3990_ce_interrupt_enable(&ce, 1, 0x1fu, 0x3e0u) == 0);
    assert(value(&f, reg(1, VQEAF_WCN3990_CE_HOST_INTR_ENABLE)) == 0x11fu);
    assert(value(&f, reg(1, VQEAF_WCN3990_CE_MISC_INTR_ENABLE)) == 0x800003e0u);
    assert(vqeaf_wcn3990_ce_disable(&ce, 1) == 0);
    assert(value(&f, reg(1, VQEAF_WCN3990_CE_DST_BASE)) == 0);
    assert(value(&f, reg(1, VQEAF_WCN3990_CE_DST_SIZE)) == 0);
    assert(value(&f, reg(1, VQEAF_WCN3990_CE_DST_WATERMARK)) == 0);
    assert(value(&f, reg(1, 0x10u)) == 0xa0000000u);
    assert(value(&f, reg(1, 0x12u)) == 0x5a5a5a5au);
}

static void test_dual_ring_and_poll(void)
{
    Fake f;
    VqeafWcn3990Ce ce;
    VqeafWcn3990CeConfig dual = config(7, 2, 2, 2048, 0);
    VqeafWcn3990CeConfig poll = config(4, 2048, 0, 256,
                                       VQEAF_WCN3990_CE_ATTR_DIS_INTR);
    init_ce(&f, &ce);
    set_value(&f, reg(7, VQEAF_WCN3990_CE_CTRL1), 0x80060000u);
    assert(vqeaf_wcn3990_ce_configure(&ce, &dual, 0x20000000u,
                                      0x30000000u) == 0);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_SRC_BASE)) == 0x20000000u);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_DST_BASE)) == 0x30000000u);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_CTRL1)) == 0x80000800u);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_SRC_WATERMARK)) == 2u);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_DST_WATERMARK)) == 2u);
    set_value(&f, reg(4, VQEAF_WCN3990_CE_SRC_READ_INDEX), UINT32_MAX);
    assert(vqeaf_wcn3990_ce_configure(&ce, &poll, 0x40000000u, 0) == 0);
    assert(reads(&f, reg(4, VQEAF_WCN3990_CE_SRC_READ_INDEX)) == 0);
    assert(value(&f, reg(4, VQEAF_WCN3990_CE_SRC_BASE)) == 0x40000000u);
    assert(writes(&f, reg(7, 0x10u)) == 0 && reads(&f, reg(7, 0x10u)) == 0);
    assert(writes(&f, reg(7, 0x12u)) == 0 && reads(&f, reg(7, 0x12u)) == 0);
    assert(writes(&f, reg(4, 0x10u)) == 0 && reads(&f, reg(4, 0x10u)) == 0);
    assert(writes(&f, reg(4, 0x12u)) == 0 && reads(&f, reg(4, 0x12u)) == 0);
    assert(vqeaf_wcn3990_ce_indices(&ce, 4, 0, 0, 0, 0) == -EOPNOTSUPP);
    assert(vqeaf_wcn3990_ce_interrupt_enable(&ce, 4, 1, 0) == -EOPNOTSUPP);
}

static void test_invalid_configuration(void)
{
    Fake f;
    VqeafWcn3990Ce ce;
    VqeafWcn3990CeConfig c;
    init_ce(&f, &ce);
    c = config(12, 16, 0, 2048, 0);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x1000, 0) == -EINVAL);
    c = config(0, 0, 0, 2048, 0);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0, 0) == -EINVAL);
    c = config(0, 16, 0, 0, 0);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x1000, 0) == -EINVAL);
    c = config(0, 16, 0, 2048, 1u);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x1000, 0) == -EINVAL);
    c = config(0, 3, 0, 2048, 0);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x1000, 0) == -ERANGE);
    c = config(0, 16, 0, 2048, 0);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0, 0) == -ERANGE);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x1001, 0) == -ERANGE);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x1000, 0x8) == -ERANGE);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 1ull << 32, 0) == -ERANGE);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, (1ull << 32) - 8u, 0) == -ERANGE);
    c = config(0, 0, 16, 2048, 0);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0, 1ull << 32) == -ERANGE);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0, (1ull << 32) - 8u) == -ERANGE);
    assert(vqeaf_wcn3990_ce_configure(&ce, 0, 0, 0) == -EINVAL);
    assert(vqeaf_wcn3990_ce_interrupt_disable(&ce, 12) == -EINVAL);
    assert(vqeaf_wcn3990_ce_interrupt_enable(&ce, 0, 1, 0) == -EINVAL);
    assert(f.write_calls == 0);
}

static void test_read_failure_and_offline(void)
{
    Fake f;
    VqeafWcn3990Ce ce;
    VqeafWcn3990CeConfig c = config(7, 2, 2, 2048, 0);
    init_ce(&f, &ce);
    f.fail_read_at = 1;
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x1000, 0x2000) == -EIO);
    assert(f.write_calls == 0);
    init_ce(&f, &ce);
    f.fail_read_at = 3;
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x1000, 0x2000) == -EIO);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_SRC_BASE)) == 0);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_DST_BASE)) == 0);
    init_ce(&f, &ce);
    set_value(&f, reg(7, VQEAF_WCN3990_CE_SRC_READ_INDEX), UINT32_MAX);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x1000, 0x2000) == -ENODEV);
    assert(writes(&f, reg(7, VQEAF_WCN3990_CE_SRC_BASE)) == 0);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_HOST_INTR_ENABLE)) == 0);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_MISC_INTR_ENABLE)) == 0);
}

static void test_partial_write_rollback(void)
{
    Fake f;
    VqeafWcn3990Ce ce;
    VqeafWcn3990CeConfig c = config(7, 2, 2, 2048, 0);
    init_ce(&f, &ce);
    set_value(&f, reg(7, VQEAF_WCN3990_CE_CTRL1), 0x80000000u);
    f.fail_write_at = 5;
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x1000, 0x2000) == -EIO);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_SRC_BASE)) == 0);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_SRC_SIZE)) == 0);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_CTRL1)) == 0x80000000u);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_SRC_WATERMARK)) == 0);
    assert(writes(&f, reg(7, VQEAF_WCN3990_CE_DST_BASE)) == 0);
    init_ce(&f, &ce);
    f.fail_write_at = 8;
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x1000, 0x2000) == -EIO);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_SRC_BASE)) == 0);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_SRC_SIZE)) == 0);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_DST_BASE)) == 0);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_DST_SIZE)) == 0);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_DST_WATERMARK)) == 0);
    assert(writes(&f, reg(7, 0x10u)) == 0 && reads(&f, reg(7, 0x10u)) == 0);
    assert(writes(&f, reg(7, 0x12u)) == 0 && reads(&f, reg(7, 0x12u)) == 0);
    assert(vqeaf_wcn3990_ce_indices(&ce, 7, 0, 0, 0, 0) == -EINVAL);
}

static void test_32_bit_ring_aperture(void)
{
    Fake f;
    VqeafWcn3990Ce ce;
    VqeafWcn3990CeMmio m = mmio_desc(&f);
    VqeafWcn3990CeConfig src = config(0, 1, 0, 2048, 0);
    VqeafWcn3990CeConfig dst = config(0, 0, 1, 2048, 0);
    uint64_t edge = (1ull << 32) - 16u;
    memset(&f, 0, sizeof(f));
    memset(&ce, 0, sizeof(ce));
    m.dma_bits = 35u;
    m.address_high_mask = 0x7u;
    assert(vqeaf_wcn3990_ce_init(&ce, &m) == 0);
    assert(ce.dma_bits == 35u && ce.address_high_mask == 0x7u);
    assert(vqeaf_wcn3990_ce_configure(&ce, &src, edge, 0) == 0);
    assert(value(&f, reg(0, VQEAF_WCN3990_CE_SRC_BASE)) == 0xfffffff0u);
    assert(vqeaf_wcn3990_ce_disable(&ce, 0) == 0);
    assert(vqeaf_wcn3990_ce_configure(&ce, &dst, 0, edge) == 0);
    assert(value(&f, reg(0, VQEAF_WCN3990_CE_DST_BASE)) == 0xfffffff0u);
    assert(vqeaf_wcn3990_ce_disable(&ce, 0) == 0);
    assert(vqeaf_wcn3990_ce_configure(&ce, &src, 1ull << 32, 0) == -ERANGE);
    assert(vqeaf_wcn3990_ce_configure(&ce, &src, 1ull << 35, 0) == -ERANGE);
    assert(vqeaf_wcn3990_ce_configure(&ce, &dst, 0, 1ull << 32) == -ERANGE);
    assert(vqeaf_wcn3990_ce_configure(&ce, &dst, 0, 1ull << 35) == -ERANGE);
    assert(ce.active_mask == 0 && ce._quiesce_mask == 0);
    assert(writes(&f, reg(0, 0x10u)) == 0 && reads(&f, reg(0, 0x10u)) == 0);
    assert(writes(&f, reg(0, 0x12u)) == 0 && reads(&f, reg(0, 0x12u)) == 0);
}

static void test_quiesce_failures(void)
{
    Fake f;
    VqeafWcn3990Ce ce;
    VqeafWcn3990CeMmio m;
    VqeafWcn3990CeConfig c = config(7, 2, 2, 2048, 0);
    init_ce(&f, &ce);
    set_value(&f, reg(7, VQEAF_WCN3990_CE_CTRL1), 0x80000000u);
    f.fail_write_at = 6;
    f.fail_write_forever = 1;
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x1000, 0x2000) == -EIO);
    assert(ce.active_mask == 0 && ce._quiesce_mask == (1u << 7));
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_SRC_BASE)) == 0x1000u);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x1000, 0x2000) == -EBUSY);
    m = mmio_desc(&f);
    assert(vqeaf_wcn3990_ce_init(&ce, &m) == -EBUSY);
    assert(vqeaf_wcn3990_ce_disable(&ce, 7) == -EIO);
    assert(ce._quiesce_mask == (1u << 7));
    f.fail_write_forever = 0;
    f.fail_write_at = 0;
    assert(vqeaf_wcn3990_ce_disable(&ce, 7) == 0);
    assert(ce.active_mask == 0 && ce._quiesce_mask == 0);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_SRC_BASE)) == 0);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_DST_BASE)) == 0);
    assert(writes(&f, reg(7, 0x10u)) == 0 && reads(&f, reg(7, 0x10u)) == 0);
    assert(writes(&f, reg(7, 0x12u)) == 0 && reads(&f, reg(7, 0x12u)) == 0);

    init_ce(&f, &ce);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x1000, 0x2000) == 0);
    f.fail_read_at = 1;
    f.fail_read_forever = 1;
    assert(vqeaf_wcn3990_ce_disable(&ce, 7) == -EIO);
    assert(ce.active_mask == (1u << 7));
    f.fail_read_forever = 0;
    f.fail_read_at = 0;
    assert(vqeaf_wcn3990_ce_disable(&ce, 7) == 0);
    assert(ce.active_mask == 0 && ce._quiesce_mask == 0);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_SRC_BASE)) == 0);

    init_ce(&f, &ce);
    assert(vqeaf_wcn3990_ce_configure(&ce, &c, 0x1000, 0x2000) == 0);
    f.fail_write_at = 1;
    f.fail_write_forever = 1;
    assert(vqeaf_wcn3990_ce_disable(&ce, 7) == -EIO);
    assert(ce.active_mask == (1u << 7));
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_SRC_BASE)) == 0x1000u);
    f.fail_write_forever = 0;
    f.fail_write_at = 0;
    assert(vqeaf_wcn3990_ce_disable(&ce, 7) == 0);
    assert(ce.active_mask == 0 && ce._quiesce_mask == 0);
    assert(value(&f, reg(7, VQEAF_WCN3990_CE_SRC_BASE)) == 0);
}

static void test_rri(void)
{
    Fake f;
    VqeafWcn3990Ce ce;
    VqeafWcn3990CeMmio m;
    VqeafWcn3990CeConfig src = config(4, 16, 0, 256,
                                      VQEAF_WCN3990_CE_ATTR_DIS_INTR);
    VqeafWcn3990CeConfig dst = config(4, 0, 16, 256,
                                      VQEAF_WCN3990_CE_ATTR_DIS_INTR);
    uint32_t rri[VQEAF_WCN3990_CE_MAX];
    uint32_t sr, sw, dr, dw, id;
    uint64_t dma = 0x100000000ull;
    init_ce(&f, &ce);
    memset(rri, 0xaa, sizeof(rri));
    assert(vqeaf_wcn3990_ce_rri_enable(&ce, rri, 0) == -EINVAL);
    assert(vqeaf_wcn3990_ce_rri_enable(&ce, 0, dma) == -EINVAL);
    assert(vqeaf_wcn3990_ce_rri_enable(&ce, rri, dma + 1u) == -ERANGE);
    assert(vqeaf_wcn3990_ce_rri_enable(&ce, rri,
              1ull << VQEAF_WCN3990_DMA_BITS) == -ERANGE);
    assert(vqeaf_wcn3990_ce_rri_disable(&ce) == 0);
    assert(vqeaf_wcn3990_ce_rri_enable(&ce, rri, dma) == 0);
    assert(value(&f, VQEAF_WCN3990_CE_RRI_LOW) == 0u);
    assert(value(&f, VQEAF_WCN3990_CE_RRI_HIGH) == 1u);
    for(id = 0; id < VQEAF_WCN3990_CE_MAX; id++)
        assert(value(&f, reg((uint8_t)id, VQEAF_WCN3990_CE_CTRL1)) &
               VQEAF_WCN3990_CE_CTRL1_UPD);
    assert(rri[0] == 0 && rri[11] == 0);
    assert(vqeaf_wcn3990_ce_rri_enable(&ce, rri, dma) == -EBUSY);
    m = mmio_desc(&f);
    assert(vqeaf_wcn3990_ce_init(&ce, &m) == -EBUSY);
    assert(ce._rri_ready == 1 && ce.rri_dma == dma && ce.rri_cpu == rri);
    assert(vqeaf_wcn3990_ce_configure(&ce, &src, 0x40000000u, 0) == 0);
    assert(vqeaf_wcn3990_ce_rri_disable(&ce) == -EBUSY);
    rri[4] = ((uint32_t)5u << 16) | 0xfffffffbu;
    set_value(&f, reg(4, VQEAF_WCN3990_CE_SRC_WRITE_INDEX), 7u);
    assert(vqeaf_wcn3990_ce_indices(&ce, 4, &sr, &sw, &dr, &dw) == 0);
    assert(sr == 11u && sw == 7u && dr == 0u && dw == 0u);
    assert(vqeaf_wcn3990_ce_disable(&ce, 4) == 0);
    assert(vqeaf_wcn3990_ce_configure(&ce, &dst, 0, 0x40000000u) == 0);
    rri[4] = ((uint32_t)9u << 16) | 3u;
    set_value(&f, reg(4, VQEAF_WCN3990_CE_DST_WRITE_INDEX), 4u);
    assert(vqeaf_wcn3990_ce_indices(&ce, 4, &sr, &sw, &dr, &dw) == 0);
    assert(sr == 0 && sw == 0 && dr == 9u && dw == 4u);
    assert(vqeaf_wcn3990_ce_disable(&ce, 4) == 0);
    assert(vqeaf_wcn3990_ce_rri_disable(&ce) == 0);
    assert(ce._rri_ready == 0 && ce.rri_cpu == 0 && ce.rri_dma == 0);
    assert(rri[4] == 0);
    for(id = 0; id < VQEAF_WCN3990_CE_MAX; id++)
        assert((value(&f, reg((uint8_t)id, VQEAF_WCN3990_CE_CTRL1)) &
                VQEAF_WCN3990_CE_CTRL1_UPD) == 0);
    assert(value(&f, VQEAF_WCN3990_CE_RRI_LOW) == 0u);
    assert(value(&f, VQEAF_WCN3990_CE_RRI_HIGH) == 0u);

    init_ce(&f, &ce);
    f.fail_write_at = 3;
    assert(vqeaf_wcn3990_ce_rri_enable(&ce, rri, dma) == -EIO);
    assert(ce._rri_ready == 0 && ce.rri_cpu == 0);
    assert(value(&f, VQEAF_WCN3990_CE_RRI_LOW) == 0u);
    assert(value(&f, VQEAF_WCN3990_CE_RRI_HIGH) == 0u);
    assert((value(&f, reg(0, VQEAF_WCN3990_CE_CTRL1)) &
            VQEAF_WCN3990_CE_CTRL1_UPD) == 0);

    init_ce(&f, &ce);
    assert(vqeaf_wcn3990_ce_rri_enable(&ce, rri, dma) == 0);
    f.fail_write_at = (int)f.write_calls + 1;
    f.fail_write_forever = 1;
    assert(vqeaf_wcn3990_ce_rri_disable(&ce) == -EIO);
    assert(ce._rri_ready == 1 && ce.rri_cpu == rri);
    f.fail_write_forever = 0;
    f.fail_write_at = 0;
    assert(vqeaf_wcn3990_ce_rri_disable(&ce) == 0);
    assert(ce._rri_ready == 0 && ce.rri_cpu == 0);
}

int main(void)
{
    test_missing_provider();
    test_init_validation();
    test_source_only_and_disable();
    test_destination_only_and_interrupts();
    test_dual_ring_and_poll();
    test_invalid_configuration();
    test_read_failure_and_offline();
    test_partial_write_rollback();
    test_32_bit_ring_aperture();
    test_quiesce_failures();
    test_rri();
    puts("wcn3990_ce_smoke: PASS");
    return 0;
}
