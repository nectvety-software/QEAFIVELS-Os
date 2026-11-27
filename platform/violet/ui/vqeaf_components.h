/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_COMPONENTS_H
#define VQEAF_COMPONENTS_H

#include "lvgl.h"
#include "vqeaf_theme.h"

#define VQEAF_LG_MAX_ITEMS 64

typedef struct {
    const char *text;
    const char *symbol;
    int disabled;
} vqeaf_component_item_t;

/* Item strings are copied; constructors return NULL on invalid input or allocation failure. */
/* Effects approximate refraction with clipped gradients and transforms. */
lv_obj_t *vqeaf_lg_button(lv_obj_t *parent, const char *text,
                          vqeaf_m3_button_style_t style);
lv_obj_t *vqeaf_lg_action_button(lv_obj_t *parent, const char *symbol,
                                 lv_event_cb_t cb, void *user_data);
lv_obj_t *vqeaf_lg_tabs(lv_obj_t *parent, const char *const *labels,
                        int count, int selected);
void vqeaf_lg_tabs_select(lv_obj_t *tabs, int index);
int vqeaf_lg_tabs_selected(lv_obj_t *tabs);
lv_obj_t *vqeaf_lg_tab_item(lv_obj_t *tabs, int index);
lv_obj_t *vqeaf_lg_badge(lv_obj_t *parent, const char *text);
lv_obj_t *vqeaf_lg_slide_confirm(lv_obj_t *parent, const char *label,
                                 lv_event_cb_t cb, void *user_data);
int vqeaf_lg_slide_confirmed(lv_obj_t *control);
void vqeaf_lg_slide_reset(lv_obj_t *control);
lv_obj_t *vqeaf_lg_card(lv_obj_t *parent, int32_t width, int32_t height);
lv_obj_t *vqeaf_lg_slider(lv_obj_t *parent, int value);
void vqeaf_lg_slider_set(lv_obj_t *slider, int value);
int vqeaf_lg_slider_get(lv_obj_t *slider);
lv_obj_t *vqeaf_lg_switch(lv_obj_t *parent, int on);
void vqeaf_lg_switch_set(lv_obj_t *control, int on);
int vqeaf_lg_switch_get(lv_obj_t *control);
lv_obj_t *vqeaf_lg_checkbox(lv_obj_t *parent, const char *label, int checked);
void vqeaf_lg_checkbox_set(lv_obj_t *control, int checked);
int vqeaf_lg_checkbox_get(lv_obj_t *control);

lv_obj_t *vqeaf_lg_select(lv_obj_t *parent,
                          const vqeaf_component_item_t *items, int count,
                          int selected, lv_event_cb_t cb, void *user_data);
lv_obj_t *vqeaf_lg_button_group(lv_obj_t *parent, const char *const *labels,
                                int count, int selected);
lv_obj_t *vqeaf_lg_button_group_item(lv_obj_t *group, int index);
void vqeaf_lg_button_group_select(lv_obj_t *group, int index);
int vqeaf_lg_button_group_selected(lv_obj_t *group);
lv_obj_t *vqeaf_lg_dropdown(lv_obj_t *parent, const char *label,
                            const vqeaf_component_item_t *items, int count,
                            lv_event_cb_t cb, void *user_data);
lv_obj_t *vqeaf_lg_popover(lv_obj_t *parent, const char *title,
                           const char *message, const char *action);
lv_obj_t *vqeaf_lg_menu(lv_obj_t *parent, const char *label,
                        const vqeaf_component_item_t *items, int count,
                        int side, lv_event_cb_t cb, void *user_data);
void vqeaf_lg_select_set(lv_obj_t *control, int index);
int vqeaf_lg_selected_index(lv_obj_t *control);
void vqeaf_lg_overlay_open(lv_obj_t *control);
void vqeaf_lg_overlay_close(lv_obj_t *control);
int vqeaf_lg_overlay_visible(lv_obj_t *control);
int vqeaf_lg_any_overlay_visible(void);

#ifdef VQEAF_LG_TESTING
void vqeaf_lg_test_allocation_fault(unsigned int after);
void vqeaf_lg_test_object_fault(unsigned int after);
void vqeaf_lg_test_event_fault(unsigned int after);
void vqeaf_lg_test_async_fault(int enabled);
void vqeaf_lg_test_queue_close(lv_obj_t *control);
int vqeaf_lg_test_live_states(void);
void vqeaf_lg_test_clear_faults(void);
#endif

#endif
