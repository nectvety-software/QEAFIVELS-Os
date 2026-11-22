/* SPDX-License-Identifier: MIT */
/* vqeaf_tools_ui.h - taskbar and system utility pages for the violet shell. */
#ifndef VQEAF_TOOLS_UI_H
#define VQEAF_TOOLS_UI_H

#include "vqeaf_ui.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VQEAF_TASKBAR_H 64

/* Taskbar is shell chrome. It is built once and refreshed on page changes. */
void vqeaf_tools_taskbar_create(lv_obj_t *screen);
void vqeaf_tools_taskbar_refresh(vqeaf_page_t current);
int vqeaf_tools_taskbar_visible(vqeaf_page_t page);

/* Page builders, wired into vqeaf_pages_build(). */
void vqeaf_tools_build_ntfs_search(lv_obj_t *content);
void vqeaf_tools_build_task_manager(lv_obj_t *content);
void vqeaf_tools_build_disk_cleanup(lv_obj_t *content);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_TOOLS_UI_H */
