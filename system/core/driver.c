#include <string.h>
#include "include/vqeaf/driver.h"
static const VqeafDriver *drivers[VQEAF_DRIVER_MAX];static VqeafDriverInstance instances[VQEAF_DRIVER_MAX];static uint32_t driver_count,instance_count;
void vqeaf_driver_reset(void){memset(drivers,0,sizeof(drivers));memset(instances,0,sizeof(instances));driver_count=instance_count=0;}
int vqeaf_driver_register(const VqeafDriver*d){if(!d||d->abi!=VQEAF_DRIVER_ABI||d->size<sizeof(*d)||!d->name||!d->probe)return -1;for(uint32_t i=0;i<driver_count;i++)if(strcmp(drivers[i]->name,d->name)==0)return -2;if(driver_count==VQEAF_DRIVER_MAX)return -3;drivers[driver_count++]=d;return 0;}
int vqeaf_driver_bind(const char*n,void*dev,uint32_t*out){if(!n||!out||instance_count==VQEAF_DRIVER_MAX)return -1;const VqeafDriver*d=0;for(uint32_t i=0;i<driver_count;i++)if(strcmp(drivers[i]->name,n)==0)d=drivers[i];if(!d)return -2;uint32_t id=instance_count++;VqeafDriverInstance*x=&instances[id];x->driver=d;x->device=dev;x->state=VQEAF_DRV_PROBING;int r=d->probe(dev);x->last_error=r;x->state=r?VQEAF_DRV_FAILED:VQEAF_DRV_READY;*out=id;return r;}
int vqeaf_driver_unbind(uint32_t id){if(id>=instance_count||!instances[id].driver||instances[id].state==VQEAF_DRV_REMOVED)return -1;VqeafDriverInstance*x=&instances[id];int r=x->driver->remove?x->driver->remove(x->device):0;x->state=VQEAF_DRV_REMOVED;x->last_error=r;return r;}
int vqeaf_driver_get(uint32_t id,VqeafDriverInstance*out){if(!out||id>=instance_count||!instances[id].driver)return -1;*out=instances[id];return 0;}
