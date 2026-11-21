#ifndef VQEAF_POWER_H
#define VQEAF_POWER_H
#include <stdint.h>
typedef enum { VQEAF_TIMEOUT_15S=0,VQEAF_TIMEOUT_1M,VQEAF_TIMEOUT_5M,VQEAF_TIMEOUT_30M,VQEAF_TIMEOUT_NEVER } VqeafScreenTimeout;
typedef enum { VQEAF_POWER_ACTIVE=0,VQEAF_POWER_DIMMED,VQEAF_POWER_DISPLAY_OFF } VqeafDisplayPowerState;
#define VQEAF_WAKELOCK_MAX 8u
#define VQEAF_WAKELOCK_NAME_MAX 24u
/* Power v0.3: wake locks, dim/blank display stages, battery-aware policy.
 * Battery data comes from a real provider only; without one the service
 * reports not-present and never invents values. */
typedef enum { VQEAF_BATTERY_ABSENT=0,VQEAF_BATTERY_DISCHARGING,VQEAF_BATTERY_CHARGING,VQEAF_BATTERY_FULL } VqeafBatteryState;
typedef struct { VqeafBatteryState state; uint32_t level_permille; uint32_t voltage_mv; uint8_t present; } VqeafBatteryStatus;
typedef struct { char owner[VQEAF_WAKELOCK_NAME_MAX]; uint64_t expires_ms; uint8_t until_released; } VqeafWakeLock;
typedef struct {
 VqeafScreenTimeout timeout;
 VqeafDisplayPowerState state;
 uint64_t last_activity_ms;
 uint8_t display_capable;
 uint8_t suspended;
 /* v0.3 additions */
 uint32_t dim_after_ms;          /* active -> dimmed after full timeout; 0 = no dim stage */
 uint32_t level_permille;        /* 0 = unknown */
 uint8_t charging;uint8_t battery_present;
 void (*blank_cb)(void *ctx,VqeafDisplayPowerState new_state);
 void *blank_ctx;
 VqeafWakeLock locks[VQEAF_WAKELOCK_MAX];
 uint32_t lock_count;
} VqeafPowerManager;
int vqeaf_power_init(VqeafPowerManager *pm,VqeafScreenTimeout timeout,int display_capable,uint64_t now_ms);
int vqeaf_power_set_timeout(VqeafPowerManager *pm,VqeafScreenTimeout timeout,uint64_t now_ms);
void vqeaf_power_user_activity(VqeafPowerManager *pm,uint64_t now_ms);
void vqeaf_power_tick(VqeafPowerManager *pm,uint64_t now_ms);
void vqeaf_power_set_suspended(VqeafPowerManager *pm,int suspended);
uint32_t vqeaf_power_timeout_ms(VqeafScreenTimeout timeout);
/* v0.3 API */
int vqeaf_power_set_dim_stage(VqeafPowerManager *pm,uint32_t dim_after_ms);
int vqeaf_power_set_blank_callback(VqeafPowerManager *pm,void (*cb)(void *ctx,VqeafDisplayPowerState),void *ctx);
int vqeaf_power_wake_lock(VqeafPowerManager *pm,const char *owner,uint64_t now_ms,uint64_t ttl_ms); /* ttl 0 = until released */
int vqeaf_power_wake_release(VqeafPowerManager *pm,const char *owner,uint64_t now_ms);
int vqeaf_power_wake_active(const VqeafPowerManager *pm);
void vqeaf_power_set_battery(VqeafPowerManager *pm,const VqeafBatteryStatus *status);
VqeafDisplayPowerState vqeaf_power_state(const VqeafPowerManager *pm);
#endif
