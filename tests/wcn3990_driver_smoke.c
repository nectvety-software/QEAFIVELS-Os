#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "../kernel/include/vqeaf/wcn3990.h"
#include "../system/core/include/vqeaf/driver.h"
#include "../system/core/include/vqeaf/capability.h"

typedef struct { int step,fail_at,shutdown_stage; } Mock;
static int step(Mock*m,int n){m->step=n;return m->fail_at==n?-EIO:0;}
static int map(void*p,uint64_t b,uint64_t s){assert(b&&s);return step(p,1);}
static int irq(void*p,uint32_t f,uint32_t n){assert(f==130&&n==12);return step(p,2);}
static int fw(void*p,const char*f,const char*c){assert(!strcmp(f,"wlanmdsp.mbn"));assert(!strcmp(c,"WCNSS_qcom_cfg.ini"));return step(p,3);}
static int fw_missing(void*p,const char*f,const char*c){assert(!strcmp(f,"wlanmdsp.mbn"));assert(!strcmp(c,"WCNSS_qcom_cfg.ini"));((Mock*)p)->step=3;return -ENODATA;}
static int ce(void*p){return step(p,4);}
static int wlfw(void*p){return step(p,5);}
static int netdev(void*p){return step(p,6);}
static void shutdown_cb(void*p,VqeafWcn3990State s){((Mock*)p)->shutdown_stage=(int)s;}

static VqeafWcn3990Device make_device(Mock*m){
 VqeafWcn3990Device d;memset(&d,0,sizeof(d));
 d.abi=VQEAF_WCN3990_DEVICE_ABI;d.size=sizeof(d);
 d.resources.compatible=VQEAF_WCN3990_COMPATIBLE;
 d.resources.bus=VQEAF_WCN3990_BUS_SNOC;
 /* Test-only resource values; hardware values must come from the DT parser. */
 d.resources.regs_base=0x18800000ull;d.resources.regs_size=0x800000ull;
 d.resources.ce_irq_first=130;d.resources.ce_irq_count=12;
 d.resources.msa_base=0x90000000ull;d.resources.msa_size=0x200000ull;
 d.ops.ctx=m;d.ops.map_registers=map;d.ops.request_ce_irqs=irq;
 d.ops.load_firmware_and_board_data=fw;d.ops.init_copy_engines=ce;
 d.ops.start_wlfw=wlfw;d.ops.register_netdev=netdev;d.ops.shutdown=shutdown_cb;
 return d;
}
int main(void){
 Mock m={0,0,0};VqeafWcn3990Device d=make_device(&m);uint32_t id=0;
 vqeaf_driver_reset();vqeaf_cap_reset();
 assert(vqeaf_wcn3990_register()==0);
 assert(vqeaf_wcn3990_bind(&d,&id)==0);
 assert(vqeaf_wcn3990_state()==VQEAF_WCN3990_NETDEV_READY);
 assert(vqeaf_cap_is_ready("radio.wifi"));
 assert(m.step==6);
 assert(vqeaf_driver_unbind(id)==0);
 assert(vqeaf_wcn3990_state()==VQEAF_WCN3990_REMOVED);
 assert(m.shutdown_stage==VQEAF_WCN3990_NETDEV_READY);

 m=(Mock){0,3,0};d=make_device(&m);id=0;
 vqeaf_driver_reset();vqeaf_cap_reset();
 assert(vqeaf_wcn3990_register()==0);
 assert(vqeaf_wcn3990_bind(&d,&id)==-EIO);
 assert(vqeaf_wcn3990_state()==VQEAF_WCN3990_FAILED);
 assert(m.shutdown_stage==VQEAF_WCN3990_IRQ_READY);
 assert(!vqeaf_cap_is_ready("radio.wifi"));

 m=(Mock){0,0,0};d=make_device(&m);d.ops.load_firmware_and_board_data=fw_missing;id=0;
 vqeaf_driver_reset();vqeaf_cap_reset();
 assert(vqeaf_wcn3990_register()==0);
 assert(vqeaf_wcn3990_bind(&d,&id)==-ENODATA);
 VqeafCapability cap;assert(vqeaf_cap_get("radio.wifi",&cap)==0);
 assert(cap.state==VQEAF_CAP_BLOCKED_FIRMWARE);
 assert(m.shutdown_stage==VQEAF_WCN3990_IRQ_READY);

 m=(Mock){0,0,0};d=make_device(&m);d.resources.bus="sdio";id=0;
 vqeaf_driver_reset();vqeaf_cap_reset();
 assert(vqeaf_wcn3990_register()==0);
 assert(vqeaf_wcn3990_bind(&d,&id)==-ENODEV);
 assert(m.step==0);
 assert(vqeaf_wcn3990_state()==VQEAF_WCN3990_FAILED);
 return 0;
}
