#include <string.h>
#include "include/vqeaf/service.h"
typedef struct {
 VqeafServiceDef def;
 VqeafServiceState state;
 int last_error;
} Service;
static Service services[VQEAF_SERVICE_MAX];
static uint32_t service_count;
static char perms[VQEAF_SERVICE_PERMTABLE][24];
static uint32_t perm_count;
static uint8_t granted[VQEAF_CALLER_MAX][VQEAF_SERVICE_PERMTABLE];
void vqeaf_svc_reset(void){
 memset(services,0,sizeof(services));memset(perms,0,sizeof(perms));
 memset(granted,0,sizeof(granted));service_count=0;perm_count=0;
}
static int perm_id(const char *p,int create){
 if(!p||!*p||strlen(p)>=24)return -1;
 for(uint32_t i=0;i<perm_count;i++)if(strcmp(perms[i],p)==0)return (int)i;
 if(!create)return -2;
 if(perm_count==VQEAF_SERVICE_PERMTABLE)return -3;
 strncpy(perms[perm_count],p,23);perms[perm_count][23]=0;
 return (int)perm_count++;
}
int vqeaf_svc_register(const VqeafServiceDef *def){
 if(!def||!def->name||!*def->name||strlen(def->name)>=VQEAF_SERVICE_NAME_MAX)return -1;
 if(!def->start||!def->stop)return -2;
 if(service_count==VQEAF_SERVICE_MAX)return -3;
 for(uint32_t i=0;i<service_count;i++)if(strcmp(services[i].def.name,def->name)==0)return -4;
 /* every declared dependency and permission must exist up front */
 for(unsigned i=0;i<VQEAF_SERVICE_DEPS;i++)if(def->dependencies[i]&&vqeaf_svc_find(def->dependencies[i])<0)return -5;
 for(unsigned i=0;i<VQEAF_SERVICE_PERMS;i++)if(def->permissions[i]&&perm_id(def->permissions[i],1)<0)return -6;
 Service *s=&services[service_count];
 memset(s,0,sizeof(*s));s->def=*def;s->state=VQEAF_SVC_STOPPED;
 return (int)service_count++;
}
int vqeaf_svc_find(const char *name){
 if(!name)return -1;
 for(uint32_t i=0;i<service_count;i++)if(strcmp(services[i].def.name,name)==0)return (int)i;
 return -1;
}
int vqeaf_svc_grant(uint32_t caller,const char *permission){
 if(caller>=VQEAF_CALLER_MAX)return -1;
 int p=perm_id(permission,1);
 if(p<0)return p;
 granted[caller][p]=1;return 0;
}
int vqeaf_svc_revoke(uint32_t caller,const char *permission){
 if(caller>=VQEAF_CALLER_MAX)return -1;
 int p=perm_id(permission,0);
 if(p<0)return p;
 granted[caller][p]=0;return 0;
}
int vqeaf_svc_holds(uint32_t caller,const char *permission){
 if(caller>=VQEAF_CALLER_MAX)return 0;
 int p=perm_id(permission,0);
 return p>=0&&granted[caller][p];
}
static int caller_permitted(uint32_t caller,const VqeafServiceDef *d){
 for(unsigned i=0;i<VQEAF_SERVICE_PERMS;i++)
  if(d->permissions[i]&&!vqeaf_svc_holds(caller,d->permissions[i]))return 0;
 return 1;
}
static int start_locked(uint32_t id,uint32_t caller,int depth){
 if(id>=service_count)return -1;
 Service *s=&services[id];
 if(s->state==VQEAF_SVC_RUNNING)return 0;
 if(s->state==VQEAF_SVC_STARTING)return -2; /* dependency cycle */
 if(depth>(int)VQEAF_SERVICE_MAX)return -2;
 s->state=VQEAF_SVC_STARTING;
 for(unsigned i=0;i<VQEAF_SERVICE_DEPS;i++){
  if(!s->def.dependencies[i])continue;
  int d=vqeaf_svc_find(s->def.dependencies[i]);
  if(d<0){s->state=VQEAF_SVC_FAILED;s->last_error=-3;return -3;}
  int r=start_locked((uint32_t)d,caller,depth+1);
  if(r){s->state=VQEAF_SVC_FAILED;s->last_error=r;return r;}
 }
 if(!caller_permitted(caller,&s->def)){s->state=VQEAF_SVC_STOPPED;s->last_error=-4;return -4;}
 int r=s->def.start(s->def.ctx);
 s->last_error=r;
 s->state=r?VQEAF_SVC_FAILED:VQEAF_SVC_RUNNING;
 return r;
}
int vqeaf_svc_start(uint32_t id,uint32_t caller){return start_locked(id,caller,0);}
int vqeaf_svc_stop(uint32_t id,uint32_t caller){
 if(id>=service_count)return -1;
 Service *s=&services[id];
 if(s->state!=VQEAF_SVC_RUNNING&&s->state!=VQEAF_SVC_FAILED)return -2;
 if(!caller_permitted(caller,&s->def))return -3;
 /* dependents must stop first: scan everything that names this service */
 for(uint32_t i=0;i<service_count;i++){
  if(i==id||services[i].state!=VQEAF_SVC_RUNNING)continue;
  for(unsigned k=0;k<VQEAF_SERVICE_DEPS;k++)
   if(services[i].def.dependencies[k]&&strcmp(services[i].def.dependencies[k],s->def.name)==0)
    return -4;
 }
 s->state=VQEAF_SVC_STOPPING;
 int r=s->def.stop(s->def.ctx);
 s->last_error=r;
 s->state=r?VQEAF_SVC_FAILED:VQEAF_SVC_STOPPED;
 return r;
}
int vqeaf_svc_stop_all(uint32_t caller){
 (void)caller;
 for(uint32_t pass=0;pass<=service_count;pass++){ /* repeated passes settle dependency chains */
  int moved=0;
  for(uint32_t i=service_count;i>0;i--){
   Service *s=&services[i-1];
   if(s->state!=VQEAF_SVC_RUNNING)continue;
   int blocked=0;
   for(uint32_t j=0;j<service_count&&!blocked;j++){
    if(j==i-1||services[j].state!=VQEAF_SVC_RUNNING)continue;
    for(unsigned k=0;k<VQEAF_SERVICE_DEPS;k++)
     if(services[j].def.dependencies[k]&&strcmp(services[j].def.dependencies[k],s->def.name)==0)blocked=1;
   }
   if(blocked)continue;
   s->state=VQEAF_SVC_STOPPING;
   int r=s->def.stop(s->def.ctx);
   s->last_error=r;s->state=r?VQEAF_SVC_FAILED:VQEAF_SVC_STOPPED;
   moved=1;
  }
  if(!moved)break;
 }
 return 0;
}
VqeafServiceState vqeaf_svc_state(uint32_t id){return id<service_count?services[id].state:VQEAF_SVC_STOPPED;}
