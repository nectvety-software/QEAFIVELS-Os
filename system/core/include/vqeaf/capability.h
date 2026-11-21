#ifndef VQEAF_CAPABILITY_H
#define VQEAF_CAPABILITY_H
#include <stdint.h>
#define VQEAF_CAP_MAX 64u
typedef enum { VQEAF_CAP_UNSUPPORTED=0,VQEAF_CAP_PRESENT,VQEAF_CAP_READY,VQEAF_CAP_DEGRADED,VQEAF_CAP_BLOCKED_FIRMWARE,VQEAF_CAP_ERROR } VqeafCapabilityState;
typedef struct { char name[40];VqeafCapabilityState state;uint32_t provider;int32_t detail; } VqeafCapability;
int vqeaf_cap_set(const char *name,VqeafCapabilityState state,uint32_t provider,int32_t detail);int vqeaf_cap_get(const char *name,VqeafCapability *out);int vqeaf_cap_is_ready(const char *name);void vqeaf_cap_reset(void);
#endif
