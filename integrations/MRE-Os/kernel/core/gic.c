#include "mre_gic.h"
#include "mre_arch.h"
#include <string.h>
#define GICD_CTLR 0x0000u
#define GICD_TYPER 0x0004u
#define GICD_IGROUPR 0x0080u
#define GICD_ISENABLER 0x0100u
#define GICD_ICENABLER 0x0180u
#define GICD_IPRIORITYR 0x0400u
#define GICD_CTLR_ENABLE_G1NS (1u<<1)
#define GICD_CTLR_ARE_NS (1u<<4)
#define GICD_CTLR_RWP (1u<<31)
#define GICR_WAKER 0x0014u
#define GICR_TYPER 0x0008u
#define GICR_SGI_BASE 0x10000u
#define GICR_IGROUPR0 (GICR_SGI_BASE+0x0080u)
#define GICR_ISENABLER0 (GICR_SGI_BASE+0x0100u)
#define GICR_ICENABLER0 (GICR_SGI_BASE+0x0180u)
#define GICR_IPRIORITYR (GICR_SGI_BASE+0x0400u)
#define GICR_WAKER_PROCESSOR_SLEEP (1u<<1)
#define GICR_WAKER_CHILDREN_ASLEEP (1u<<2)
#define GICC_CTLR 0x0000u
#define GICC_PMR 0x0004u
#define GICC_IAR 0x000Cu
#define GICC_EOIR 0x0010u
#define GIC_SPURIOUS_ID 1023u
#define GIC_MAX_IRQS 1024u
typedef struct irq_slot { mre_irq_handler_t handler; void *context; } irq_slot_t;
static mre_gic_config_t g_cfg;
static irq_slot_t g_handlers[GIC_MAX_IRQS];
static uintptr_t g_redist_cpu;
static int g_ready;
static volatile uint32_t *reg32(uintptr_t base, uint32_t off)
{ return (volatile uint32_t *)(base + off); }
static int wait_clear(uintptr_t base, uint32_t off, uint32_t mask)
{
    uint32_t n;
    for(n=0;n<100000u;++n) if((*reg32(base,off)&mask)==0u) return 1;
    return 0;
}
uint32_t mre_gic_irq_id(uint32_t iar) { return iar & 0x3FFu; }
mre_status_t mre_gic_register_handler(uint32_t irq, mre_irq_handler_t fn, void *ctx)
{
    if(irq>=GIC_MAX_IRQS || fn==NULL) return MRE_E_INVALID;
    g_handlers[irq].handler=fn; g_handlers[irq].context=ctx; return MRE_OK;
}
mre_status_t mre_gic_configure(const mre_gic_config_t *c)
{
    uint32_t i, lines;
    if(c==NULL || c->distributor_base==0u ||
       (c->version!=MRE_GIC_V2 && c->version!=MRE_GIC_V3)) return MRE_E_INVALID;
    if(c->version==MRE_GIC_V3 && (c->redistributor_base==0u || c->redistributor_size<0x20000u))
        return MRE_E_INVALID;
    g_ready=0; g_cfg=*c; g_redist_cpu=0;
    memset(g_handlers,0,sizeof(g_handlers));
    if(c->version==MRE_GIC_V3) {
#if defined(__aarch64__)
        uint64_t mpidr, affinity, typer, stride=0x20000u;
        uintptr_t r;
        __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
        affinity=(((mpidr>>32)&0xFFu)<<24)|(((mpidr>>16)&0xFFu)<<16)|
                 (((mpidr>>8)&0xFFu)<<8)|(mpidr&0xFFu);
        for(r=c->redistributor_base;r<c->redistributor_base+c->redistributor_size;r+=stride) {
            typer=*(volatile uint64_t *)(r+GICR_TYPER);
            if((uint32_t)(typer>>32)==(uint32_t)affinity){g_redist_cpu=r;break;}
            stride=((typer&(1ull<<1))!=0u)?0x40000u:0x20000u;
            if((typer&(1ull<<4))!=0u) break;
        }
        if(g_redist_cpu==0u) return MRE_E_NOT_FOUND;
        {
            volatile uint32_t *w=reg32(g_redist_cpu,GICR_WAKER);
            *w &= ~GICR_WAKER_PROCESSOR_SLEEP;
            mre_arch_dsb_sy();
            if(!wait_clear(g_redist_cpu,GICR_WAKER,GICR_WAKER_CHILDREN_ASLEEP))
                return MRE_E_BUSY;
        }
        *reg32(g_redist_cpu,GICR_IGROUPR0)=0xFFFFFFFFu;
        for(i=0;i<32u;i+=4u)
            *reg32(g_redist_cpu,GICR_IPRIORITYR+i)=0xA0A0A0A0u;
        *reg32(g_redist_cpu,GICR_ICENABLER0)=0xFFFFFFFFu;
#else
        return MRE_E_UNSUPPORTED;
#endif
    }
    *reg32(c->distributor_base,GICD_CTLR)=0u;
    if(!wait_clear(c->distributor_base,GICD_CTLR,GICD_CTLR_RWP)) return MRE_E_BUSY;
    lines=((*reg32(c->distributor_base,GICD_TYPER)&0x1Fu)+1u)*32u;
    if(lines>GIC_MAX_IRQS) lines=GIC_MAX_IRQS;
    if(c->interrupt_count!=0u && c->interrupt_count<lines) lines=c->interrupt_count;
    g_cfg.interrupt_count=lines;
    for(i=(c->version==MRE_GIC_V3)?32u:0u;i<lines;i+=32u) {
        *reg32(c->distributor_base,GICD_IGROUPR+i/8u)=0xFFFFFFFFu;
        *reg32(c->distributor_base,GICD_ICENABLER+i/8u)=0xFFFFFFFFu;
    }
    for(i=(c->version==MRE_GIC_V3)?32u:0u;i<lines;i+=4u)
        *reg32(c->distributor_base,GICD_IPRIORITYR+i)=0xA0A0A0A0u;
    mre_arch_dsb_sy();
    if(c->version==MRE_GIC_V3) {
        *reg32(c->distributor_base,GICD_CTLR)=GICD_CTLR_ARE_NS;
        if(!wait_clear(c->distributor_base,GICD_CTLR,GICD_CTLR_RWP)) return MRE_E_BUSY;
    }
    *reg32(c->distributor_base,GICD_CTLR)=GICD_CTLR_ENABLE_G1NS|
        ((c->version==MRE_GIC_V3)?GICD_CTLR_ARE_NS:0u);
    if(!wait_clear(c->distributor_base,GICD_CTLR,GICD_CTLR_RWP)) return MRE_E_BUSY;
#if defined(__aarch64__)
    if(c->version==MRE_GIC_V3) {
        uint64_t sre=1u, pmr=0xFFu, bpr=0u, grp=1u, sre_readback;
        __asm__ volatile("msr icc_sre_el1, %0\nisb"::"r"(sre):"memory");
        __asm__ volatile("mrs %0, icc_sre_el1":"=r"(sre_readback));
        if((sre_readback&1u)==0u) return MRE_E_UNSUPPORTED;
        __asm__ volatile("msr icc_pmr_el1, %0"::"r"(pmr):"memory");
        __asm__ volatile("msr icc_bpr1_el1, %0"::"r"(bpr):"memory");
        __asm__ volatile("msr icc_igrpen1_el1, %0\nisb"::"r"(grp):"memory");
    } else
#endif
    if(c->version==MRE_GIC_V2) {
        if(c->cpu_interface_base==0u) return MRE_E_INVALID;
        *reg32(c->cpu_interface_base,GICC_PMR)=0xFFu;
        *reg32(c->cpu_interface_base,GICC_CTLR)=2u;
    }
    mre_arch_dsb_sy(); g_ready=1; return MRE_OK;
}
mre_status_t mre_gic_enable_irq(uint32_t irq)
{
    if(!g_ready || irq>=g_cfg.interrupt_count) return MRE_E_INVALID;
    if(g_cfg.version==MRE_GIC_V3 && irq<32u)
        *reg32(g_redist_cpu,GICR_ISENABLER0)=1u<<irq;
    else
        *reg32(g_cfg.distributor_base,GICD_ISENABLER+(irq/32u)*4u)=1u<<(irq%32u);
    mre_arch_dsb_sy(); return MRE_OK;
}
uint32_t mre_gic_acknowledge(void)
{
    if(!g_ready) return GIC_SPURIOUS_ID;
#if defined(__aarch64__)
    if(g_cfg.version==MRE_GIC_V3) {
        uint64_t v; __asm__ volatile("mrs %0, icc_iar1_el1":"=r"(v)); return mre_gic_irq_id((uint32_t)v);
    }
#endif
    return mre_gic_irq_id(*reg32(g_cfg.cpu_interface_base,GICC_IAR));
}
void mre_gic_end(uint32_t irq)
{
    if(!g_ready || irq==GIC_SPURIOUS_ID) return;
#if defined(__aarch64__)
    if(g_cfg.version==MRE_GIC_V3) {
        uint64_t v=irq; __asm__ volatile("msr icc_eoir1_el1, %0\nisb"::"r"(v):"memory"); return;
    }
#endif
    *reg32(g_cfg.cpu_interface_base,GICC_EOIR)=irq;
}
void mre_gic_dispatch(void)
{
    uint32_t irq=mre_gic_acknowledge();
    if(irq==GIC_SPURIOUS_ID) return;
    if(irq<GIC_MAX_IRQS && g_handlers[irq].handler)
        g_handlers[irq].handler(irq,g_handlers[irq].context);
    mre_gic_end(irq);
}
