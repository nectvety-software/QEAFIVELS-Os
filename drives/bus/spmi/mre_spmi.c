#include "mre_spmi.h"

mre_status_t mre_spmi_init(mre_spmi_bus_t *bus, const mre_spmi_ops_t *ops,
                            void *context, uint32_t timeout_us)
{
    if (bus == NULL || ops == NULL || ops->read8 == NULL ||
        ops->write8 == NULL || timeout_us == 0u)
        return MRE_E_INVALID;
    bus->ops = ops;
    bus->context = context;
    bus->timeout_us = timeout_us;
    bus->verified = 0u;
    return MRE_OK;
}

mre_status_t mre_spmi_read8(mre_spmi_bus_t *bus, uint8_t sid,
                             uint16_t address, uint8_t *value)
{
    if (bus == NULL || bus->ops == NULL || value == NULL || sid > 0x0fu)
        return MRE_E_INVALID;
    return bus->ops->read8(bus->context, sid, address, value, bus->timeout_us);
}

mre_status_t mre_spmi_write8(mre_spmi_bus_t *bus, uint8_t sid,
                              uint16_t address, uint8_t value)
{
    if (bus == NULL || bus->ops == NULL || sid > 0x0fu)
        return MRE_E_INVALID;
    if (bus->verified == 0u) return MRE_E_PERMISSION;
    return bus->ops->write8(bus->context, sid, address, value, bus->timeout_us);
}
