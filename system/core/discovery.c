#include <string.h>
#include <stdint.h>
#include "include/vqeaf/discovery.h"
typedef struct { char bus[16]; VqeafBusEnumerate fn; } Bus;
static Bus buses[VQEAF_BUS_MAX];
static uint32_t bus_count,device_count;
void vqeaf_discovery_reset(void){memset(buses,0,sizeof(buses));bus_count=device_count=0;}
int vqeaf_discovery_register_bus(const char *bus_id,VqeafBusEnumerate fn){
 if(!bus_id||!*bus_id||strlen(bus_id)>=sizeof(buses[0].bus)||!fn)return -1;
 for(uint32_t i=0;i<bus_count;i++)if(strcmp(buses[i].bus,bus_id)==0)return -2;
 if(bus_count==VQEAF_BUS_MAX)return -3;
 strncpy(buses[bus_count].bus,bus_id,15);
 buses[bus_count].fn=fn;
 return (int)bus_count++;
}
int vqeaf_discovery_device_count(void){return (int)device_count;}
int vqeaf_discovery_scan(void){
 uint32_t processed=0;
 for(uint32_t b=0;b<bus_count;b++){
  VqeafDiscoveredDevice found[VQEAF_DISCOVER_MAX];
  memset(found,0,sizeof(found));
  int n=buses[b].fn(buses[b].bus,found,VQEAF_DISCOVER_MAX);
  if(n<0)continue;
  for(int i=0;i<n;i++){
   processed++;device_count++;
   /* capability name: dev.<bus>/<device> — built manually, bounded */
   char cap[64];size_t ci=0;
   const char *parts[3]={"dev.",buses[b].bus,"/"};
   for(int k=0;k<3&&ci<sizeof(cap)-1;k++){size_t l=strlen(parts[k]);if(ci+l>=sizeof(cap)-1)break;memcpy(cap+ci,parts[k],l);ci+=l;}
   size_t nl=strlen(found[i].name);
   if(ci+nl>=sizeof(cap))continue;
   memcpy(cap+ci,found[i].name,nl);ci+=nl;cap[ci]=0;
   if(found[i].status==VQEAF_DISC_BLOCKED_FIRMWARE){vqeaf_cap_set(cap,VQEAF_CAP_BLOCKED_FIRMWARE,0,0);continue;}
   if(found[i].status==VQEAF_DISC_BLOCKED_HARDWARE){vqeaf_cap_set(cap,VQEAF_CAP_DEGRADED,0,0);continue;}
   /* Try to bind a driver of the same name; PRESENT without one, READY with. */
   uint32_t instance=0;
   static void *dev_cookie;dev_cookie=(void*)(uintptr_t)(b+1);
   int bind=vqeaf_driver_bind(found[i].name,dev_cookie,&instance);
   if(bind==0){
    VqeafDriverInstance ins;
    vqeaf_driver_get(instance,&ins);
    vqeaf_cap_set(cap,ins.state==VQEAF_DRV_READY?VQEAF_CAP_READY:VQEAF_CAP_ERROR,1,(int32_t)instance);
   }else{
    vqeaf_cap_set(cap,VQEAF_CAP_PRESENT,0,0); /* seen by firmware, no driver yet */
   }
  }
 }
 return (int)processed;
}
