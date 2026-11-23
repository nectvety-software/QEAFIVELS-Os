#ifndef MRE_DEVICE_H
#define MRE_DEVICE_H

#include "mre_types.h"

typedef enum mre_bus_type {
    MRE_BUS_PLATFORM = 0,
    MRE_BUS_MMIO,
    MRE_BUS_I2C,
    MRE_BUS_SPI,
    MRE_BUS_USB,
    MRE_BUS_PCI,
    MRE_BUS_VIRTUAL
} mre_bus_type_t;

typedef struct mre_device {
    const char *name;
    const char *compatible;
    mre_bus_type_t bus;
    uintptr_t mmio_base;
    size_t mmio_size;
    uint32_t irq;
    void *platform_data;
} mre_device_t;

#endif
