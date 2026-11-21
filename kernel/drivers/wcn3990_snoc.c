/* SPDX-License-Identifier: MIT */
/*
 * QEAF native WCN3990/SNOC driver orchestration.
 *
 * This is the hardware-independent driver layer, not a reimplementation of
 * Qualcomm's firmware protocol. A Violet platform backend must supply the
 * MMIO/IRQ/CE/QMI operations. Probe fails closed when that backend or genuine
 * firmware/board calibration is unavailable.
 */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "vqeaf/wcn3990.h"
#include "../../system/core/include/vqeaf/driver.h"
#include "../../system/core/include/vqeaf/capability.h"

static VqeafWcn3990State g_state = VQEAF_WCN3990_OFF;
static int g_last_error;
static VqeafWcn3990Device *g_device;

static int resources_valid(const VqeafWcn3990Device *d)
{
 const VqeafWcn3990Resources *r=&d->resources;
 if(!r->compatible||strcmp(r->compatible,VQEAF_WCN3990_COMPATIBLE)!=0)return 0;
 if(!r->bus||strcmp(r->bus,VQEAF_WCN3990_BUS_SNOC)!=0)return 0;
 if(!r->regs_base||r->regs_size<0x1000u||r->regs_base>UINT64_MAX-r->regs_size)return 0;
 if(r->ce_irq_count!=VQEAF_WCN3990_CE_COUNT||
    r->ce_irq_first>UINT32_MAX-r->ce_irq_count)return 0;
 if(!r->msa_base||!r->msa_size||r->msa_base>UINT64_MAX-r->msa_size)return 0;
 return 1;
}

static int ops_valid(const VqeafWcn3990Ops *o)
{
 return o->map_registers&&o->request_ce_irqs&&
        o->load_firmware_and_board_data&&o->init_copy_engines&&
        o->start_wlfw&&o->register_netdev&&o->shutdown;
}

static int fail_probe(VqeafWcn3990Device *d,int rc,VqeafWcn3990State stage)
{
 if(d&&d->ops.shutdown&&stage!=VQEAF_WCN3990_OFF)
  d->ops.shutdown(d->ops.ctx,stage);
 g_state=VQEAF_WCN3990_FAILED;
 g_last_error=rc?rc:-EIO;
 vqeaf_cap_set("radio.wifi",g_last_error==-ENODATA?
               VQEAF_CAP_BLOCKED_FIRMWARE:VQEAF_CAP_ERROR,0,g_last_error);
 return g_last_error;
}

static int wcn3990_probe(void *opaque)
{
 VqeafWcn3990Device *d=(VqeafWcn3990Device *)opaque;
 VqeafWcn3990State stage=VQEAF_WCN3990_OFF;
 int rc;
 if(!d||d->abi!=VQEAF_WCN3990_DEVICE_ABI||d->size<sizeof(*d))
  return fail_probe(d,-EINVAL,stage);
 if(!resources_valid(d)||!ops_valid(&d->ops))
  return fail_probe(d,-ENODEV,stage);

 g_device=d; g_state=VQEAF_WCN3990_PROBING; g_last_error=0;
 vqeaf_cap_set("radio.wifi",VQEAF_CAP_PRESENT,0,0);
 rc=d->ops.map_registers(d->ops.ctx,d->resources.regs_base,d->resources.regs_size);
 if(rc)return fail_probe(d,rc,stage);
 stage=VQEAF_WCN3990_MAPPED; g_state=stage;

 rc=d->ops.request_ce_irqs(d->ops.ctx,d->resources.ce_irq_first,
                           d->resources.ce_irq_count);
 if(rc)return fail_probe(d,rc,stage);
 stage=VQEAF_WCN3990_IRQ_READY; g_state=stage;

 rc=d->ops.load_firmware_and_board_data(d->ops.ctx,
       VQEAF_WCN3990_FIRMWARE,VQEAF_WCN3990_BOARD_CONFIG);
 if(rc)return fail_probe(d,rc,stage);
 stage=VQEAF_WCN3990_FIRMWARE_READY; g_state=stage;

 rc=d->ops.init_copy_engines(d->ops.ctx);
 if(rc)return fail_probe(d,rc,stage);
 stage=VQEAF_WCN3990_CE_READY; g_state=stage;

 rc=d->ops.start_wlfw(d->ops.ctx);
 if(rc)return fail_probe(d,rc,stage);
 stage=VQEAF_WCN3990_WLFW_READY; g_state=stage;

 rc=d->ops.register_netdev(d->ops.ctx);
 if(rc)return fail_probe(d,rc,stage);
 g_state=VQEAF_WCN3990_NETDEV_READY;
 g_last_error=0;
 vqeaf_cap_set("radio.wifi",VQEAF_CAP_READY,0,0);
 return 0;
}

static int wcn3990_remove(void *opaque)
{
 VqeafWcn3990Device *d=(VqeafWcn3990Device *)opaque;
 if(!d||d!=g_device)return -EINVAL;
 if(d->ops.shutdown)d->ops.shutdown(d->ops.ctx,g_state);
 g_device=0;g_state=VQEAF_WCN3990_REMOVED;g_last_error=0;
 vqeaf_cap_set("radio.wifi",VQEAF_CAP_UNSUPPORTED,0,0);
 return 0;
}

static const VqeafDriver g_driver={
 .abi=VQEAF_DRIVER_ABI,.size=sizeof(VqeafDriver),
 .name="qcom-wcn3990-snoc",.device_class=VQEAF_DEV_RADIO,
 .probe=wcn3990_probe,.remove=wcn3990_remove
};

int vqeaf_wcn3990_register(void)
{
 return vqeaf_driver_register(&g_driver);
}
int vqeaf_wcn3990_bind(VqeafWcn3990Device *device,uint32_t *instance_id)
{
 if(!device||!instance_id)return -EINVAL;
 return vqeaf_driver_bind("qcom-wcn3990-snoc",device,instance_id);
}
int vqeaf_wcn3990_state(void){return (int)g_state;}
int vqeaf_wcn3990_last_error(void){return g_last_error;}
