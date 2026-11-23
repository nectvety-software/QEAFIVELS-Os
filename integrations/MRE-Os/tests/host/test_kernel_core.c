#include "mre_fdt.h"
#include "mre_mmu.h"
#include "mre_timer.h"
#include "mre_uart.h"
#include "mre_violet.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t blob[2048], structure[1024], strings[256];
static uint32_t sp, stp;
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static void put64(uint8_t *p,uint64_t v){put32(p,(uint32_t)(v>>32));put32(p+4u,(uint32_t)v);}
static uint32_t add_string(const char *s)
{ uint32_t off=stp;size_t n=strlen(s)+1u;memcpy(strings+stp,s,n);stp+=(uint32_t)n;return off; }
static void token(uint32_t v){put32(structure+sp,v);sp+=4u;}
static void node(const char *name)
{
    size_t n=strlen(name)+1u;token(1u);memcpy(structure+sp,name,n);sp+=(uint32_t)n;
    while((sp&3u)!=0u)structure[sp++]=0;
}
static void prop(uint32_t nameoff,const void *data,uint32_t len)
{
    token(3u);put32(structure+sp,len);put32(structure+sp+4u,nameoff);sp+=8u;
    if(len)memcpy(structure+sp,data,len);sp+=len;
    while((sp&3u)!=0u)structure[sp++]=0;
}
static void u32prop(uint32_t nameoff,uint32_t value)
{ uint8_t b[4];put32(b,value);prop(nameoff,b,4u); }
static void make_fdt(void)
{
    uint32_t n_ac=add_string("#address-cells"),n_sc=add_string("#size-cells");
    uint32_t n_compat=add_string("compatible"),n_reg=add_string("reg");
    uint32_t n_irq=add_string("interrupts"),n_dtype=add_string("device_type");
    uint32_t n_size=add_string("size"),n_ranges=add_string("ranges");
    uint8_t reg[32],irqs[48],dyn_size[8]={0,0,0,0,0,0x10,0,0};
    uint32_t so=72u,stro;
    memset(blob,0,sizeof(blob));memset(structure,0,sizeof(structure));sp=0;
    node("");prop(n_compat,"xiaomi,violet",14u);u32prop(n_ac,2u);u32prop(n_sc,2u);
    node("intc@17a00000");prop(n_compat,"arm,gic-v3",11u);
    put32(reg,0u);put32(reg+4,0x17A00000u);put32(reg+8,0u);put32(reg+12,0x10000u);
    put32(reg+16,0u);put32(reg+20,0x17A60000u);put32(reg+24,0u);put32(reg+28,0x100000u);
    prop(n_reg,reg,32u);token(2u);
    node("serial@880000");prop(n_compat,"qcom,msm-geni-console",22u);
    put32(reg,0u);put32(reg+4,0x00880000u);put32(reg+8,0u);put32(reg+12,0x4000u);
    prop(n_reg,reg,16u);
    put32(irqs,0u);put32(irqs+4,0x259u);put32(irqs+8,0u);prop(n_irq,irqs,12u);token(2u);
    node("timer");prop(n_compat,"arm,armv8-timer",16u);
    put32(irqs,1u);put32(irqs+4,1u);put32(irqs+8,0xF08u);
    put32(irqs+12,1u);put32(irqs+16,2u);put32(irqs+20,0xF08u);
    put32(irqs+24,1u);put32(irqs+28,3u);put32(irqs+32,0xF08u);
    put32(irqs+36,1u);put32(irqs+40,0u);put32(irqs+44,0xF08u);
    prop(n_irq,irqs,48u);token(2u);
    node("memory@80000000");prop(n_dtype,"memory",7u);
    put32(reg,0u);put32(reg+4,0x80000000u);put32(reg+8,0u);put32(reg+12,0x10000000u);
    prop(n_reg,reg,16u);token(2u);
    node("reserved-memory");u32prop(n_ac,2u);u32prop(n_sc,2u);prop(n_ranges,NULL,0u);
    node("secure@90000000");put32(reg,0u);put32(reg+4,0x90000000u);put32(reg+8,0u);put32(reg+12,0x100000u);
    prop(n_reg,reg,16u);token(2u);
    node("dynamic-pool");prop(n_size,dyn_size,8u);token(2u);
    token(2u);token(2u);token(9u);
    stro=so+sp;
    put64(blob+40u,0x91000000ull);put64(blob+48u,0x100000ull);
    put64(blob+56u,0u);put64(blob+64u,0u);
    put32(blob,0xD00DFEEDu);put32(blob+4,stro+stp);put32(blob+8,so);
    put32(blob+12,stro);put32(blob+16,40u);put32(blob+20,17u);
    put32(blob+24,16u);put32(blob+28,0u);put32(blob+32,stp);put32(blob+36,sp);
    memcpy(blob+so,structure,sp);memcpy(blob+stro,strings,stp);
}
typedef struct fake_uart { uint32_t regs[1024];uint32_t fifo[128];size_t fifo_count; } fake_uart_t;
static uint32_t fake_read(void *ctx,uint32_t off)
{
    fake_uart_t *f=(fake_uart_t *)ctx;
    if(off==0x610u)return (1u<<30)|1u;
    if(off==0x800u)return 0u;
    return f->regs[off/4u];
}
static void fake_write(void *ctx,uint32_t off,uint32_t v)
{
    fake_uart_t *f=(fake_uart_t *)ctx;f->regs[off/4u]=v;
    if(off==0x700u && f->fifo_count<128u)f->fifo[f->fifo_count++]=v;
}
static uint64_t lookup_descriptor(uint64_t va);
static void test_fdt_and_violet(void)
{
    mre_fdt_t f;mre_fdt_node_t n;mre_fdt_reg_t r;mre_violet_boot_info_t info;uint32_t cursor=0;
    make_fdt();assert(mre_fdt_open(&f,blob,sizeof(blob))==MRE_OK);
    assert(mre_fdt_find_compatible(&f,"arm,gic-v3",&cursor,&n)==MRE_OK);
    assert(mre_fdt_get_reg(&f,n,0u,&r)==MRE_OK);
    assert(r.address==0x17A00000ull && r.size==0x10000ull);
    assert(mre_fdt_find_compatible(&f,"qcom,msm-geni-console",&cursor,&n)==MRE_OK);
    assert(mre_fdt_get_reg(&f,n,0u,&r)==MRE_OK);
    assert(r.address==0x00880000ull && r.size==0x4000ull);
    assert(mre_fdt_find_compatible(&f,"missing",&cursor,&n)==MRE_E_NOT_FOUND);
    cursor=0;assert(mre_fdt_find_property_string(&f,"device_type","memory",&cursor,&n)==MRE_OK);
    assert(mre_fdt_get_reg(&f,n,0u,&r)==MRE_OK && r.address==0x80000000ull);
    assert(mre_violet_discover(&f,&info)==MRE_OK);
    assert(info.board_identity_verified==1u);
    assert(info.gic_found && info.gic.version==MRE_GIC_V3);
    assert(info.gic.distributor_base==0x17A00000ull);
    assert(info.gic.redistributor_base==0x17A60000ull);
    assert(info.uart_found && info.uart_base==0x00880000ull && info.uart_irq==633u);
    assert(info.timer_found && info.timer_irq==18u);
    assert(info.memory_region_count==1u && info.reserved_region_count==2u);
    assert(info.dynamic_reserved_count==1u && info.memory_map_valid==0u);
    assert(mre_violet_mmu_prerequisites(&info)==MRE_E_UNSUPPORTED);
    {
        mre_violet_boot_info_t safe=info;
        mre_violet_kernel_layout_t layout={
            0x80081000ull,0x800DE330ull,
            0x80081000ull,0x80087000ull,
            0x80087000ull,0x80088000ull
        };
        uint64_t d;
        safe.memory_map_valid=1u;safe.dynamic_reserved_count=0u;
        safe.memory[0].base=0x80000000ull;safe.memory[0].size=0x20000000ull;
        safe.reserved[0].base=0x91000000ull;safe.reserved[0].size=0x100000ull;
        safe.reserved[1].base=0x90000000ull;safe.reserved[1].size=0x100000ull;
        assert(mre_violet_mmu_prerequisites(&safe)==MRE_OK);
        assert(mre_violet_build_page_tables(&safe,&layout)==MRE_OK);
        d=lookup_descriptor(0x80000000ull);assert((d&1u)!=0u && ((d>>2)&7u)==1u);
        d=lookup_descriptor(0x80081000ull);
        assert((d&1u)!=0u && (d&(1ull<<7))!=0u && (d&(1ull<<53))==0u);
        d=lookup_descriptor(0x80090000ull);
        assert((d&1u)!=0u && (d&(1ull<<7))==0u && (d&(1ull<<53))!=0u);
        assert(lookup_descriptor(0x90000000ull)==0u);
        assert(lookup_descriptor(0x91000000ull)==0u);
        d=lookup_descriptor(0x00880000ull);assert((d&1u)!=0u && ((d>>2)&7u)==0u);
    }
    assert(mre_fdt_open(&f,blob,12u)==MRE_E_INVALID);
}
static uint64_t lookup_descriptor(uint64_t va)
{
    const uint64_t *root=mre_mmu_root_table();
    uint64_t d=root[(va>>39)&511u];
    uint64_t *l1,*l2,*l3;
    if((d&1u)==0u)return 0u;
    l1=(uint64_t *)(uintptr_t)(d&0x0000FFFFFFFFF000ull);
    d=l1[(va>>30)&511u];if((d&1u)==0u)return 0u;
    if((d&3u)!=3u)return d;
    l2=(uint64_t *)(uintptr_t)(d&0x0000FFFFFFFFF000ull);
    d=l2[(va>>21)&511u];if((d&1u)==0u)return 0u;
    if((d&3u)!=3u)return d;
    l3=(uint64_t *)(uintptr_t)(d&0x0000FFFFFFFFF000ull);
    return l3[(va>>12)&511u];
}
static void test_mmu(void)
{
    mre_mmu_region_t r={0x40000000ull,0x80000000ull,8192u,MRE_MEMORY_NORMAL_WB,1u,0u};
    const uint64_t *root;uint64_t *l1,*l2,*l3;uint64_t d;
    assert(mre_mmu_reset()==MRE_OK);assert(mre_mmu_map_range(&r)==MRE_OK);
    assert(mre_mmu_tables_used()==3u);root=mre_mmu_root_table();
    d=root[(r.virtual_address>>39)&511u];l1=(uint64_t *)(uintptr_t)(d&0x0000FFFFFFFFF000ull);
    d=l1[(r.virtual_address>>30)&511u];l2=(uint64_t *)(uintptr_t)(d&0x0000FFFFFFFFF000ull);
    d=l2[(r.virtual_address>>21)&511u];l3=(uint64_t *)(uintptr_t)(d&0x0000FFFFFFFFF000ull);
    assert((l3[(r.virtual_address>>12)&511u]&0x0000FFFFFFFFF000ull)==r.physical_address);
    r.writable=0u;r.executable=1u;assert(mre_mmu_map_range(&r)==MRE_OK);
    assert((l3[(r.virtual_address>>12)&511u]&(1ull<<7))!=0u);
    r.executable=0u;r.virtual_address++;assert(mre_mmu_map_range(&r)==MRE_E_INVALID);

    {
        mre_mmu_region_t ram={0x80000000ull,0x80000000ull,0x100000000ull,
                              MRE_MEMORY_NORMAL_WB,1u,0u};
        mre_mmu_region_t text={0x80081000ull,0x80081000ull,4096u,
                               MRE_MEMORY_NORMAL_WB,0u,1u};
        assert(mre_mmu_reset()==MRE_OK);
        assert(mre_mmu_map_range(&ram)==MRE_OK);
        assert(mre_mmu_tables_used()==1u);
        assert(mre_mmu_map_range(&text)==MRE_OK);
        assert(mre_mmu_tables_used()==3u);
        assert(mre_mmu_map_range(&ram)==MRE_OK);
        assert(mre_mmu_tables_used()==3u);
    }
}
static void test_timer_math(void)
{
    assert(mre_timer_ticks_from_frequency(19200000u,1000000u)==19200000u);
    assert(mre_timer_ticks_from_frequency(32768u,1000000u)==32768u);
    assert(mre_timer_ticks_from_frequency(19200000u,10000u)==192000u);
    assert(mre_timer_ticks_from_frequency(0u,1000u)==0u);
    assert(mre_timer_ticks_from_frequency(UINT64_MAX,1000000000000ull)==UINT64_MAX);
}
static void test_uart(void)
{
    fake_uart_t f;mre_uart_geni_t u;mre_uart_bus_t bus;const char *msg="1234567890123\nZ";
    memset(&f,0,sizeof(f));memset(&u,0,sizeof(u));f.regs[0x48u/4u]=1u;
    bus.read32=fake_read;bus.write32=fake_write;bus.context=&f;
    assert(mre_uart_geni_attach(&u,&bus,16u,4u,100u,1,0,1)==MRE_E_PERMISSION);
    assert(mre_uart_geni_attach(&u,&bus,16u,4u,100u,1,1,1)==MRE_OK);
    assert(mre_uart_geni_write(&u,msg,strlen(msg))==MRE_OK);
    assert(f.regs[0x270u/4u]==16u && f.fifo_count==16u);
    assert(f.fifo[13]=='\r' && f.fifo[14]=='\n' && f.fifo[15]=='Z');
}
int main(void)
{
    test_fdt_and_violet();test_mmu();test_timer_math();test_uart();
    puts("MRE AArch64 core host tests: PASS");return 0;
}
