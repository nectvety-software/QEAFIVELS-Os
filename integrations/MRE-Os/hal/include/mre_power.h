#ifndef MRE_POWER_H
#define MRE_POWER_H
#include "mre_types.h"
typedef struct mre_power_state { int32_t battery_percent; int32_t temperature_millic; uint32_t voltage_uv; uint32_t flags; } mre_power_state_t;
typedef struct mre_power_ops {
    mre_status_t (*get_state)(mre_power_state_t *out);
    mre_status_t (*set_charging_limit)(uint32_t milliamps);
    mre_status_t (*suspend)(void);
} mre_power_ops_t;
#endif
