#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/include/vqeaf/wcn3990_phase2.h"
#include "../system/core/include/vqeaf/driver.h"
#include "../system/core/include/vqeaf/capability.h"

typedef struct { int step,fail_at,freed,irq_count,configured; uint64_t next_dma; } Mock;
static int hit(Mock*m,int n){m->step=n;return m->fail_at==n?-EIO:0;}
static int map(void*p,uint64_t b,uint64_t s){assert(b&&s);return hit(p,1);}
static void unmap(void*p){(void)p;}
static int regs(void*p){return hit(p,2);} static void unregs(void*p){(void)p;}
static int clocks(void*p){return hit(p,3);} static void unclocks(void*p){(void)p;}
static int icnss(void*p){return hit(p,4);} static void un_icnss(void*p){(void)p;}
static int msa(void*p,uint64_t b,uint64_t s){assert(b&&s);return hit(p,5);}
static void unmsa(void*p){(void)p;}
static int irq(void*p,uint32_t n,uint8_t ce){Mock*m=p;assert(n==130u+ce);m->irq_count++;return 0;}
static void unirq(void*p,uint32_t n,uint8_t ce){(void)n;(void)ce;((Mock*)p)->irq_count--;}
static int dma_alloc(void*p,uint64_t n,uint64_t a,void**cpu,uint64_t*dma){
 Mock*m=p;assert(a==8&&n);*cpu=malloc((size_t)((n+7)&~7ull));if(!*cpu)return -ENOMEM;
 m->next_dma=(m->next_dma+7)&~7ull;*dma=m->next_dma;m->next_dma+=n;return 0;
}
static void dma_free(void*p,uint64_t n,void*cpu,uint64_t dma){(void)n;(void)dma;free(cpu);((Mock*)p)->freed++;}
static int ce_cfg(void*p,const VqeafWcn3990CeConfig*c,uint64_t s,uint64_t d){
 Mock*m=p;assert(c->id<12);assert((c->src_entries==0)==(s==0));assert((c->dst_entries==0)==(d==0));m->configured++;return 0;
}
static void ce_disable(void*p,uint8_t ce){(void)p;(void)ce;}
static VqeafWcn3990P2Device make(Mock*m){
 VqeafWcn3990P2Device d;memset(&d,0,sizeof(d));d.abi=VQEAF_WCN3990_P2_ABI;d.size=sizeof(d);
 d.resources.snoc_base=0x18800000;d.resources.snoc_size=0x800000;
 d.resources.msa_base=0x90000000;d.resources.msa_size=0x100000;
 d.resources.ce_irq_count=12;for(uint32_t i=0;i<12;i++)d.resources.ce_irq[i]=130+i;
 d.ops.ctx=m;d.ops.map_snoc=map;d.ops.unmap_snoc=unmap;
 d.ops.regulators_enable=regs;d.ops.regulators_disable=unregs;
 d.ops.clocks_enable=clocks;d.ops.clocks_disable=unclocks;
 d.ops.icnss_power_on=icnss;d.ops.icnss_power_off=un_icnss;
 d.ops.msa_prepare=msa;d.ops.msa_release=unmsa;
 d.ops.request_irq=irq;d.ops.free_irq=unirq;
 d.ops.dma_alloc=dma_alloc;d.ops.dma_free=dma_free;
 d.ops.ce_configure=ce_cfg;d.ops.ce_disable=ce_disable;return d;
}
int main(void){
 uint32_t count=0;const VqeafWcn3990CeConfig*c=vqeaf_wcn3990_ce_config(&count);
 assert(c&&count==12&&c[4].src_entries==2048&&c[1].dst_entries==512&&c[6].src_entries==0);
 Mock m={0,0,0,0,0,0x100000};VqeafWcn3990P2Device d=make(&m);uint32_t id=0;
 vqeaf_driver_reset();vqeaf_cap_reset();assert(vqeaf_wcn3990_phase2_register()==0);
 assert(vqeaf_wcn3990_phase2_bind(&d,&id)==0);
 assert(vqeaf_wcn3990_phase2_state()==VQEAF_WCN_P2_CE);
 assert(m.irq_count==12&&m.configured==11);
 VqeafCapability cap;assert(vqeaf_cap_get("radio.wifi",&cap)==0&&cap.state==VQEAF_CAP_PRESENT);
 assert(vqeaf_cap_get("radio.wifi.snoc_ce",&cap)==0&&cap.state==VQEAF_CAP_READY);
 assert(vqeaf_driver_unbind(id)==0);assert(vqeaf_wcn3990_phase2_state()==VQEAF_WCN_P2_REMOVED);
 assert(m.irq_count==0&&m.freed>0);

 m=(Mock){0,0,0,0,0,0x100000};d=make(&m);d.resources.ce_irq[11]=130;
 vqeaf_driver_reset();vqeaf_cap_reset();assert(vqeaf_wcn3990_phase2_register()==0);
 assert(vqeaf_wcn3990_phase2_bind(&d,&id)==-EINVAL);assert(m.step==0);

 m=(Mock){0,4,0,0,0,0x100000};d=make(&m);
 vqeaf_driver_reset();vqeaf_cap_reset();assert(vqeaf_wcn3990_phase2_register()==0);
 assert(vqeaf_wcn3990_phase2_bind(&d,&id)==-EIO);
 assert(vqeaf_wcn3990_phase2_state()==VQEAF_WCN_P2_FAILED);
 assert(m.irq_count==0&&m.freed==0);
 return 0;
}
