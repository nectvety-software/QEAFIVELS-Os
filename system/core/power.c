#include <string.h>
#include "include/vqeaf/power.h"
uint32_t vqeaf_power_timeout_ms(VqeafScreenTimeout t){static const uint32_t v[]={15000u,60000u,300000u,1800000u,0u};return t<=VQEAF_TIMEOUT_NEVER?v[t]:0u;}
int vqeaf_power_init(VqeafPowerManager*p,VqeafScreenTimeout t,int capable,uint64_t now){
 if(!p||t>VQEAF_TIMEOUT_NEVER)return -1;
 memset(p,0,sizeof(*p));
 p->timeout=t;p->display_capable=capable?1:0;p->last_activity_ms=now;p->state=VQEAF_POWER_ACTIVE;p->dim_after_ms=0;
 return 0;
}
int vqeaf_power_set_timeout(VqeafPowerManager*p,VqeafScreenTimeout t,uint64_t now){if(!p||t>VQEAF_TIMEOUT_NEVER)return -1;p->timeout=t;p->last_activity_ms=now;p->state=VQEAF_POWER_ACTIVE;return 0;}
void vqeaf_power_user_activity(VqeafPowerManager*p,uint64_t now){
 if(!p)return;
 p->last_activity_ms=now;
 if(p->state!=VQEAF_POWER_ACTIVE&&p->blank_cb)p->blank_cb(p->blank_ctx,VQEAF_POWER_ACTIVE);
 p->state=VQEAF_POWER_ACTIVE;
}
void vqeaf_power_set_suspended(VqeafPowerManager*p,int s){if(p)p->suspended=s?1:0;}
int vqeaf_power_set_dim_stage(VqeafPowerManager*p,uint32_t dim_after_ms){if(!p)return -1;p->dim_after_ms=dim_after_ms;return 0;}
int vqeaf_power_set_blank_callback(VqeafPowerManager*p,void(*cb)(void*,VqeafDisplayPowerState),void*ctx){if(!p)return -1;p->blank_cb=cb;p->blank_ctx=ctx;return 0;}
static int expire_locks(VqeafPowerManager*p,uint64_t now){
 for(uint32_t i=0;i<p->lock_count;){
  if(!p->locks[i].until_released&&now>=p->locks[i].expires_ms){
   for(uint32_t j=i+1;j<p->lock_count;j++)p->locks[j-1]=p->locks[j];
   p->lock_count--;continue;
  }
  i++;
 }
 return (int)p->lock_count;
}
int vqeaf_power_wake_lock(VqeafPowerManager*p,const char*owner,uint64_t now,uint64_t ttl){
 if(!p||!owner||!*owner||strlen(owner)>=VQEAF_WAKELOCK_NAME_MAX)return -1;
 if(ttl&&now>UINT64_MAX-ttl)return -1;
 expire_locks(p,now);
 for(uint32_t i=0;i<p->lock_count;i++)if(strcmp(p->locks[i].owner,owner)==0){ /* refresh */p->locks[i].until_released=!ttl;p->locks[i].expires_ms=ttl?now+ttl:0;return 0;}
 if(p->lock_count==VQEAF_WAKELOCK_MAX)return -2;
 VqeafWakeLock*l=&p->locks[p->lock_count++];
 memset(l,0,sizeof(*l));
 strncpy(l->owner,owner,VQEAF_WAKELOCK_NAME_MAX-1);
 l->until_released=ttl?0:1;l->expires_ms=ttl?now+ttl:0;
 return 0;
}
int vqeaf_power_wake_release(VqeafPowerManager*p,const char*owner,uint64_t now){
 if(!p||!owner)return -1;
 expire_locks(p,now);
 for(uint32_t i=0;i<p->lock_count;i++)if(strcmp(p->locks[i].owner,owner)==0){
  for(uint32_t j=i+1;j<p->lock_count;j++)p->locks[j-1]=p->locks[j];
  p->lock_count--;return 0;
 }
 return -2;
}
int vqeaf_power_wake_active(const VqeafPowerManager*p){if(!p)return 0;return p->lock_count?1:0;}
void vqeaf_power_set_battery(VqeafPowerManager*p,const VqeafBatteryStatus*s){
 if(!p)return;
 if(!s||!s->present){p->battery_present=0;p->level_permille=0;p->charging=0;return;} /* no provider = absent, never invented */
 if(s->level_permille>1000)return; /* reject impossible telemetry */
 p->battery_present=1;p->level_permille=s->level_permille;p->charging=s->state==VQEAF_BATTERY_CHARGING?1:0;
}
VqeafDisplayPowerState vqeaf_power_state(const VqeafPowerManager*p){return p?p->state:VQEAF_POWER_DISPLAY_OFF;}
/* Effective blank policy: low battery shortens the screen timeout. Policy is
 * fixed and documented: <=15% level halves the timeout; critical <=5% blanks
 * immediately regardless of the configured timeout. */
static uint64_t effective_timeout(const VqeafPowerManager*p,uint64_t base){
 if(!p->battery_present||p->charging)return base;
 if(p->level_permille<=50)return 0;
 if(p->level_permille<=150)return base/2;
 return base;
}
void vqeaf_power_tick(VqeafPowerManager*p,uint64_t now){
 if(!p||!p->display_capable||p->suspended)return;
 expire_locks(p,now);
 if(p->lock_count||p->timeout==VQEAF_TIMEOUT_NEVER||now<p->last_activity_ms)return;
 uint64_t base=vqeaf_power_timeout_ms(p->timeout);
 uint64_t limit=effective_timeout(p,base);
 if(limit==0){ /* critical battery: blank immediately */
  if(p->state!=VQEAF_POWER_DISPLAY_OFF){p->state=VQEAF_POWER_DISPLAY_OFF;if(p->blank_cb)p->blank_cb(p->blank_ctx,VQEAF_POWER_DISPLAY_OFF);}
  return;
 }
 uint64_t idle=now-p->last_activity_ms;
 VqeafDisplayPowerState target=p->state;
 if(idle>=limit)target=VQEAF_POWER_DISPLAY_OFF;
 else if(p->dim_after_ms&&idle>=limit-p->dim_after_ms)target=VQEAF_POWER_DIMMED;
 else target=VQEAF_POWER_ACTIVE;
 if(target!=p->state){p->state=target;if(p->blank_cb)p->blank_cb(p->blank_ctx,target);}
}
