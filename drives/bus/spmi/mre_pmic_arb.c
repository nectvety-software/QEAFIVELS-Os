#include "mre_pmic_arb.h"
#include <string.h>

#define ARB_VERSION_OFFSET       0x0000u
#define ARB_APID_MAP_BASE        0x0900u
#define ARB_OWNERSHIP_BASE       0x0700u
#define ARB_CHANNEL_STRIDE       0x10000u
#define ARB_OBSERVER_STRIDE      0x00080u
#define ARB_CMD                  0x00u
#define ARB_STATUS               0x08u
#define ARB_WDATA0               0x10u
#define ARB_WDATA1               0x14u
#define ARB_RDATA0               0x18u
#define ARB_RDATA1               0x1cu
#define ARB_STATUS_DONE          (1u << 0)
#define ARB_STATUS_FAILURE       (1u << 1)
#define ARB_STATUS_DENIED        (1u << 2)
#define ARB_STATUS_DROPPED       (1u << 3)
#define ARB_PPID_MASK            0x0fffu
#define ARB_APID_VALID           0x8000u
#define ARB_OP_EXT_WRITE         2u
#define ARB_OP_EXT_READ          13u
#define ARB_OP_WRITE             14u
#define ARB_OP_READ              15u

static mre_status_t arb_read(mre_pmic_arb_t *a, mre_pmic_arb_region_t r,
                             uint32_t off, uint32_t *v)
{
    if (a == NULL || v == NULL || (off & 3u) != 0u ||
        (uint64_t)off + 4u > a->windows[r].size)
        return MRE_E_INVALID;
    return a->ops->read32(a->context, r, off, v);
}

static mre_status_t arb_write(mre_pmic_arb_t *a, mre_pmic_arb_region_t r,
                              uint32_t off, uint32_t v)
{
    if (a == NULL || (off & 3u) != 0u ||
        (uint64_t)off + 4u > a->windows[r].size)
        return MRE_E_INVALID;
    return a->ops->write32(a->context, r, off, v);
}

static mre_status_t arb_resolve_apid(mre_pmic_arb_t *a, uint8_t sid,
                                     uint16_t address, uint16_t *apid)
{
    uint16_t ppid;
    if (sid > 0x0fu || apid == NULL) return MRE_E_INVALID;
    ppid = (uint16_t)(((uint16_t)sid << 8) | (address >> 8));
    if (a->ppid_valid[ppid] == 0u) return MRE_E_NOT_FOUND;
    *apid = a->ppid_to_apid[ppid];
    return MRE_OK;
}

static mre_status_t arb_wait_done(mre_pmic_arb_t *a,
                                  mre_pmic_arb_region_t region,
                                  uint32_t channel_offset,
                                  uint32_t timeout_us,
                                  uint8_t *completed)
{
    uint64_t start = a->ops->time_us(a->context);
    uint32_t i, status = 0u;
    if (completed == NULL) return MRE_E_INVALID;
    *completed = 0u;
    for (i = 0u; i < a->max_polls; ++i) {
        mre_status_t st = arb_read(a, region, channel_offset + ARB_STATUS,
                                   &status);
        if (st != MRE_OK) return st;
        if ((status & ARB_STATUS_DONE) != 0u) {
            *completed = 1u;
            if ((status & ARB_STATUS_DENIED) != 0u) return MRE_E_PERMISSION;
            if ((status & (ARB_STATUS_FAILURE | ARB_STATUS_DROPPED)) != 0u)
                return MRE_E_IO;
            return MRE_OK;
        }
        if ((a->ops->time_us(a->context) - start) >= timeout_us)
            return MRE_E_BUSY;
        a->ops->delay_us(a->context, 1u);
    }
    return MRE_E_BUSY;
}

static uint32_t pack_word(const uint8_t *data, uint32_t count)
{
    uint32_t value = 0u, i;
    for (i = 0u; i < count; ++i)
        value |= (uint32_t)data[i] << (i * 8u);
    return value;
}

static void unpack_word(uint8_t *data, uint32_t count, uint32_t value)
{
    uint32_t i;
    for (i = 0u; i < count; ++i) {
        data[i] = (uint8_t)value;
        value >>= 8;
    }
}

static mre_status_t arb_transfer(mre_pmic_arb_t *a, uint8_t sid,
                                 uint16_t address, uint8_t *data,
                                 uint32_t length, uint8_t is_write,
                                 uint32_t timeout_us)
{
    uint16_t apid;
    uint32_t channel_offset, command, opcode, first, second;
    mre_pmic_arb_region_t region;
    mre_status_t st;
    uint8_t command_attempted = 0u;
    uint8_t command_completed = 0u;

    if (a == NULL || data == NULL || length == 0u || length > 8u ||
        a->probed == 0u)
        return MRE_E_INVALID;
    if (is_write && a->write_authorized == 0u) return MRE_E_PERMISSION;
    st = arb_resolve_apid(a, sid, address, &apid);
    if (st != MRE_OK) return st;
    if (is_write && a->apid_write_ee[apid] != a->execution_environment)
        return MRE_E_PERMISSION;

    if (is_write) {
        region = MRE_PMIC_ARB_CHANNELS;
        channel_offset = (uint32_t)apid * ARB_CHANNEL_STRIDE;
        opcode = (address > 0xffu) ? ARB_OP_EXT_WRITE : ARB_OP_WRITE;
    } else {
        region = MRE_PMIC_ARB_OBSERVER;
        channel_offset = (uint32_t)a->execution_environment * ARB_CHANNEL_STRIDE +
                         (uint32_t)apid * ARB_OBSERVER_STRIDE;
        opcode = (address > 0xffu) ? ARB_OP_EXT_READ : ARB_OP_READ;
    }
    if ((uint64_t)channel_offset + ARB_RDATA1 + 4u >
        a->windows[region].size)
        return MRE_E_INVALID;

    command = (opcode << 27) | ((uint32_t)(address & 0xffu) << 4) |
              ((length - 1u) & 0x7u);

    a->ops->lock(a->context);
    if (a->transaction_active != 0u || a->faulted != 0u) {
        a->ops->unlock(a->context);
        return MRE_E_BUSY;
    }
    a->transaction_active = 1u;

    if (is_write) {
        first = length > 4u ? 4u : length;
        st = arb_write(a, region, channel_offset + ARB_WDATA0,
                       pack_word(data, first));
        if (st == MRE_OK && length > 4u)
            st = arb_write(a, region, channel_offset + ARB_WDATA1,
                           pack_word(data + 4u, length - 4u));
    } else {
        st = MRE_OK;
    }
    if (st == MRE_OK && a->ops->barrier != NULL)
        a->ops->barrier(a->context);
    if (st == MRE_OK) {
        command_attempted = 1u;
        st = arb_write(a, region, channel_offset + ARB_CMD, command);
    }
    if (st == MRE_OK && a->ops->barrier != NULL)
        a->ops->barrier(a->context);
    if (st == MRE_OK)
        st = arb_wait_done(a, region, channel_offset, timeout_us,
                            &command_completed);

    if (st == MRE_OK && !is_write) {
        first = length > 4u ? 4u : length;
        st = arb_read(a, region, channel_offset + ARB_RDATA0, &first);
        if (st == MRE_OK) {
            unpack_word(data, length > 4u ? 4u : length, first);
            if (length > 4u) {
                st = arb_read(a, region, channel_offset + ARB_RDATA1, &second);
                if (st == MRE_OK) unpack_word(data + 4u, length - 4u, second);
            }
        }
    }

    if (command_attempted != 0u && command_completed == 0u &&
        st != MRE_OK && st != MRE_E_PERMISSION)
        a->faulted = 1u;
    a->transaction_active = 0u;
    a->ops->unlock(a->context);
    return st;
}

static mre_status_t spmi_arb_read8(void *context, uint8_t sid,
                                    uint16_t address, uint8_t *value,
                                    uint32_t timeout_us)
{
    mre_pmic_arb_t *a = (mre_pmic_arb_t *)context;
    if (a == NULL || timeout_us == 0u) return MRE_E_INVALID;
    return arb_transfer(a, sid, address, value, 1u, 0u, timeout_us);
}

static mre_status_t spmi_arb_write8(void *context, uint8_t sid,
                                     uint16_t address, uint8_t value,
                                     uint32_t timeout_us)
{
    mre_pmic_arb_t *a = (mre_pmic_arb_t *)context;
    if (a == NULL || timeout_us == 0u) return MRE_E_INVALID;
    return arb_transfer(a, sid, address, &value, 1u, 1u, timeout_us);
}

static const mre_spmi_ops_t s_pmic_arb_spmi_ops = {
    spmi_arb_read8,
    spmi_arb_write8
};

mre_status_t mre_pmic_arb_parse_fdt(const mre_fdt_t *fdt,
                                     mre_pmic_arb_window_t windows[4],
                                     uint8_t *execution_environment,
                                     uint8_t *channel)
{
    static const char *const expected_names[5] = {
        "core", "chnls", "obsrvr", "intr", "cnfg"
    };
    mre_fdt_node_t node, duplicate;
    mre_fdt_reg_t regs[5];
    const void *names_data;
    const uint8_t *names;
    uint32_t names_length, ee, ch, cursor = 0u, i, pos = 0u;
    mre_status_t st;

    if (fdt == NULL || windows == NULL || execution_environment == NULL ||
        channel == NULL)
        return MRE_E_INVALID;
    st = mre_fdt_find_compatible(fdt, "qcom,spmi-pmic-arb", &cursor, &node);
    if (st != MRE_OK) return st;
    st = mre_fdt_find_compatible(fdt, "qcom,spmi-pmic-arb", &cursor,
                                  &duplicate);
    if (st == MRE_OK) return MRE_E_EXISTS;
    if (st != MRE_E_NOT_FOUND) return st;

    st = mre_fdt_get_property(fdt, node, "reg-names",
                              &names_data, &names_length);
    if (st != MRE_OK) return st;
    names = (const uint8_t *)names_data;
    for (i = 0u; i < 5u; ++i) {
        size_t expected_length = strlen(expected_names[i]);
        const uint8_t *end;
        if (pos >= names_length) return MRE_E_INVALID;
        end = (const uint8_t *)memchr(names + pos, 0, names_length - pos);
        if (end == NULL ||
            (size_t)(end - (names + pos)) != expected_length ||
            memcmp(names + pos, expected_names[i], expected_length) != 0)
            return MRE_E_INVALID;
        pos = (uint32_t)(end - names) + 1u;
    }
    if (pos != names_length) return MRE_E_INVALID;

    for (i = 0u; i < 5u; ++i) {
        st = mre_fdt_get_reg(fdt, node, i, &regs[i]);
        if (st != MRE_OK || regs[i].address == 0u || regs[i].size == 0u)
            return (st == MRE_OK) ? MRE_E_INVALID : st;
    }
    if (regs[0].size > UINT32_MAX || regs[1].size > UINT32_MAX ||
        regs[2].size > UINT32_MAX || regs[4].size > UINT32_MAX)
        return MRE_E_INVALID;

    st = mre_fdt_get_u32(fdt, node, "qcom,ee", &ee);
    if (st != MRE_OK || ee > 5u) return MRE_E_INVALID;
    st = mre_fdt_get_u32(fdt, node, "qcom,channel", &ch);
    if (st != MRE_OK || ch > 5u) return MRE_E_INVALID;

    windows[MRE_PMIC_ARB_CORE].base = regs[0].address;
    windows[MRE_PMIC_ARB_CORE].size = (uint32_t)regs[0].size;
    windows[MRE_PMIC_ARB_CHANNELS].base = regs[1].address;
    windows[MRE_PMIC_ARB_CHANNELS].size = (uint32_t)regs[1].size;
    windows[MRE_PMIC_ARB_OBSERVER].base = regs[2].address;
    windows[MRE_PMIC_ARB_OBSERVER].size = (uint32_t)regs[2].size;
    windows[MRE_PMIC_ARB_CONFIG].base = regs[4].address;
    windows[MRE_PMIC_ARB_CONFIG].size = (uint32_t)regs[4].size;
    *execution_environment = (uint8_t)ee;
    *channel = (uint8_t)ch;
    return MRE_OK;
}

mre_status_t mre_pmic_arb_init(
    mre_pmic_arb_t *a, const mre_pmic_arb_io_ops_t *ops, void *context,
    const mre_pmic_arb_window_t windows[4], uint8_t ee, uint8_t channel,
    uint32_t timeout_us, uint32_t max_polls, uint8_t resources_verified)
{
    uint32_t i;
    if (a == NULL || ops == NULL || windows == NULL ||
        ops->read32 == NULL || ops->write32 == NULL ||
        ops->time_us == NULL || ops->delay_us == NULL ||
        ops->lock == NULL || ops->unlock == NULL ||
        ee > 5u || channel > 5u || timeout_us == 0u || max_polls == 0u ||
        resources_verified != 1u)
        return MRE_E_INVALID;
    for (i = 0u; i < 4u; ++i) {
        if (windows[i].base == 0u || windows[i].size < 4u ||
            windows[i].base > UINT64_MAX - windows[i].size)
            return MRE_E_INVALID;
    }
    if (windows[MRE_PMIC_ARB_CORE].size < 0x1100u ||
        windows[MRE_PMIC_ARB_CONFIG].size < 0x0f00u ||
        windows[MRE_PMIC_ARB_CHANNELS].size <
            ((MRE_PMIC_ARB_MAX_APIDS - 1u) * ARB_CHANNEL_STRIDE + 0x20u) ||
        windows[MRE_PMIC_ARB_OBSERVER].size <
            ((uint32_t)ee * ARB_CHANNEL_STRIDE +
             MRE_PMIC_ARB_MAX_APIDS * ARB_OBSERVER_STRIDE + 0x20u))
        return MRE_E_INVALID;

    memset(a, 0, sizeof(*a));
    a->ops = ops;
    a->context = context;
    for (i = 0u; i < 4u; ++i) a->windows[i] = windows[i];
    a->execution_environment = ee;
    a->channel = channel;
    a->timeout_us = timeout_us;
    a->max_polls = max_polls;
    a->resources_verified = 1u;
    return MRE_OK;
}

mre_status_t mre_pmic_arb_probe(mre_pmic_arb_t *a)
{
    uint32_t version, i, map_value, owner_value;
    uint16_t ppid;
    uint32_t entries = 0u;
    mre_status_t st;
    if (a == NULL || a->ops == NULL || a->resources_verified == 0u)
        return MRE_E_PERMISSION;
    if (a->faulted != 0u) return MRE_E_BUSY;
    a->probed = 0u;
    a->write_authorized = 0u;
    memset(a->ppid_to_apid, 0, sizeof(a->ppid_to_apid));
    memset(a->ppid_valid, 0, sizeof(a->ppid_valid));
    memset(a->apid_write_ee, 0xff, sizeof(a->apid_write_ee));

    st = arb_read(a, MRE_PMIC_ARB_CORE, ARB_VERSION_OFFSET, &version);
    if (st != MRE_OK) return st;
    if (version < MRE_PMIC_ARB_VERSION_V5_MIN) return MRE_E_UNSUPPORTED;

    for (i = 0u; i < MRE_PMIC_ARB_MAX_APIDS; ++i) {
        st = arb_read(a, MRE_PMIC_ARB_CORE, ARB_APID_MAP_BASE + i * 4u,
                      &map_value);
        if (st != MRE_OK) return st;
        if (map_value == 0u) continue;
        ppid = (uint16_t)((map_value >> 8) & ARB_PPID_MASK);
        st = arb_read(a, MRE_PMIC_ARB_CONFIG,
                      ARB_OWNERSHIP_BASE + i * 4u, &owner_value);
        if (st != MRE_OK) return st;
        a->apid_write_ee[i] = (uint8_t)(owner_value & 0x7u);
        if (a->ppid_valid[ppid] == 0u ||
            a->apid_write_ee[i] == a->execution_environment) {
            a->ppid_to_apid[ppid] = (uint16_t)i;
            a->ppid_valid[ppid] = 1u;
        }
        ++entries;
    }
    if (entries == 0u) return MRE_E_NOT_FOUND;
    a->version = version;
    a->probed = 1u;
    return MRE_OK;
}

mre_status_t mre_pmic_arb_bind_spmi(mre_pmic_arb_t *a,
                                     mre_spmi_bus_t *bus,
                                     uint32_t timeout_us)
{
    if (a == NULL || bus == NULL || a->probed == 0u ||
        a->faulted != 0u || timeout_us == 0u)
        return MRE_E_INVALID;
    return mre_spmi_init(bus, &s_pmic_arb_spmi_ops, a, timeout_us);
}

mre_status_t mre_pmic_arb_authorize_writes(mre_pmic_arb_t *a,
                                            mre_spmi_bus_t *bus,
                                            uint8_t owner_verified,
                                            uint8_t explicit_authorization)
{
    if (a == NULL || bus == NULL || bus->context != a ||
        bus->ops != &s_pmic_arb_spmi_ops || a->probed == 0u ||
        a->faulted != 0u || owner_verified != 1u ||
        explicit_authorization != 1u)
        return MRE_E_PERMISSION;
    a->write_authorized = 1u;
    bus->verified = 1u;
    return MRE_OK;
}
