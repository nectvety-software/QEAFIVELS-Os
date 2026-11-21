/* Recovery Mode menu and Fastboot Mode device screen. Both are pure
 * renderers: selection and availability come from the caller, and items that
 * have no working backend are drawn as disabled instead of pretending. */
#ifndef VQEAF_RECOVERY_SCREEN_H
#define VQEAF_RECOVERY_SCREEN_H
#include "gfx.h"

typedef enum {
    VQEAF_RECOVERY_REBOOT_SYSTEM = 0,
    VQEAF_RECOVERY_APPLY_UPDATE,
    VQEAF_RECOVERY_WIPE_DATA,
    VQEAF_RECOVERY_WIPE_CACHE,
    VQEAF_RECOVERY_ADVANCED,
    VQEAF_RECOVERY_POWER_OFF,
    VQEAF_RECOVERY_ITEM_COUNT
} VqeafRecoveryItem;

typedef struct {
    const char *device;
    const char *model;
    const char *version;
    const char *build;
    const char *detail;
    const char *footer;    /* NULL = the x86_64 reference default */
} VqeafDeviceInfo;

typedef enum {
    VQEAF_FASTBOOT_CONTINUE = 0,
    VQEAF_FASTBOOT_RECOVERY,
    VQEAF_FASTBOOT_UPDATE,
    VQEAF_FASTBOOT_FACTORY_RESET,
    VQEAF_FASTBOOT_POWER_OFF,
    VQEAF_FASTBOOT_ITEM_COUNT
} VqeafFastbootItem;

void vqeaf_recovery_screen_render(const VqeafFramebuffer *fb, uint32_t selected,
                                  const char *status);
const char *vqeaf_recovery_item_label(uint32_t index);
const char *vqeaf_recovery_item_note(uint32_t index);
int vqeaf_recovery_item_available(uint32_t index);

void vqeaf_fastboot_screen_render(const VqeafFramebuffer *fb,
                                  const VqeafDeviceInfo *info, uint32_t selected,
                                  const char *status);
const char *vqeaf_fastboot_item_label(uint32_t index);
int vqeaf_fastboot_item_available(uint32_t index);
#endif
