/* SPDX-License-Identifier: MIT */
/*
 * Native WCN3990 Phase 2: SNOC power/resource lifecycle + CE ring allocation.
 * No QMI, WLFW, firmware boot, or network interface is started here.
 * All board-specific register, regulator, clock, GIC and DMA operations are
 * supplied by a future Violet platform backend populated from the real DTB.
 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "vqeaf/wcn3990_phase2.h"
#include "vqeaf/wcn3990_ce.h"
#include "../../system/core/include/vqeaf/driver.h"
#include "../../system/core/include/vqeaf/capability.h"

#define CE_ATTR_DIS_INTR 8u
static const VqeafWcn3990CeConfig ce_cfg[VQEAF_WCN3990_CE_MAX] = {
 {0,16,0,2048,0}, {1,0,512,2048,0}, {2,0,64,2048,0},
 {3,32,0,2048,0}, {4,2048,0,256,CE_ATTR_DIS_INTR},
 {5,0,512,512,0}, {6,0,0,0,0}, {7,2,2,2048,0},
 {8,0,128,2048,0}, {9,0,512,2048,0},
 {10,0,512,2048,0}, {11,0,512,2048,0}
};
typedef struct {
 void *src_cpu,*dst_cpu;
 uint64_t src_dma,dst_dma,src_bytes,dst_bytes;
 uint8_t configured;
} CeRuntime;
static CeRuntime ce_rt[VQEAF_WCN3990_CE_MAX];
static VqeafWcn3990P2Device *active;
static uint16_t irq_registered;
static void *rri_cpu;
static uint64_t rri_dma, rri_bytes;
static VqeafWcn3990P2State state=VQEAF_WCN_P2_OFF;
static int last_error;
static int quiesce_pending;

const VqeafWcn3990CeConfig *vqeaf_wcn3990_ce_config(uint32_t *count)
{
 if(count)*count=VQEAF_WCN3990_CE_MAX;
 return ce_cfg;
}
static int valid(const VqeafWcn3990P2Device *d)
{
 const VqeafWcn3990P2Resources *r;
 const VqeafWcn3990P2Ops *o;
 if(!d||d->abi!=VQEAF_WCN3990_P2_ABI||d->size<sizeof(*d))return 0;
 r=&d->resources;
 o=&d->ops;
 if(!r->snoc_base||r->snoc_size<0x1000||r->snoc_base>UINT64_MAX-r->snoc_size)return 0;
 if(!r->msa_base||!r->msa_size||r->msa_base>UINT64_MAX-r->msa_size)return 0;
 if(r->ce_irq_count!=VQEAF_WCN3990_CE_MAX)return 0;
 for(uint32_t i=0;i<r->ce_irq_count;i++){
  if(r->ce_irq[i]<32)return 0;
  for(uint32_t j=0;j<i;j++)if(r->ce_irq[i]==r->ce_irq[j])return 0;
 }
 return o->map_snoc&&o->unmap_snoc&&o->regulators_enable&&o->regulators_disable&&
  o->clocks_enable&&o->clocks_disable&&o->icnss_power_on&&o->icnss_power_off&&
  o->msa_prepare&&o->msa_release&&o->request_irq&&o->free_irq&&
  o->dma_alloc&&o->dma_free&&o->ce_configure&&o->ce_disable;
}
static void zero_mem(void *p,uint64_t n)
{
 volatile uint8_t *b=(volatile uint8_t *)p;
 for(uint64_t i=0;i<n;i++)b[i]=0;
}
static int release_ce(VqeafWcn3990P2Device *d)
{
 for(int i=(int)VQEAF_WCN3990_CE_MAX-1;i>=0;i--){
  CeRuntime *c=&ce_rt[i];int rc;
  if(c->configured){
   rc=d->ops.ce_disable(d->ops.ctx,(uint8_t)i);
   if(rc)return rc;
   c->configured=0;
  }
  if(c->dst_cpu){d->ops.dma_free(d->ops.ctx,c->dst_bytes,c->dst_cpu,c->dst_dma);c->dst_cpu=0;}
  if(c->src_cpu){d->ops.dma_free(d->ops.ctx,c->src_bytes,c->src_cpu,c->src_dma);c->src_cpu=0;}
  c->dst_dma=c->src_dma=c->dst_bytes=c->src_bytes=0;
 }
 return 0;
}
static int unwind(VqeafWcn3990P2Device *d,VqeafWcn3990P2State s)
{
 int rc;
 if(!d)return 0;
 rc=release_ce(d);if(rc)return rc;
 if(rri_cpu){
  rc=d->ops.ce_rri_disable(d->ops.ctx);
  if(rc)return rc;
  d->ops.dma_free(d->ops.ctx,rri_bytes,rri_cpu,rri_dma);
  rri_cpu=0;rri_dma=0;rri_bytes=0;
 }
 for(int i=(int)VQEAF_WCN3990_CE_MAX-1;i>=0;i--)
  if(irq_registered&(1u<<i)){
   rc=d->ops.free_irq(d->ops.ctx,d->resources.ce_irq[i],(uint8_t)i);
   if(rc)return rc; /* keep MSA/power/DMA intact while an IRQ may still fire */
   irq_registered=(uint16_t)(irq_registered&~(1u<<i));
  }
 if(s>=VQEAF_WCN_P2_MSA)d->ops.msa_release(d->ops.ctx);
 if(s>=VQEAF_WCN_P2_ICNSS)d->ops.icnss_power_off(d->ops.ctx);
 if(s>=VQEAF_WCN_P2_CLOCKS)d->ops.clocks_disable(d->ops.ctx);
 if(s>=VQEAF_WCN_P2_REGULATORS)d->ops.regulators_disable(d->ops.ctx);
 if(s>=VQEAF_WCN_P2_MAPPED)d->ops.unmap_snoc(d->ops.ctx);
 return 0;
}
static int fail(VqeafWcn3990P2Device *d,int rc,VqeafWcn3990P2State stage)
{
 int cleanup=0;
 if(d&&valid(d))cleanup=unwind(d,stage);
 active=0;state=VQEAF_WCN_P2_FAILED;last_error=cleanup?cleanup:(rc?rc:-EIO);
 quiesce_pending=cleanup!=0;
 vqeaf_cap_set("radio.wifi",VQEAF_CAP_PRESENT,0,last_error);
 vqeaf_cap_set("radio.wifi.snoc_ce",VQEAF_CAP_ERROR,0,last_error);
 return last_error;
}
/* Bounded retry for steps that report -EAGAIN (ABI v2). Both a clock and a
 * delay must be supplied; otherwise -EAGAIN is propagated unchanged. A
 * non-advancing clock or the attempt cap also terminates the retry. */
static uint64_t step_now(VqeafWcn3990P2Device *d)
{
 return d->ops.now_ms ? d->ops.now_ms(d->ops.ctx) : 0;
}
static uint32_t step_timeout(VqeafWcn3990P2Device *d)
{
 return d->step_timeout_ms?d->step_timeout_ms:VQEAF_WCN3990_P2_DEFAULT_TIMEOUT_MS;
}
static int step_before(VqeafWcn3990P2Device *d,uint64_t started)
{
 uint64_t now=step_now(d);
 return now-started<step_timeout(d);
}
static void step_delay(VqeafWcn3990P2Device *d)
{
 d->ops.delay_ms(d->ops.ctx,1);
}
#define VQEAF_STEP(d,expr,rc,started) do {                              \
 if(!(d)->ops.now_ms||!(d)->ops.delay_ms){(rc)=(expr);break;}           \
 (started)=step_now(d);                                                 \
 for(uint32_t attempt=0;attempt<VQEAF_WCN3990_P2_MAX_STEP_ATTEMPTS;     \
     attempt++){                                                        \
  uint64_t before_delay;                                                \
  (rc)=(expr);                                                          \
  if((rc)!=-EAGAIN||!step_before(d,started)||                           \
     attempt+1u==VQEAF_WCN3990_P2_MAX_STEP_ATTEMPTS)break;               \
  before_delay=step_now(d);                                             \
  step_delay(d);                                                        \
  if(step_now(d)==before_delay||!step_before(d,started))break;          \
 }                                                                     \
 if((rc)==-EAGAIN)(rc)=-ETIMEDOUT;                                     \
} while(0)

static int probe(void *opaque)
{
 VqeafWcn3990P2Device *d=(VqeafWcn3990P2Device *)opaque;
 VqeafWcn3990P2State stage=VQEAF_WCN_P2_OFF;
 int rc;
 uint64_t started;
 if(quiesce_pending){state=VQEAF_WCN_P2_FAILED;last_error=-EBUSY;return -EBUSY;}
 if(active)return -EBUSY;
 if(!valid(d))return fail(d,-EINVAL,stage);
 memset(ce_rt,0,sizeof(ce_rt));irq_registered=0;rri_cpu=0;rri_dma=0;
 rri_bytes=0;active=d;state=VQEAF_WCN_P2_OFF;last_error=0;
 VQEAF_STEP(d,d->ops.map_snoc(d->ops.ctx,d->resources.snoc_base,d->resources.snoc_size),rc,started);
 if(rc)return fail(d,rc,stage);
 stage=state=VQEAF_WCN_P2_MAPPED;
 VQEAF_STEP(d,d->ops.regulators_enable(d->ops.ctx),rc,started);
 if(rc)return fail(d,rc,stage);
 stage=state=VQEAF_WCN_P2_REGULATORS;
 VQEAF_STEP(d,d->ops.clocks_enable(d->ops.ctx),rc,started);
 if(rc)return fail(d,rc,stage);
 stage=state=VQEAF_WCN_P2_CLOCKS;
 VQEAF_STEP(d,d->ops.icnss_power_on(d->ops.ctx),rc,started);
 if(rc)return fail(d,rc,stage);
 stage=state=VQEAF_WCN_P2_ICNSS;
 VQEAF_STEP(d,d->ops.msa_prepare(d->ops.ctx,d->resources.msa_base,d->resources.msa_size),rc,started);
 if(rc)return fail(d,rc,stage);
 stage=state=VQEAF_WCN_P2_MSA;
 for(uint32_t i=0;i<VQEAF_WCN3990_CE_MAX;i++){
  VQEAF_STEP(d,d->ops.request_irq(d->ops.ctx,d->resources.ce_irq[i],(uint8_t)i),rc,started);
  if(rc)return fail(d,rc,stage);
  irq_registered=(uint16_t)(irq_registered|(1u<<i));
 }
 stage=state=VQEAF_WCN_P2_IRQS;
 /* Poll-mode CEs read their completion indices from a DDR array addressed by
  * the wrapper. Without a provider for it the CEs stay disabled at the index
  * level and reads report -EOPNOTSUPP instead of inventing an index. */
 if(d->ops.ce_rri){
  if(!d->ops.ce_rri_disable)return fail(d,-EINVAL,stage);
  rri_bytes=VQEAF_WCN3990_CE_RRI_BYTES;
  VQEAF_STEP(d,d->ops.dma_alloc(d->ops.ctx,rri_bytes,
             VQEAF_WCN3990_CE_RRI_ALIGN,&rri_cpu,&rri_dma),rc,started);
  if(rc||!rri_cpu||!rri_dma)return fail(d,rc?rc:-ENOMEM,stage);
  if((rri_dma&(VQEAF_WCN3990_CE_RRI_ALIGN-1u))||
     rri_dma>=(1ull<<VQEAF_WCN3990_DMA_BITS)||
     rri_bytes>((1ull<<VQEAF_WCN3990_DMA_BITS)-rri_dma))
   return fail(d,-ERANGE,stage);
  zero_mem(rri_cpu,rri_bytes);
  VQEAF_STEP(d,d->ops.ce_rri(d->ops.ctx,rri_cpu,rri_dma),rc,started);
  if(rc)return fail(d,rc,stage);
 }
 for(uint32_t i=0;i<VQEAF_WCN3990_CE_MAX;i++){
  const VqeafWcn3990CeConfig *cfg=&ce_cfg[i];CeRuntime *c=&ce_rt[i];
  if(!cfg->src_entries&&!cfg->dst_entries)continue;
  if(cfg->src_entries){
   c->src_bytes=(uint64_t)cfg->src_entries*sizeof(VqeafWcn3990CeDesc);
   VQEAF_STEP(d,d->ops.dma_alloc(d->ops.ctx,c->src_bytes,VQEAF_WCN3990_CE_RING_ALIGN,&c->src_cpu,&c->src_dma),rc,started);
   if(rc||!c->src_cpu||!c->src_dma)return fail(d,rc?rc:-ENOMEM,stage);
   if((c->src_dma&7u)||c->src_dma>=(1ull<<VQEAF_WCN3990_DMA_BITS)||
      c->src_bytes>((1ull<<VQEAF_WCN3990_DMA_BITS)-c->src_dma))
    return fail(d,-ERANGE,stage);
   zero_mem(c->src_cpu,c->src_bytes);
  }
  if(cfg->dst_entries){
   c->dst_bytes=(uint64_t)cfg->dst_entries*sizeof(VqeafWcn3990CeDesc);
   VQEAF_STEP(d,d->ops.dma_alloc(d->ops.ctx,c->dst_bytes,VQEAF_WCN3990_CE_RING_ALIGN,&c->dst_cpu,&c->dst_dma),rc,started);
   if(rc||!c->dst_cpu||!c->dst_dma)return fail(d,rc?rc:-ENOMEM,stage);
   if((c->dst_dma&7u)||c->dst_dma>=(1ull<<VQEAF_WCN3990_DMA_BITS)||
      c->dst_bytes>((1ull<<VQEAF_WCN3990_DMA_BITS)-c->dst_dma))
    return fail(d,-ERANGE,stage);
   zero_mem(c->dst_cpu,c->dst_bytes);
  }
  c->configured=1;
  VQEAF_STEP(d,d->ops.ce_configure(d->ops.ctx,cfg,c->src_dma,c->dst_dma),rc,started);
  if(rc)return fail(d,rc,stage);
 }
 stage=state=VQEAF_WCN_P2_CE;
 /* This capability means only the SNOC + CE transport is initialized.
  * radio.wifi remains PRESENT until a later phase boots WLFW and netdev. */
 vqeaf_cap_set("radio.wifi",VQEAF_CAP_PRESENT,0,0);
 vqeaf_cap_set("radio.wifi.snoc_ce",VQEAF_CAP_READY,0,VQEAF_WCN3990_CE_MAX);
 return 0;
}
static int remove_dev(void *opaque)
{
 VqeafWcn3990P2Device *d=(VqeafWcn3990P2Device *)opaque;int rc;
 if(!d||d!=active)return -EINVAL;
 rc=unwind(d,state);
 if(rc){
  active=0;state=VQEAF_WCN_P2_FAILED;last_error=rc;quiesce_pending=1;
  vqeaf_cap_set("radio.wifi.snoc_ce",VQEAF_CAP_ERROR,0,rc);
  vqeaf_cap_set("radio.wifi",VQEAF_CAP_PRESENT,0,rc);
  return rc;
 }
 active=0;state=VQEAF_WCN_P2_REMOVED;last_error=0;quiesce_pending=0;
 vqeaf_cap_set("radio.wifi.snoc_ce",VQEAF_CAP_UNSUPPORTED,0,0);
 vqeaf_cap_set("radio.wifi",VQEAF_CAP_PRESENT,0,0);
 return 0;
}
static const VqeafDriver drv={
 .abi=VQEAF_DRIVER_ABI,.size=sizeof(VqeafDriver),
 .name="qcom-wcn3990-phase2",.device_class=VQEAF_DEV_RADIO,
 .probe=probe,.remove=remove_dev
};
int vqeaf_wcn3990_phase2_register(void){return vqeaf_driver_register(&drv);}
int vqeaf_wcn3990_phase2_bind(VqeafWcn3990P2Device *d,uint32_t *id)
{
 if(!d||!id)return -EINVAL;
 if(active||quiesce_pending)return -EBUSY;
 return vqeaf_driver_bind("qcom-wcn3990-phase2",d,id);
}
int vqeaf_wcn3990_phase2_state(void){return (int)state;}
int vqeaf_wcn3990_phase2_error(void){return last_error;}
