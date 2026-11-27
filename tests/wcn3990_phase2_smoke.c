#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
extern void *VirtualAlloc(void *,size_t,uint32_t,uint32_t);
extern int VirtualProtect(void *,size_t,uint32_t,uint32_t *);
extern int VirtualFree(void *,size_t,uint32_t);
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#include "../kernel/include/vqeaf/wcn3990_phase2.h"
#include "../system/core/include/vqeaf/driver.h"
#include "../system/core/include/vqeaf/capability.h"

typedef struct {
 void *base;
 size_t page_size;
} GuardPage;

static GuardPage guard_page_alloc(void)
{
 GuardPage g;
#ifdef _WIN32
 uint32_t old_protect;
 g.page_size=0x10000u;
 g.base=VirtualAlloc(NULL,g.page_size*2u,0x2000u|0x1000u,0x04u);
 assert(g.base);
 assert(VirtualProtect((uint8_t*)g.base+g.page_size,g.page_size,
                       0x01u,&old_protect));
#else
 long page=sysconf(_SC_PAGESIZE);
 assert(page>0);
 g.page_size=(size_t)page;
 g.base=mmap(NULL,g.page_size*2u,PROT_READ|PROT_WRITE,
             MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 assert(g.base!=MAP_FAILED);
 assert(mprotect((uint8_t*)g.base+g.page_size,g.page_size,PROT_NONE)==0);
#endif
 return g;
}

static VqeafWcn3990P2Device *guard_page_short_device(GuardPage *g)
{
 uint32_t *tail=(uint32_t*)((uint8_t*)g->base+g->page_size);
 tail[-2]=VQEAF_WCN3990_P2_ABI;
 tail[-1]=8u;
 return (VqeafWcn3990P2Device *)(void *)(tail-2);
}

static void guard_page_free(GuardPage *g)
{
#ifdef _WIN32
 assert(VirtualFree(g->base,0,0x8000u));
#else
 assert(munmap(g->base,g->page_size*2u)==0);
#endif
}

typedef struct {
 int step,fail_at,freed,irq_count,configured,configure_fail_at,disable_calls;
 int unmap_calls,regulator_disable_calls,clock_disable_calls,power_off_calls;
 int msa_release_calls,free_irq_calls;
 int rri_calls,rri_disable_calls,rri_fail_at,rri_disable_fail;
 uint16_t disable_fail_mask;
 uint8_t disable_order[VQEAF_WCN3990_CE_MAX];
 uint64_t next_dma,ce11_dst_dma,rri_dma,freed_dma[VQEAF_WCN3990_CE_MAX*2u+1u];
} Mock;
static int hit(Mock*m,int n){m->step=n;return m->fail_at==n?-EIO:0;}
static int map(void*p,uint64_t b,uint64_t s){assert(b&&s);return hit(p,1);}
static void unmap(void*p){((Mock*)p)->unmap_calls++;}
static int regs(void*p){return hit(p,2);} static void unregs(void*p){((Mock*)p)->regulator_disable_calls++;}
static int clocks(void*p){return hit(p,3);} static void unclocks(void*p){((Mock*)p)->clock_disable_calls++;}
static int icnss(void*p){return hit(p,4);} static void un_icnss(void*p){((Mock*)p)->power_off_calls++;}
static int msa(void*p,uint64_t b,uint64_t s){assert(b&&s);return hit(p,5);}
static void unmsa(void*p){((Mock*)p)->msa_release_calls++;}
static int irq(void*p,uint32_t n,uint8_t ce){Mock*m=p;assert(n==130u+ce);m->irq_count++;return 0;}
static void unirq(void*p,uint32_t n,uint8_t ce){(void)n;(void)ce;Mock*m=p;m->irq_count--;m->free_irq_calls++;}
static int dma_alloc(void*p,uint64_t n,uint64_t a,void**cpu,uint64_t*dma){
 Mock*m=p;assert((a==4||a==8)&&n);*cpu=malloc((size_t)((n+7)&~7ull));if(!*cpu)return -ENOMEM;
 m->next_dma=(m->next_dma+7)&~7ull;*dma=m->next_dma;m->next_dma+=n;return 0;
}
static void dma_free(void*p,uint64_t n,void*cpu,uint64_t dma){
 (void)n;Mock*m=p;assert(m->freed<(int)(sizeof(m->freed_dma)/sizeof(m->freed_dma[0])));
 m->freed_dma[m->freed]=dma;m->freed++;free(cpu);
}
static int ce_cfg(void*p,const VqeafWcn3990CeConfig*c,uint64_t s,uint64_t d){
 Mock*m=p;assert(c->id<12);assert((c->src_entries==0)==(s==0));assert((c->dst_entries==0)==(d==0));
 if(c->id==11)m->ce11_dst_dma=d;
 m->configured++;
 return m->configure_fail_at==m->configured?-EIO:0;
}
static int ce_disable(void*p,uint8_t ce){
 Mock*m=p;assert(ce<VQEAF_WCN3990_CE_MAX);
 assert(m->disable_calls<(int)VQEAF_WCN3990_CE_MAX);
 m->disable_order[m->disable_calls++]=ce;return (m->disable_fail_mask&(1u<<ce))?-EIO:0;
}
static int ce_rri(void*p,void*cpu,uint64_t dma){
 Mock*m=p;assert(cpu&&dma);m->rri_calls++;m->rri_dma=dma;
 return (m->rri_fail_at&&m->rri_calls>=m->rri_fail_at)?-EIO:0;
}
static int ce_rri_dis(void*p){Mock*m=p;m->rri_disable_calls++;
 return m->rri_disable_fail?-EIO:0;}
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
static VqeafWcn3990P2Device make_rri(Mock*m){
 VqeafWcn3990P2Device d=make(m);
 d.ops.ce_rri=ce_rri;d.ops.ce_rri_disable=ce_rri_dis;return d;
}
int main(void){
 uint32_t count=0;const VqeafWcn3990CeConfig*c=vqeaf_wcn3990_ce_config(&count);
 assert(c&&count==12&&c[4].src_entries==2048&&c[1].dst_entries==512&&c[6].src_entries==0);
 Mock m={.next_dma=0x100000};VqeafWcn3990P2Device d=make(&m);uint32_t id=0;
 vqeaf_driver_reset();vqeaf_cap_reset();assert(vqeaf_wcn3990_phase2_register()==0);
 assert(vqeaf_wcn3990_phase2_bind(&d,&id)==0);
 assert(vqeaf_wcn3990_phase2_state()==VQEAF_WCN_P2_CE);
 assert(m.irq_count==12&&m.configured==11);
 VqeafCapability cap;assert(vqeaf_cap_get("radio.wifi",&cap)==0&&cap.state==VQEAF_CAP_PRESENT);
 assert(vqeaf_cap_get("radio.wifi.snoc_ce",&cap)==0&&cap.state==VQEAF_CAP_READY);
 {
  Mock other_mock={.next_dma=0x200000};VqeafWcn3990P2Device other=make(&other_mock);
  uint32_t other_id=UINT32_MAX;
  for(uint32_t attempt=0;attempt<VQEAF_DRIVER_MAX;attempt++){
   assert(vqeaf_wcn3990_phase2_bind(&other,&other_id)==-EBUSY);
   assert(other_id==UINT32_MAX);
  }
  assert(other_mock.step==0&&other_mock.irq_count==0&&other_mock.configured==0);
  assert(vqeaf_wcn3990_phase2_state()==VQEAF_WCN_P2_CE);
  assert(vqeaf_wcn3990_phase2_error()==0);
  assert(m.irq_count==12&&m.configured==11&&m.freed==0&&m.disable_calls==0);
  assert(vqeaf_cap_get("radio.wifi.snoc_ce",&cap)==0&&cap.state==VQEAF_CAP_READY);
 }
 assert(vqeaf_driver_unbind(id)==0);assert(vqeaf_wcn3990_phase2_state()==VQEAF_WCN_P2_REMOVED);
 assert(m.irq_count==0&&m.freed==12&&m.disable_calls==11);

 m=(Mock){.next_dma=0x100000};d=make(&m);d.resources.ce_irq[11]=130;
 vqeaf_driver_reset();vqeaf_cap_reset();assert(vqeaf_wcn3990_phase2_register()==0);
 assert(vqeaf_wcn3990_phase2_bind(&d,&id)==-EINVAL);assert(m.step==0);

 {
  GuardPage guard=guard_page_alloc();
  VqeafWcn3990P2Device *short_device=guard_page_short_device(&guard);
  uint32_t short_id=UINT32_MAX;
  vqeaf_driver_reset();vqeaf_cap_reset();assert(vqeaf_wcn3990_phase2_register()==0);
  assert(vqeaf_wcn3990_phase2_bind(short_device,&short_id)==-EINVAL);
  assert(vqeaf_wcn3990_phase2_error()==-EINVAL);
  assert(vqeaf_wcn3990_phase2_state()==VQEAF_WCN_P2_FAILED);
  vqeaf_driver_reset();
  guard_page_free(&guard);
 }

 m=(Mock){.fail_at=4,.next_dma=0x100000};d=make(&m);
 vqeaf_driver_reset();vqeaf_cap_reset();assert(vqeaf_wcn3990_phase2_register()==0);
 assert(vqeaf_wcn3990_phase2_bind(&d,&id)==-EIO);
 assert(vqeaf_wcn3990_phase2_state()==VQEAF_WCN_P2_FAILED);
 assert(m.irq_count==0&&m.freed==0);

 m=(Mock){.configure_fail_at=3,.next_dma=0x100000};d=make(&m);
 vqeaf_driver_reset();vqeaf_cap_reset();assert(vqeaf_wcn3990_phase2_register()==0);
 assert(vqeaf_wcn3990_phase2_bind(&d,&id)==-EIO);
 assert(vqeaf_wcn3990_phase2_state()==VQEAF_WCN_P2_FAILED);
 assert(m.irq_count==0&&m.freed==3&&m.disable_calls==3);

 m=(Mock){.next_dma=0x100000};d=make_rri(&m);
 vqeaf_driver_reset();vqeaf_cap_reset();assert(vqeaf_wcn3990_phase2_register()==0);
 assert(vqeaf_wcn3990_phase2_bind(&d,&id)==0);
 assert(m.rri_calls==1&&m.rri_dma>=0x100000u&&(m.rri_dma&3u)==0);
 assert(m.configured==11&&m.irq_count==12&&m.rri_disable_calls==0);
 {
  uint64_t rri_seen=m.rri_dma;
  assert(vqeaf_driver_unbind(id)==0);
  assert(m.rri_disable_calls==1&&m.freed==13);
  assert(m.freed_dma[12]==rri_seen);
  assert(m.free_irq_calls==12);
 }

 m=(Mock){.next_dma=0x100000,.rri_fail_at=1};d=make_rri(&m);
 vqeaf_driver_reset();vqeaf_cap_reset();assert(vqeaf_wcn3990_phase2_register()==0);
 assert(vqeaf_wcn3990_phase2_bind(&d,&id)==-EIO);
 assert(vqeaf_wcn3990_phase2_state()==VQEAF_WCN_P2_FAILED);
 assert(m.rri_calls==1&&m.rri_disable_calls==1&&m.configured==0);
 assert(m.freed==1&&m.free_irq_calls==12&&m.msa_release_calls==1);

 m=(Mock){.next_dma=0x100000};d=make(&m);d.ops.ce_rri=ce_rri;
 vqeaf_driver_reset();vqeaf_cap_reset();assert(vqeaf_wcn3990_phase2_register()==0);
 assert(vqeaf_wcn3990_phase2_bind(&d,&id)==-EINVAL);
 assert(m.rri_calls==0&&m.configured==0&&m.free_irq_calls==12);

 m=(Mock){.next_dma=0x100000};d=make(&m);
 vqeaf_driver_reset();vqeaf_cap_reset();assert(vqeaf_wcn3990_phase2_register()==0);
 assert(vqeaf_wcn3990_phase2_bind(&d,&id)==0);
 m.disable_fail_mask=1u<<10;
 assert(vqeaf_driver_unbind(id)==-EIO);
 assert(vqeaf_wcn3990_phase2_state()==VQEAF_WCN_P2_FAILED);
 assert(vqeaf_wcn3990_phase2_error()==-EIO);
 assert(m.disable_calls==2&&m.disable_order[0]==11&&m.disable_order[1]==10);
 assert(m.freed==1&&m.freed_dma[0]==m.ce11_dst_dma);
 assert(m.irq_count==12&&m.free_irq_calls==0);
 assert(m.msa_release_calls==0&&m.power_off_calls==0&&m.clock_disable_calls==0);
 assert(m.regulator_disable_calls==0&&m.unmap_calls==0);
 vqeaf_driver_reset();vqeaf_cap_reset();assert(vqeaf_wcn3990_phase2_register()==0);
 {
  uint32_t blocked_id=UINT32_MAX;
  for(uint32_t attempt=0;attempt<VQEAF_DRIVER_MAX;attempt++){
   assert(vqeaf_wcn3990_phase2_bind(&d,&blocked_id)==-EBUSY);
   assert(blocked_id==UINT32_MAX);
  }
  {
   VqeafDriverInstance rejected;
   assert(vqeaf_driver_get(0,&rejected)==-1);
  }
 }
 assert(m.disable_calls==2&&m.freed==1&&m.irq_count==12);
 assert(m.free_irq_calls==0&&m.msa_release_calls==0&&m.power_off_calls==0);
 assert(m.clock_disable_calls==0&&m.regulator_disable_calls==0&&m.unmap_calls==0);

 return 0;
 }
