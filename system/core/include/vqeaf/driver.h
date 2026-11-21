#ifndef VQEAF_DRIVER_H
#define VQEAF_DRIVER_H
#include <stdint.h>
#define VQEAF_DRIVER_ABI 1u
#define VQEAF_DRIVER_MAX 32u
typedef enum { VQEAF_DEV_UNKNOWN=0,VQEAF_DEV_STORAGE,VQEAF_DEV_DISPLAY,VQEAF_DEV_INPUT,VQEAF_DEV_AUDIO,VQEAF_DEV_POWER,VQEAF_DEV_RADIO,VQEAF_DEV_SENSOR } VqeafDeviceClass;
typedef enum { VQEAF_DRV_REGISTERED=1,VQEAF_DRV_PROBING,VQEAF_DRV_READY,VQEAF_DRV_DEGRADED,VQEAF_DRV_FAILED,VQEAF_DRV_REMOVED } VqeafDriverState;
typedef struct { uint32_t abi,size;const char *name;VqeafDeviceClass device_class;int (*probe)(void *device);int (*remove)(void *device); } VqeafDriver;
typedef struct { const VqeafDriver *driver;void *device;VqeafDriverState state;int last_error; } VqeafDriverInstance;
int vqeaf_driver_register(const VqeafDriver *driver);int vqeaf_driver_bind(const char *name,void *device,uint32_t *instance_id);int vqeaf_driver_unbind(uint32_t instance_id);int vqeaf_driver_get(uint32_t instance_id,VqeafDriverInstance *out);void vqeaf_driver_reset(void);
#endif
