#ifndef VQEAF_SERVICE_H
#define VQEAF_SERVICE_H
#include <stdint.h>
/* System service manager v1: lifecycle, dependency ordering, permissions.
 * Starting a service requires the caller to hold every permission the service
 * declares; stopping a service requires its dependents to stop first. */
#define VQEAF_SERVICE_MAX 16u
#define VQEAF_SERVICE_NAME_MAX 24u
#define VQEAF_SERVICE_DEPS 4u
#define VQEAF_SERVICE_PERMS 4u
#define VQEAF_SERVICE_PERMTABLE 16u
#define VQEAF_CALLER_MAX 8u
typedef enum { VQEAF_SVC_STOPPED=0,VQEAF_SVC_STARTING,VQEAF_SVC_RUNNING,VQEAF_SVC_STOPPING,VQEAF_SVC_FAILED } VqeafServiceState;
typedef struct {
 const char *name;
 uint32_t version;
 int (*start)(void *ctx);
 int (*stop)(void *ctx);
 void *ctx;
 const char *dependencies[VQEAF_SERVICE_DEPS];
 const char *permissions[VQEAF_SERVICE_PERMS];
} VqeafServiceDef;
int vqeaf_svc_register(const VqeafServiceDef *def); /* returns id */
int vqeaf_svc_grant(uint32_t caller,const char *permission);
int vqeaf_svc_revoke(uint32_t caller,const char *permission);
int vqeaf_svc_holds(uint32_t caller,const char *permission);
int vqeaf_svc_start(uint32_t id,uint32_t caller);   /* starts dependencies first; cycle-safe */
int vqeaf_svc_stop(uint32_t id,uint32_t caller);    /* refuses while dependents run */
int vqeaf_svc_stop_all(uint32_t caller);            /* reverse registration order */
VqeafServiceState vqeaf_svc_state(uint32_t id);
int vqeaf_svc_find(const char *name);
void vqeaf_svc_reset(void);
#endif
