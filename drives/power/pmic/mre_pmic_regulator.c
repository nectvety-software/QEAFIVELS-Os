#include "mre_pmic_regulator.h"

mre_status_t mre_pmic_regulator_init(mre_pmic_regulator_t *r,
                                      mre_spmi_bus_t *bus,
                                      const mre_pmic_regulator_desc_t *d)
{
    if (r == NULL || bus == NULL || d == NULL || d->name == NULL ||
        d->sid > 0x0fu || d->enable_mask == 0u ||
        (d->enable_value & ~d->enable_mask) != 0u ||
        d->voltage_mask == 0u || d->step_uv == 0u ||
        d->min_uv == 0u || d->selector_max > 0xffu ||
        d->voltage_shift >= 8u ||
        ((uint32_t)d->voltage_mask >> d->voltage_shift) == 0u)
        return MRE_E_INVALID;
    r->bus = bus;
    r->desc = *d;
    return MRE_OK;
}

mre_status_t mre_pmic_regulator_get_enabled(mre_pmic_regulator_t *r,
                                             uint32_t *enabled)
{
    uint8_t value;
    mre_status_t st;
    if (r == NULL || r->bus == NULL || enabled == NULL) return MRE_E_INVALID;
    st = mre_spmi_read8(r->bus, r->desc.sid, r->desc.enable_address, &value);
    if (st != MRE_OK) return st;
    *enabled = ((value & r->desc.enable_mask) == r->desc.enable_value) ? 1u : 0u;
    return MRE_OK;
}

mre_status_t mre_pmic_regulator_set_enabled(mre_pmic_regulator_t *r,
                                             uint32_t enabled)
{
    uint8_t old_value, new_value, check;
    mre_status_t st;
    if (r == NULL || r->bus == NULL || enabled > 1u) return MRE_E_INVALID;
    if (r->desc.evidence_verified == 0u || r->bus->verified == 0u)
        return MRE_E_PERMISSION;
    st = mre_spmi_read8(r->bus, r->desc.sid, r->desc.enable_address, &old_value);
    if (st != MRE_OK) return st;
    new_value = enabled ? (uint8_t)((old_value & (uint8_t)~r->desc.enable_mask) |
                                    r->desc.enable_value)
                        : (uint8_t)(old_value & (uint8_t)~r->desc.enable_mask);
    st = mre_spmi_write8(r->bus, r->desc.sid, r->desc.enable_address, new_value);
    if (st != MRE_OK) {
        if (mre_spmi_write8(r->bus, r->desc.sid,
                            r->desc.enable_address, old_value) != MRE_OK)
            return MRE_E_IO;
        return st;
    }
    st = mre_spmi_read8(r->bus, r->desc.sid, r->desc.enable_address, &check);
    if (st == MRE_OK && (check & r->desc.enable_mask) ==
        (new_value & r->desc.enable_mask)) return MRE_OK;
    if (mre_spmi_write8(r->bus, r->desc.sid,
                        r->desc.enable_address, old_value) != MRE_OK)
        return MRE_E_IO;
    return (st == MRE_OK) ? MRE_E_IO : st;
}

mre_status_t mre_pmic_regulator_set_voltage(mre_pmic_regulator_t *r,
                                             uint32_t uv,
                                             uint32_t *actual_uv)
{
    uint32_t selector, rounded;
    uint8_t old_value, new_value, check;
    mre_status_t st;
    if (r == NULL || r->bus == NULL) return MRE_E_INVALID;
    if (r->desc.evidence_verified == 0u || r->bus->verified == 0u)
        return MRE_E_PERMISSION;
    if (uv < r->desc.min_uv) return MRE_E_INVALID;
    selector = (uv - r->desc.min_uv) / r->desc.step_uv;
    if (selector > r->desc.selector_max ||
        ((selector << r->desc.voltage_shift) &
         ~(uint32_t)r->desc.voltage_mask) != 0u)
        return MRE_E_INVALID;
    rounded = r->desc.min_uv + selector * r->desc.step_uv;
    if (rounded != uv) return MRE_E_INVALID;

    st = mre_spmi_read8(r->bus, r->desc.sid, r->desc.voltage_address, &old_value);
    if (st != MRE_OK) return st;
    new_value = (uint8_t)((old_value & (uint8_t)~r->desc.voltage_mask) |
                 ((selector << r->desc.voltage_shift) & r->desc.voltage_mask));
    st = mre_spmi_write8(r->bus, r->desc.sid, r->desc.voltage_address, new_value);
    if (st != MRE_OK) {
        if (mre_spmi_write8(r->bus, r->desc.sid,
                            r->desc.voltage_address, old_value) != MRE_OK)
            return MRE_E_IO;
        return st;
    }
    st = mre_spmi_read8(r->bus, r->desc.sid, r->desc.voltage_address, &check);
    if (st == MRE_OK && (check & r->desc.voltage_mask) ==
        (new_value & r->desc.voltage_mask)) {
        if (actual_uv != NULL) *actual_uv = rounded;
        return MRE_OK;
    }
    if (mre_spmi_write8(r->bus, r->desc.sid,
                        r->desc.voltage_address, old_value) != MRE_OK)
        return MRE_E_IO;
    return (st == MRE_OK) ? MRE_E_IO : st;
}
