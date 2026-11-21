#ifndef VQEAF_DISCOVERY_H
#define VQEAF_DISCOVERY_H
#include <stdint.h>
#include "driver.h"
#include "capability.h"
/* Device discovery: bus enumerators report what firmware/bus probing sees;
 * discovery matches drivers by name, binds them and registers capability
 * entries. No enumerator may claim READY without a successful driver probe. */
#define VQEAF_BUS_MAX 8u
#define VQEAF_DISCOVER_MAX 32u
#define VQEAF_DISC_NAME_MAX 32u
typedef enum { VQEAF_DISC_OK=0,VQEAF_DISC_BLOCKED_FIRMWARE,VQEAF_DISC_BLOCKED_HARDWARE } VqeafDiscoveryStatus;
typedef struct {
 VqeafDeviceClass device_class;
 char name[VQEAF_DISC_NAME_MAX];  /* matches VqeafDriver.name when a driver exists */
 char bus[16];
 VqeafDiscoveryStatus status;     /* blocked devices never get a driver bind */
} VqeafDiscoveredDevice;
typedef int (*VqeafBusEnumerate)(const char *bus_id,VqeafDiscoveredDevice *out,uint32_t max);
int vqeaf_discovery_register_bus(const char *bus_id,VqeafBusEnumerate fn);
int vqeaf_discovery_scan(void);                    /* returns devices processed */
int vqeaf_discovery_device_count(void);
void vqeaf_discovery_reset(void);
#endif
