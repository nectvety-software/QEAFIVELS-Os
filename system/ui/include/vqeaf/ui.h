/* Minimal native UI layer: smartphone shell (lock screen, home grid, quick
 * settings, app screens) drawn directly on the GOP linear framebuffer. Also
 * hosts the full-screen update / fastboot views and the power-menu overlay.
 * No compositor, no widget toolkit, no hardware claims. */
#ifndef VQEAF_UI_H
#define VQEAF_UI_H
#include <stdint.h>
#include "update_screen.h"
#include "phone_shell.h"
int vqeaf_ui_init(uint64_t framebuffer_base,uint32_t width,uint32_t height,
                  uint32_t stride,uint32_t format,uint64_t free_pages,
                  uint32_t task_count);
/* Platform identity shown on the fastboot info card. Defaults describe the
 * x86_64 QEMU reference; a port overrides them so the screen never claims a
 * different machine. `footer` NULL keeps the reference footer line. */
void vqeaf_ui_set_platform(const char *device,const char *model,
                           const char *detail,const char *build,
                           const char *footer);
int vqeaf_ui_ready(void);
void vqeaf_ui_set_blanked(int blanked);
void vqeaf_ui_set_clock(int valid,uint32_t year,uint32_t month,uint32_t day,
                        uint32_t hour,uint32_t minute,uint32_t second);
void vqeaf_ui_set_power_status(uint32_t timeout_index,int keep_awake,
                               int battery_present,uint32_t permille,int charging);
void vqeaf_ui_set_storage_status(int mounted,uint64_t sectors,const char *path,
                                 const char *file,const char *summary);
void vqeaf_ui_update_show(uint32_t percent,const char *stage,int failed);
void vqeaf_ui_update_show_view(const VqeafUpdateView *view);
void vqeaf_ui_update_hide(void);
int vqeaf_ui_update_active(void);
void vqeaf_ui_show_home(void);
void vqeaf_ui_show_fastboot(void);
int vqeaf_ui_fastboot_active(void);
void vqeaf_ui_fastboot_nav(int delta);
uint32_t vqeaf_ui_fastboot_selected(void);
void vqeaf_ui_set_power_menu(int open,uint32_t selected);
/* Routed phone keys. Returns a VqeafPhoneAction, or -1 when the UI is down. */
int vqeaf_ui_phone_key(VqeafPhoneKey key);
/* Touch entry point in framebuffer pixels. Handles the power-menu overlay,
 * the update view and the fastboot view, then falls through to the shell
 * hit-test. Returns a VqeafPhoneAction, or -1 when the UI is down. */
int vqeaf_ui_tap(uint32_t x,uint32_t y);
/* Current phone-shell screen (a VqeafPhoneScreen), or -1 when another view
 * owns the framebuffer. Lets the kernel log screen transitions. */
int vqeaf_ui_phone_screen(void);
void vqeaf_ui_render(uint64_t pit_ticks);
#endif
