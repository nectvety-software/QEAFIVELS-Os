#ifndef VQEAF_POWER_MENU_H
#define VQEAF_POWER_MENU_H
#include <stdint.h>
#include "input.h"
/* Power menu modal: opened by a long power press, navigated with volume keys,
 * confirmed with a short power press, cancelled with another long press.
 * FASTBOOT is advertised as an option but reported UNSUPPORTED on targets
 * without a verified bootloader protocol — the menu never pretends. */
#define VQEAF_PMENU_ITEMS 4u
typedef enum { VQEAF_PMENU_CLOSED=0,VQEAF_PMENU_OPEN } VqeafPowerMenuState;
typedef enum {
 VQEAF_PMENU_NONE=0,VQEAF_PMENU_POWER_OFF,VQEAF_PMENU_RESTART,VQEAF_PMENU_FASTBOOT,VQEAF_PMENU_SLEEP
} VqeafPowerAction;
typedef struct { VqeafPowerMenuState state; uint32_t selected; } VqeafPowerMenu;
void vqeaf_pmenu_init(VqeafPowerMenu *m);
/* Programmatic open (e.g. the phone shell's POWER tile). */
void vqeaf_pmenu_open(VqeafPowerMenu *m);
/* Returns the confirmed action (menu closes), VQEAF_PMENU_NONE otherwise, -1 on bad args. */
int vqeaf_pmenu_handle(VqeafPowerMenu *m,const VqeafInputEvent *e);
const char *vqeaf_pmenu_item_name(uint32_t index); /* 0 on bad index */
VqeafPowerAction vqeaf_pmenu_item_action(uint32_t index);
#endif
