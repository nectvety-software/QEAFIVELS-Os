#include <assert.h>
#include <string.h>
#include "../system/core/include/vqeaf/driver.h"
#include "../system/core/include/vqeaf/capability.h"
#include "../system/core/include/vqeaf/storage.h"
#include "../system/core/include/vqeaf/power.h"
#include "../system/core/include/vqeaf/locale.h"
static int probe_ok(void*p){return p?0:-9;}static int removed;
static int remove_ok(void*p){(void)p;removed++;return 0;}
static const VqeafDriver drv={VQEAF_DRIVER_ABI,sizeof(VqeafDriver),"ram-storage",VQEAF_DEV_STORAGE,probe_ok,remove_ok};
int main(void){
 vqeaf_driver_reset();assert(vqeaf_driver_register(&drv)==0);assert(vqeaf_driver_register(&drv)==-2);uint32_t id;int device=1;assert(vqeaf_driver_bind("ram-storage",&device,&id)==0);VqeafDriverInstance ins;assert(vqeaf_driver_get(id,&ins)==0&&ins.state==VQEAF_DRV_READY);assert(vqeaf_driver_unbind(id)==0&&removed==1);
 vqeaf_cap_reset();assert(vqeaf_cap_set("storage.internal",VQEAF_CAP_READY,1,0)==0);assert(vqeaf_cap_is_ready("storage.internal"));assert(vqeaf_cap_set("wifi",VQEAF_CAP_BLOCKED_FIRMWARE,0,0)==0);assert(!vqeaf_cap_is_ready("wifi"));
 VqeafVnode root;memset(&root,0,sizeof(root));vqeaf_storage_reset();assert(vqeaf_storage_mount("/data",&root,0)==0);VqeafVnode nested;memset(&nested,0,sizeof(nested));VqeafVnode*out=0;const char*rel=0;assert(vqeaf_storage_resolve("/data/user/settings",&out,&rel)==0&&out==&root&&strcmp(rel,"/user/settings")==0);assert(vqeaf_storage_mount("/data/cache",&nested,0)==0);assert(vqeaf_storage_resolve("/data/cache/index",&out,&rel)==0&&out==&nested&&strcmp(rel,"/index")==0);assert(vqeaf_storage_resolve("/datax/file",&out,&rel)==-2);assert(vqeaf_storage_resolve("/data/../escape",&out,&rel)==-1);assert(vqeaf_storage_unmount("/data/cache")==0);assert(vqeaf_storage_unmount("/data")==0);assert(vqeaf_storage_resolve("/data/user",&out,&rel)==-2);
 VqeafPowerManager pm;assert(vqeaf_power_init(&pm,VQEAF_TIMEOUT_15S,1,1000)==0);vqeaf_power_tick(&pm,15999);assert(pm.state==VQEAF_POWER_ACTIVE);vqeaf_power_tick(&pm,16000);assert(pm.state==VQEAF_POWER_DISPLAY_OFF);vqeaf_power_user_activity(&pm,17000);assert(pm.state==VQEAF_POWER_ACTIVE);assert(vqeaf_power_set_timeout(&pm,VQEAF_TIMEOUT_NEVER,17000)==0);vqeaf_power_tick(&pm,9999999);assert(pm.state==VQEAF_POWER_ACTIVE);assert(vqeaf_power_set_timeout(&pm,VQEAF_TIMEOUT_15S,20000000)==0);vqeaf_power_set_suspended(&pm,1);vqeaf_power_tick(&pm,20000000);assert(pm.state==VQEAF_POWER_ACTIVE);vqeaf_power_set_suspended(&pm,0);vqeaf_power_user_activity(&pm,20000001);vqeaf_power_tick(&pm,20015001);assert(pm.state==VQEAF_POWER_DISPLAY_OFF);vqeaf_power_user_activity(&pm,20016000);assert(pm.state==VQEAF_POWER_ACTIVE);assert(vqeaf_power_timeout_ms(VQEAF_TIMEOUT_30M)==1800000u);
 assert(strcmp(VQEAF_DEFAULT_LOCALE,"vi-VN")==0);assert(strcmp(VQEAF_DEFAULT_FONT_FAMILY,"Ubuntu")==0);return 0;
}
