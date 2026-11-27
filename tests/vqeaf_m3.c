/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_m3.c - host gate for the Material Design 3 core (phase 2).
 *
 * Proves, with real LVGL layout, that the M3 type scale resolves to the
 * rasterised faces (display pinned bold, text roles honour bold-text mode),
 * and that the phase-2 components carry the M3 geometry/roles: top app bar
 * 64h, navigation bar 80h with the active-indicator pill, list item 56/72h,
 * dialog 40% scrim + radius-XL card, snackbar inverse surface, slider and
 * linear progress map 0..100 onto track/thumb geometry.
 * No device state is read; nothing here is part of the initramfs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "vqeaf_theme.h"
#include "vqeaf_components.h"

static int failures;

static void expect(const char *name, int got, int want)
{
    if(got != want) {
        printf("FAIL %s: got %d want %d\n", name, got, want);
        failures++;
    } else {
        printf("ok   %s\n", name);
    }
}

static void expect_near(const char *name, int got, int want, int tol)
{
    int d = got - want;
    if(d < 0) d = -d;
    if(d > tol) {
        printf("FAIL %s: got %d want %d (+-%d)\n", name, got, want, tol);
        failures++;
    } else {
        printf("ok   %s (%d)\n", name, got);
    }
}

static int ceq(lv_color_t a, lv_color_t b)
{
    return a.red == b.red && a.green == b.green && a.blue == b.blue;
}

static int component_clicks;

static void component_click_cb(lv_event_t *event)
{
    (void)event;
    component_clicks++;
}

static uint32_t *framebuffer;
static lv_display_t *display;
static int width = 720;
static int height = 1200;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area; (void)px_map;
    lv_display_flush_ready(disp);
}

static uint32_t test_tick;
static int reentrant_action;
static lv_obj_t *reentrant_anchor;
static lv_obj_t *reentrant_replacement;
static int reentrant_calls;

static uint32_t tick_cb(void) { return test_tick; }

static void step(uint32_t milliseconds)
{
    test_tick += milliseconds;
    lv_timer_handler();
}

static void reentrant_overlay_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if(code != LV_EVENT_VALUE_CHANGED || reentrant_action == 0) return;
    reentrant_calls++;
    if(reentrant_action == 1) {
        lv_obj_delete(reentrant_anchor);
    } else if(reentrant_action == 2) {
        vqeaf_lg_overlay_open(reentrant_replacement);
    } else if(reentrant_action == 3) {
        lv_obj_t *scrim = lv_obj_get_child(lv_layer_top(), 0);
        if(scrim != NULL) lv_obj_delete(scrim);
    }
    reentrant_action = 0;
}

static void click_overlay_item(int index)
{
    lv_obj_t *surface = lv_obj_get_child(lv_layer_top(), 2);
    lv_obj_t *item = surface != NULL ? lv_obj_get_child(surface, index) : NULL;

    if(item != NULL) lv_obj_send_event(item, LV_EVENT_CLICKED, NULL);
}

#define EXPECT_CONSTRUCTOR_SWEEP(name, expression, setter, limit) do { \
    uint32_t base_children = lv_obj_get_child_count(parent); \
    int base_states = vqeaf_lg_test_live_states(); \
    int clean = 1; \
    int succeeded = 0; \
    unsigned int fault; \
    for(fault = 1; fault <= (limit) && !succeeded; fault++) { \
        lv_obj_t *made; \
        setter(fault); \
        made = (expression); \
        vqeaf_lg_test_clear_faults(); \
        if(made == NULL) { \
            if(lv_obj_get_child_count(parent) != base_children || \
               vqeaf_lg_test_live_states() != base_states) clean = 0; \
        } else { \
            lv_obj_delete(made); \
            if(lv_obj_get_child_count(parent) != base_children || \
               vqeaf_lg_test_live_states() != base_states) clean = 0; \
            succeeded = 1; \
        } \
    } \
    if(!succeeded) clean = 0; \
    expect(name, clean, 1); \
} while(0)

#define EXPECT_OVERLAY_SWEEP(name, setter, limit) do { \
    uint32_t base_layer_children = lv_obj_get_child_count(lv_layer_top()); \
    int base_states = vqeaf_lg_test_live_states(); \
    int clean = 1; \
    int succeeded = 0; \
    unsigned int fault; \
    for(fault = 1; fault <= (limit) && !succeeded; fault++) { \
        lv_obj_t *control = vqeaf_lg_select(parent, sweep_items, 3, 0, NULL, NULL); \
        setter(fault); \
        vqeaf_lg_overlay_open(control); \
        vqeaf_lg_test_clear_faults(); \
        if(vqeaf_lg_overlay_visible(control)) { \
            lv_obj_delete(control); \
            if(lv_obj_get_child_count(lv_layer_top()) != base_layer_children || \
               vqeaf_lg_test_live_states() != base_states) clean = 0; \
            succeeded = 1; \
        } else { \
            if(lv_obj_get_child_count(lv_layer_top()) != base_layer_children || \
               vqeaf_lg_test_live_states() != base_states + 1) clean = 0; \
            lv_obj_delete(control); \
            if(vqeaf_lg_test_live_states() != base_states) clean = 0; \
        } \
    } \
    if(!succeeded) clean = 0; \
    expect(name, clean, 1); \
} while(0)

static void test_liquid_hardening(lv_obj_t *screen)
{
    static const char *const labels[] = { "Một", "Hai", "Ba" };
    static const char *const too_many_labels[VQEAF_LG_MAX_ITEMS + 1] = { 0 };
    static const vqeaf_component_item_t sweep_items[] = {
        { "Tự động", "A", 0 },
        { "Sáng", "B", 0 },
        { "Tắt", NULL, 1 },
    };
    static const vqeaf_component_item_t too_many_items[VQEAF_LG_MAX_ITEMS + 1] = { 0 };
    lv_obj_t *parent = lv_obj_create(screen);
    lv_obj_t *plain, *slider, *toggle, *check, *tabs, *group, *select;
    lv_obj_t *control, *replacement, *geometry;
    char copied_text[3][24] = { "Tự động", "Sáng", "Tắt" };
    char copied_symbol[3][8] = { "A", "B", "C" };
    vqeaf_component_item_t copied_items[] = {
        { copied_text[0], copied_symbol[0], 0 },
        { copied_text[1], copied_symbol[1], 0 },
        { copied_text[2], copied_symbol[2], 1 },
    };
    int old_width, old_height, old_screen_width, old_screen_height;
    int i;

    lv_obj_remove_style_all(parent);
    lv_obj_set_pos(parent, 0, 0);
    lv_obj_set_size(parent, 600, 900);
    expect("liquid hardening starts without live state",
           vqeaf_lg_test_live_states(), 0);

    expect("liquid constructors reject NULL parent",
           vqeaf_lg_button(NULL, "X", VQEAF_M3_FILLED) == NULL &&
           vqeaf_lg_action_button(NULL, "X", NULL, NULL) == NULL &&
           vqeaf_lg_tabs(NULL, labels, 3, 0) == NULL &&
           vqeaf_lg_badge(NULL, "X") == NULL &&
           vqeaf_lg_slide_confirm(NULL, "X", NULL, NULL) == NULL &&
           vqeaf_lg_card(NULL, 10, 10) == NULL &&
           vqeaf_lg_slider(NULL, 0) == NULL &&
           vqeaf_lg_switch(NULL, 0) == NULL &&
           vqeaf_lg_checkbox(NULL, "X", 0) == NULL &&
           vqeaf_lg_select(NULL, sweep_items, 3, 0, NULL, NULL) == NULL &&
           vqeaf_lg_button_group(NULL, labels, 3, 0) == NULL &&
           vqeaf_lg_dropdown(NULL, "X", sweep_items, 3, NULL, NULL) == NULL &&
           vqeaf_lg_popover(NULL, "X", "Y", "Z") == NULL &&
           vqeaf_lg_menu(NULL, "X", sweep_items, 3, 0, NULL, NULL) == NULL, 1);
    expect("liquid constructors reject invalid item arrays and counts",
           vqeaf_lg_tabs(parent, NULL, 3, 0) == NULL &&
           vqeaf_lg_tabs(parent, labels, 0, 0) == NULL &&
           vqeaf_lg_tabs(parent, labels, -1, 0) == NULL &&
           vqeaf_lg_tabs(parent, too_many_labels, VQEAF_LG_MAX_ITEMS + 1, 0) == NULL &&
           vqeaf_lg_button_group(parent, NULL, 3, 0) == NULL &&
           vqeaf_lg_button_group(parent, labels, 0, 0) == NULL &&
           vqeaf_lg_button_group(parent, too_many_labels, VQEAF_LG_MAX_ITEMS + 1, 0) == NULL &&
           vqeaf_lg_select(parent, NULL, 1, 0, NULL, NULL) == NULL &&
           vqeaf_lg_select(parent, sweep_items, 0, 0, NULL, NULL) == NULL &&
           vqeaf_lg_select(parent, sweep_items, -1, 0, NULL, NULL) == NULL &&
           vqeaf_lg_select(parent, too_many_items, VQEAF_LG_MAX_ITEMS + 1, 0,
                           NULL, NULL) == NULL &&
           vqeaf_lg_dropdown(parent, "X", NULL, 1, NULL, NULL) == NULL &&
           vqeaf_lg_dropdown(parent, "X", too_many_items, VQEAF_LG_MAX_ITEMS + 1,
                             NULL, NULL) == NULL &&
           vqeaf_lg_menu(parent, "X", NULL, 1, 0, NULL, NULL) == NULL &&
           vqeaf_lg_menu(parent, "X", too_many_items, VQEAF_LG_MAX_ITEMS + 1,
                         0, NULL, NULL) == NULL, 1);

    EXPECT_CONSTRUCTOR_SWEEP("liquid button allocation failures clean partial roots",
        (vqeaf_lg_button(parent, "X", VQEAF_M3_FILLED)),
        vqeaf_lg_test_allocation_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid action allocation failures clean partial roots",
        (vqeaf_lg_action_button(parent, "X", component_click_cb, NULL)),
        vqeaf_lg_test_allocation_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid tabs allocation failures clean partial roots",
        (vqeaf_lg_tabs(parent, labels, 3, 1)),
        vqeaf_lg_test_allocation_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid slide allocation failures clean partial roots",
        (vqeaf_lg_slide_confirm(parent, "X", component_click_cb, NULL)),
        vqeaf_lg_test_allocation_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid slider allocation failures clean partial roots",
        (vqeaf_lg_slider(parent, 50)),
        vqeaf_lg_test_allocation_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid switch allocation failures clean partial roots",
        (vqeaf_lg_switch(parent, 0)),
        vqeaf_lg_test_allocation_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid checkbox allocation failures clean partial roots",
        (vqeaf_lg_checkbox(parent, "X", 0)),
        vqeaf_lg_test_allocation_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid select allocation failures clean copied items",
        (vqeaf_lg_select(parent, sweep_items, 3, 0, component_click_cb, NULL)),
        vqeaf_lg_test_allocation_fault, 32);
    EXPECT_CONSTRUCTOR_SWEEP("liquid group allocation failures clean partial roots",
        (vqeaf_lg_button_group(parent, labels, 3, 0)),
        vqeaf_lg_test_allocation_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid dropdown allocation failures clean copied items",
        (vqeaf_lg_dropdown(parent, "X", sweep_items, 3, component_click_cb, NULL)),
        vqeaf_lg_test_allocation_fault, 32);
    EXPECT_CONSTRUCTOR_SWEEP("liquid popover allocation failures clean copied items",
        (vqeaf_lg_popover(parent, "X", "Y", "Z")),
        vqeaf_lg_test_allocation_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid menu allocation failures clean copied items",
        (vqeaf_lg_menu(parent, "X", sweep_items, 3, 0, component_click_cb, NULL)),
        vqeaf_lg_test_allocation_fault, 32);

    EXPECT_CONSTRUCTOR_SWEEP("liquid button object failures clean partial roots",
        (vqeaf_lg_button(parent, "X", VQEAF_M3_FILLED)),
        vqeaf_lg_test_object_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid action object failures clean partial roots",
        (vqeaf_lg_action_button(parent, "X", component_click_cb, NULL)),
        vqeaf_lg_test_object_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid tabs object failures clean partial roots",
        (vqeaf_lg_tabs(parent, labels, 3, 1)),
        vqeaf_lg_test_object_fault, 32);
    EXPECT_CONSTRUCTOR_SWEEP("liquid badge object failures clean partial roots",
        (vqeaf_lg_badge(parent, "X")),
        vqeaf_lg_test_object_fault, 8);
    EXPECT_CONSTRUCTOR_SWEEP("liquid slide object failures clean partial roots",
        (vqeaf_lg_slide_confirm(parent, "X", component_click_cb, NULL)),
        vqeaf_lg_test_object_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid card object failure leaves no root",
        (vqeaf_lg_card(parent, 100, 100)),
        vqeaf_lg_test_object_fault, 4);
    EXPECT_CONSTRUCTOR_SWEEP("liquid slider object failures clean partial roots",
        (vqeaf_lg_slider(parent, 50)),
        vqeaf_lg_test_object_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid switch object failures clean partial roots",
        (vqeaf_lg_switch(parent, 0)),
        vqeaf_lg_test_object_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid checkbox object failures clean partial roots",
        (vqeaf_lg_checkbox(parent, "X", 0)),
        vqeaf_lg_test_object_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid select object failures clean partial roots",
        (vqeaf_lg_select(parent, sweep_items, 3, 0, component_click_cb, NULL)),
        vqeaf_lg_test_object_fault, 32);
    EXPECT_CONSTRUCTOR_SWEEP("liquid group object failures clean partial roots",
        (vqeaf_lg_button_group(parent, labels, 3, 0)),
        vqeaf_lg_test_object_fault, 32);
    EXPECT_CONSTRUCTOR_SWEEP("liquid dropdown object failures clean partial roots",
        (vqeaf_lg_dropdown(parent, "X", sweep_items, 3, component_click_cb, NULL)),
        vqeaf_lg_test_object_fault, 32);
    EXPECT_CONSTRUCTOR_SWEEP("liquid popover object failures clean partial roots",
        (vqeaf_lg_popover(parent, "X", "Y", "Z")),
        vqeaf_lg_test_object_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid menu object failures clean partial roots",
        (vqeaf_lg_menu(parent, "X", sweep_items, 3, 0, component_click_cb, NULL)),
        vqeaf_lg_test_object_fault, 32);

    EXPECT_CONSTRUCTOR_SWEEP("liquid button event failures clean partial roots",
        (vqeaf_lg_button(parent, "X", VQEAF_M3_FILLED)),
        vqeaf_lg_test_event_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid action event failures clean partial roots",
        (vqeaf_lg_action_button(parent, "X", component_click_cb, NULL)),
        vqeaf_lg_test_event_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid tabs event failures clean partial roots",
        (vqeaf_lg_tabs(parent, labels, 3, 1)),
        vqeaf_lg_test_event_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid slide event failures clean partial roots",
        (vqeaf_lg_slide_confirm(parent, "X", component_click_cb, NULL)),
        vqeaf_lg_test_event_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid slider event failures clean partial roots",
        (vqeaf_lg_slider(parent, 50)),
        vqeaf_lg_test_event_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid switch event failures clean partial roots",
        (vqeaf_lg_switch(parent, 0)),
        vqeaf_lg_test_event_fault, 8);
    EXPECT_CONSTRUCTOR_SWEEP("liquid checkbox event failures clean partial roots",
        (vqeaf_lg_checkbox(parent, "X", 0)),
        vqeaf_lg_test_event_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid select event failures clean partial roots",
        (vqeaf_lg_select(parent, sweep_items, 3, 0, component_click_cb, NULL)),
        vqeaf_lg_test_event_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid group event failures clean partial roots",
        (vqeaf_lg_button_group(parent, labels, 3, 0)),
        vqeaf_lg_test_event_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid dropdown event failures clean partial roots",
        (vqeaf_lg_dropdown(parent, "X", sweep_items, 3, component_click_cb, NULL)),
        vqeaf_lg_test_event_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid popover event failures clean partial roots",
        (vqeaf_lg_popover(parent, "X", "Y", "Z")),
        vqeaf_lg_test_event_fault, 16);
    EXPECT_CONSTRUCTOR_SWEEP("liquid menu event failures clean partial roots",
        (vqeaf_lg_menu(parent, "X", sweep_items, 3, 0, component_click_cb, NULL)),
        vqeaf_lg_test_event_fault, 16);

    EXPECT_OVERLAY_SWEEP("liquid overlay object failures clean every partial layer",
        vqeaf_lg_test_object_fault, 64);
    EXPECT_OVERLAY_SWEEP("liquid overlay event failures clean every partial layer",
        vqeaf_lg_test_event_fault, 32);
    control = vqeaf_lg_select(parent, sweep_items, 3, 0, NULL, NULL);
    vqeaf_lg_test_allocation_fault(1);
    vqeaf_lg_overlay_open(control);
    vqeaf_lg_test_clear_faults();
    expect("liquid overlay context allocation failure leaves no layer",
           vqeaf_lg_overlay_visible(control) == 0 &&
           lv_obj_get_child_count(lv_layer_top()) == 0, 1);
    lv_obj_delete(control);

    for(i = 1; i <= 3; i++) {
        char name[80];

        reentrant_anchor = vqeaf_lg_select(parent, sweep_items, 3, 0,
                                           reentrant_overlay_cb, NULL);
        reentrant_replacement = vqeaf_lg_dropdown(parent, "Thay thế",
                                                  sweep_items, 3, NULL, NULL);
        vqeaf_lg_overlay_open(reentrant_anchor);
        reentrant_action = i;
        reentrant_calls = 0;
        click_overlay_item(0);
        snprintf(name, sizeof(name), "liquid overlay reentrant action %d invokes once", i);
        expect(name, reentrant_calls, 1);
        if(i == 2) {
            expect("liquid callback can replace overlay without stale dismissal",
                   vqeaf_lg_overlay_visible(reentrant_replacement), 1);
            vqeaf_lg_overlay_close(reentrant_replacement);
        } else {
            expect("liquid callback destruction removes old overlay",
                   vqeaf_lg_any_overlay_visible(), 0);
        }
        if(i != 1) lv_obj_delete(reentrant_anchor);
        lv_obj_delete(reentrant_replacement);
        reentrant_anchor = NULL;
        reentrant_replacement = NULL;
        expect("liquid reentrant cleanup leaves no top-layer orphan",
               lv_obj_get_child_count(lv_layer_top()) == 0, 1);
    }

    control = vqeaf_lg_select(parent, sweep_items, 3, 0, NULL, NULL);
    vqeaf_lg_overlay_open(control);
    vqeaf_lg_test_queue_close(control);
    expect("liquid queued dismissal hides overlay immediately",
           vqeaf_lg_overlay_visible(control), 0);
    step(1);
    expect("liquid queued dismissal runs on next timer turn",
           lv_obj_get_child_count(lv_layer_top()) == 0, 1);
    lv_obj_delete(control);

    control = vqeaf_lg_select(parent, sweep_items, 3, 0, NULL, NULL);
    replacement = vqeaf_lg_dropdown(parent, "Thay thế", sweep_items, 3, NULL, NULL);
    vqeaf_lg_overlay_open(control);
    vqeaf_lg_test_queue_close(control);
    vqeaf_lg_overlay_open(replacement);
    step(5);
    expect("liquid cancelled async work cannot dismiss replacement",
           vqeaf_lg_overlay_visible(replacement) &&
           lv_obj_get_child_count(lv_layer_top()) == 3, 1);
    vqeaf_lg_overlay_close(replacement);
    lv_obj_delete(control);
    lv_obj_delete(replacement);

    control = vqeaf_lg_select(parent, sweep_items, 3, 0, NULL, NULL);
    vqeaf_lg_overlay_open(control);
    vqeaf_lg_test_async_fault(1);
    vqeaf_lg_test_queue_close(control);
    vqeaf_lg_test_clear_faults();
    expect("liquid async allocation failure falls back to safe destruction",
           vqeaf_lg_overlay_visible(control) == 0 &&
           lv_obj_get_child_count(lv_layer_top()) == 0, 1);
    lv_obj_delete(control);

    plain = lv_obj_create(parent);
    lv_obj_set_user_data(plain, (void *)(intptr_t)0x1234);
    slider = vqeaf_lg_slider(parent, 37);
    toggle = vqeaf_lg_switch(parent, 1);
    check = vqeaf_lg_checkbox(parent, "X", 1);
    tabs = vqeaf_lg_tabs(parent, labels, 3, 1);
    group = vqeaf_lg_button_group(parent, labels, 3, 1);
    select = vqeaf_lg_select(parent, sweep_items, 3, 1, NULL, NULL);
    vqeaf_lg_slider_set(plain, 99);
    vqeaf_lg_switch_set(plain, 0);
    vqeaf_lg_checkbox_set(plain, 0);
    vqeaf_lg_tabs_select(plain, 2);
    vqeaf_lg_button_group_select(plain, 2);
    vqeaf_lg_select_set(plain, 2);
    vqeaf_lg_overlay_open(plain);
    expect("liquid APIs ignore unrelated objects with forged user data",
           vqeaf_lg_slider_get(slider) == 37 &&
           vqeaf_lg_switch_get(toggle) == 1 &&
           vqeaf_lg_checkbox_get(check) == 1 &&
           vqeaf_lg_tabs_selected(tabs) == 1 &&
           vqeaf_lg_button_group_selected(group) == 1 &&
           vqeaf_lg_selected_index(select) == 1 &&
           vqeaf_lg_any_overlay_visible() == 0, 1);
    vqeaf_lg_slider_set(toggle, 99);
    vqeaf_lg_switch_set(slider, 0);
    vqeaf_lg_checkbox_set(tabs, 0);
    vqeaf_lg_tabs_select(group, 2);
    vqeaf_lg_button_group_select(select, 2);
    vqeaf_lg_select_set(check, 2);
    expect("liquid setters reject the wrong component kind",
           vqeaf_lg_slider_get(slider) == 37 &&
           vqeaf_lg_switch_get(toggle) == 1 &&
           vqeaf_lg_checkbox_get(check) == 1 &&
           vqeaf_lg_tabs_selected(tabs) == 1 &&
           vqeaf_lg_button_group_selected(group) == 1 &&
           vqeaf_lg_selected_index(select) == 1, 1);
    expect("liquid getters reject NULL and wrong kinds",
           vqeaf_lg_slider_get(NULL) == 0 &&
           vqeaf_lg_switch_get(NULL) == 0 &&
           vqeaf_lg_checkbox_get(NULL) == 0 &&
           vqeaf_lg_slide_confirmed(NULL) == 0 &&
           vqeaf_lg_tabs_selected(NULL) == -1 &&
           vqeaf_lg_button_group_selected(NULL) == -1 &&
           vqeaf_lg_selected_index(NULL) == -1 &&
           vqeaf_lg_slider_get(toggle) == 0 &&
           vqeaf_lg_switch_get(slider) == 0 &&
           vqeaf_lg_checkbox_get(tabs) == 0 &&
           vqeaf_lg_tabs_selected(group) == -1 &&
           vqeaf_lg_button_group_selected(select) == -1, 1);
    vqeaf_lg_overlay_close(NULL);
    vqeaf_lg_slide_reset(NULL);
    expect("liquid item accessors reject invalid indices and kinds",
           vqeaf_lg_tab_item(tabs, -1) == NULL &&
           vqeaf_lg_tab_item(tabs, 99) == NULL &&
           vqeaf_lg_tab_item(group, 0) == NULL &&
           vqeaf_lg_button_group_item(group, -1) == NULL &&
           vqeaf_lg_button_group_item(group, 99) == NULL &&
           vqeaf_lg_button_group_item(tabs, 0) == NULL, 1);
    lv_obj_delete(plain);
    lv_obj_delete(slider);
    lv_obj_delete(toggle);
    lv_obj_delete(check);
    lv_obj_delete(tabs);
    lv_obj_delete(group);
    lv_obj_delete(select);

    select = vqeaf_lg_select(parent, copied_items, 3, 0, NULL, NULL);
    strcpy(copied_text[0], "changed");
    strcpy(copied_text[1], "changed");
    strcpy(copied_symbol[1], "Z");
    vqeaf_lg_select_set(select, 1);
    expect("liquid select owns a deep copy of item text",
           strcmp(lv_label_get_text(lv_obj_get_child(select, 0)), "Sáng") == 0, 1);
    vqeaf_lg_overlay_open(select);
    {
        lv_obj_t *surface = lv_obj_get_child(lv_layer_top(), 2);
        lv_obj_t *item = lv_obj_get_child(surface, 1);

        expect("liquid overlay rows use copied text and symbols",
               strcmp(lv_label_get_text(lv_obj_get_child(item, 0)), "B") == 0 &&
               strcmp(lv_label_get_text(lv_obj_get_child(item, 1)), "Sáng") == 0, 1);
    }
    vqeaf_lg_overlay_close(select);
    lv_obj_delete(select);

    geometry = lv_obj_create(parent);
    lv_obj_remove_style_all(geometry);
    slider = vqeaf_lg_slider(geometry, 100);
    lv_obj_set_size(slider, 1, 48);
    lv_obj_send_event(slider, LV_EVENT_SIZE_CHANGED, NULL);
    expect("liquid slider clamps one-pixel geometry",
           lv_obj_get_width(lv_obj_get_child(slider, 0)) >= 0 &&
           lv_obj_get_width(lv_obj_get_child(slider, 0)) <= 1 &&
           lv_obj_get_x(lv_obj_get_child(slider, 1)) >= 0, 1);
    control = vqeaf_lg_slide_confirm(geometry, "X", NULL, NULL);
    lv_obj_set_width(control, 1);
    lv_obj_send_event(control, LV_EVENT_SIZE_CHANGED, NULL);
    expect("liquid slide clamps one-pixel geometry",
           lv_obj_get_width(lv_obj_get_child(control, 0)) >= 0 &&
           lv_obj_get_width(lv_obj_get_child(control, 0)) <= 1, 1);
    lv_obj_delete(geometry);

    old_width = lv_display_get_horizontal_resolution(display);
    old_height = lv_display_get_vertical_resolution(display);
    old_screen_width = lv_obj_get_width(screen);
    old_screen_height = lv_obj_get_height(screen);
    lv_display_set_resolution(display, 8, 8);
    lv_obj_set_size(screen, 8, 8);
    geometry = lv_obj_create(screen);
    lv_obj_remove_style_all(geometry);
    lv_obj_set_pos(geometry, 0, 0);
    lv_obj_set_size(geometry, 8, 8);
    control = vqeaf_lg_select(geometry, sweep_items, 3, 0, NULL, NULL);
    lv_obj_update_layout(geometry);
    vqeaf_lg_overlay_open(control);
    step(300);
    lv_obj_update_layout(lv_layer_top());
    {
        int bounded = lv_obj_get_child_count(lv_layer_top()) == 3;

        for(i = 0; i < 3; i++) {
            lv_obj_t *layer = lv_obj_get_child(lv_layer_top(), i);
            lv_area_t area;

            lv_obj_get_coords(layer, &area);
            if(area.x1 < 0 || area.y1 < 0 || area.x2 >= 8 || area.y2 >= 8) {
                printf("tiny layer %d: %d,%d-%d,%d\n", i,
                       area.x1, area.y1, area.x2, area.y2);
                bounded = 0;
            }
        }
        expect("liquid overlay geometry stays inside an eight-pixel display",
               bounded, 1);
    }
    vqeaf_lg_overlay_close(control);
    lv_obj_delete(geometry);
    lv_display_set_resolution(display, old_width, old_height);
    lv_obj_set_size(screen, old_screen_width, old_screen_height);
    lv_obj_update_layout(screen);

    lv_obj_delete(parent);
    vqeaf_lg_test_clear_faults();
    expect("liquid hardening leaves no live component state",
           vqeaf_lg_test_live_states(), 0);
    expect("liquid hardening leaves no overlay",
           vqeaf_lg_any_overlay_visible(), 0);
}

int main(void)
{
    framebuffer = calloc((size_t)width * height, sizeof(uint32_t));
    if(framebuffer == NULL) return 2;
    lv_init();
    lv_tick_set_cb(tick_cb);
    display = lv_display_create(width, height);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display, framebuffer, NULL,
                           (uint32_t)(sizeof(uint32_t) * width * height),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, flush_cb);
    lv_display_set_default(display);

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    vqeaf_theme_set(VQEAF_THEME_DARK);
    vqeaf_type_set_bold(0);

    /* --- type scale ------------------------------------------------------- */
    expect("display large pinned to bold 96",
           vqeaf_m3_type(VQEAF_M3_TYPE_DISPLAY_LARGE) == &vqeaf_font_bold_96, 1);
    expect("display small pinned to bold 56",
           vqeaf_m3_type(VQEAF_M3_TYPE_DISPLAY_SMALL) == &vqeaf_font_bold_56, 1);
    expect("label large is bold caption",
           vqeaf_m3_type(VQEAF_M3_TYPE_LABEL_LARGE) == &vqeaf_font_bold_20, 1);
    expect("label medium is regular caption",
           vqeaf_m3_type(VQEAF_M3_TYPE_LABEL_MEDIUM) == &vqeaf_font_20, 1);
    expect("label large != label medium",
           vqeaf_m3_type(VQEAF_M3_TYPE_LABEL_LARGE) !=
           vqeaf_m3_type(VQEAF_M3_TYPE_LABEL_MEDIUM), 1);
    vqeaf_type_set_bold(1);
    expect("headline large honours bold mode",
           vqeaf_m3_type(VQEAF_M3_TYPE_HEADLINE_LARGE) == &vqeaf_font_bold_34, 1);
    expect("body large honours bold mode",
           vqeaf_m3_type(VQEAF_M3_TYPE_BODY_LARGE) == &vqeaf_font_bold_24, 1);
    vqeaf_type_set_bold(0);
    expect("headline large regular after bold off",
           vqeaf_m3_type(VQEAF_M3_TYPE_HEADLINE_LARGE) == &vqeaf_font_34, 1);
    /* M3 sizes are non-increasing within each family (display/headline/title/
     * body/label); across families body-large may exceed title-small. */
    int mono = 1;
    static const int families[][2] = {
        { VQEAF_M3_TYPE_DISPLAY_LARGE, VQEAF_M3_TYPE_DISPLAY_SMALL },
        { VQEAF_M3_TYPE_HEADLINE_LARGE, VQEAF_M3_TYPE_HEADLINE_SMALL },
        { VQEAF_M3_TYPE_TITLE_LARGE, VQEAF_M3_TYPE_TITLE_SMALL },
        { VQEAF_M3_TYPE_BODY_LARGE, VQEAF_M3_TYPE_BODY_SMALL },
        { VQEAF_M3_TYPE_LABEL_LARGE, VQEAF_M3_TYPE_LABEL_SMALL }
    };
    for(unsigned f = 0; f < sizeof(families) / sizeof(families[0]); f++)
        for(int t = families[f][0]; t < families[f][1]; t++)
            if(vqeaf_m3_type_token(t)->size < vqeaf_m3_type_token(t + 1)->size)
                mono = 0;
    expect("type token sizes non-increasing within each family", mono, 1);
    expect("display large token 57dp",
           vqeaf_m3_type_token(VQEAF_M3_TYPE_DISPLAY_LARGE)->size, 57);

    /* --- top app bar ------------------------------------------------------ */
    lv_obj_t *bar = vqeaf_m3_top_app_bar(screen, "Cài đặt",
                                         VQEAF_SYM_ARROW_BACK,
                                         VQEAF_SYM_MORE_VERT, NULL, NULL);
    lv_obj_update_layout(screen);
    expect("top app bar height 64", lv_obj_get_height(bar), 64);
    expect("top app bar surface-container fill",
           ceq(lv_obj_get_style_bg_color(bar, 0), VQEAF_C_SURFACE_CONTAINER), 1);
    expect("top app bar has nav + title + action",
           lv_obj_get_child_count(bar), 3);
    expect("nav icon button clickable",
           lv_obj_has_flag(lv_obj_get_child(bar, 0), LV_OBJ_FLAG_CLICKABLE), 1);

    /* --- navigation bar + active indicator -------------------------------- */
    lv_obj_t *nav = vqeaf_m3_navigation_bar(screen);
    lv_obj_t *sel = vqeaf_m3_nav_item(nav, VQEAF_SYM_HOME, "Home", 1);
    lv_obj_t *unsel = vqeaf_m3_nav_item(nav, VQEAF_SYM_SETTINGS, "Cài đặt", 0);
    lv_obj_update_layout(screen);
    expect("navigation bar height 80", lv_obj_get_height(nav), 80);
    lv_obj_t *pill_sel = lv_obj_get_child(sel, 1);
    lv_obj_t *pill_un = lv_obj_get_child(unsel, 1);
    expect("selected pill opaque",
           lv_obj_get_style_bg_opa(pill_sel, 0), LV_OPA_COVER);
    expect("selected pill secondary-container",
           ceq(lv_obj_get_style_bg_color(pill_sel, 0),
               VQEAF_C_SECONDARY_CONTAINER), 1);
    expect("unselected pill transparent",
           lv_obj_get_style_bg_opa(pill_un, 0), LV_OPA_TRANSP);
    expect("selected pill full radius",
           lv_obj_get_style_radius(pill_sel, 0), LV_RADIUS_CIRCLE);

    /* --- list item -------------------------------------------------------- */
    lv_obj_t *one = vqeaf_m3_list_item(screen, VQEAF_SYM_WIFI, "Wi-Fi", NULL, NULL);
    lv_obj_t *trail = NULL;
    lv_obj_t *two = vqeaf_m3_list_item(screen, VQEAF_SYM_BLUETOOTH, "Bluetooth",
                                       "Đang tắt", &trail);
    lv_obj_update_layout(screen);
    expect("one-line list item 56", lv_obj_get_height(one), 56);
    expect("two-line list item 72", lv_obj_get_height(two), 72);
    expect("trailing slot returned", trail != NULL, 1);

    /* --- dialog ----------------------------------------------------------- */
    lv_obj_t *confirm = NULL, *dismiss = NULL;
    lv_obj_t *scrim = vqeaf_m3_dialog(screen, "Đặt lại chủ đề?",
                                      "Màu động sẽ trở về mặc định.",
                                      "Đặt lại", "Hủy", &confirm, &dismiss);
    lv_obj_update_layout(screen);
    expect("dialog scrim 40%", lv_obj_get_style_bg_opa(scrim, 0), 102);
    lv_obj_t *card = lv_obj_get_child(scrim, 0);
    expect("dialog card radius XL",
           lv_obj_get_style_radius(card, 0), VQEAF_M3_SHAPE_XL);
    expect("dialog card surface-container-high",
           ceq(lv_obj_get_style_bg_color(card, 0),
               VQEAF_C_SURFACE_CONTAINER_HIGH), 1);
    expect("dialog confirm button returned", confirm != NULL, 1);
    expect("dialog dismiss button returned", dismiss != NULL, 1);

    /* --- snackbar --------------------------------------------------------- */
    lv_obj_t *action = NULL;
    lv_obj_t *snack = vqeaf_m3_snackbar(screen, "Đã lưu phiên", "Hoàn tác",
                                        &action);
    lv_obj_update_layout(screen);
    expect("snackbar inverse surface",
           ceq(lv_obj_get_style_bg_color(snack, 0), VQEAF_C_INVERSE_SURFACE), 1);
    expect("snackbar radius XS",
           lv_obj_get_style_radius(snack, 0), VQEAF_M3_SHAPE_XS);
    expect("snackbar action returned", action != NULL, 1);
    lv_obj_t *albl = lv_obj_get_child(action, 1);
    expect("snackbar action accent colour",
           ceq(lv_obj_get_style_text_color(albl, 0), VQEAF_C_PRIMARY), 1);

    /* --- slider ----------------------------------------------------------- */
    lv_obj_t *sl = vqeaf_m3_slider(screen, 50);
    lv_obj_update_layout(screen);
    int sw = lv_obj_get_width(sl);
    lv_obj_t *act = lv_obj_get_child(sl, 1);
    lv_obj_t *th = lv_obj_get_child(sl, 2);
    expect_near("slider 50 active half track", lv_obj_get_width(act), sw / 2, 2);
    vqeaf_m3_slider_set(sl, 0);
    lv_obj_update_layout(screen);
    expect("slider 0 active empty", lv_obj_get_width(act), 0);
    expect("slider 0 thumb at start", lv_obj_get_x(th), 0);
    vqeaf_m3_slider_set(sl, 100);
    lv_obj_update_layout(screen);
    expect("slider 100 active full", lv_obj_get_width(act), sw);
    expect("slider 100 thumb at end", lv_obj_get_x(th), sw - 24);
    vqeaf_m3_slider_set(sl, 150);  /* clamps */
    lv_obj_update_layout(screen);
    expect("slider clamps >100", lv_obj_get_width(act), sw);

    /* --- linear progress -------------------------------------------------- */
    lv_obj_t *pg = vqeaf_m3_progress_linear(screen, 25);
    lv_obj_update_layout(screen);
    int pw = lv_obj_get_width(pg);
    lv_obj_t *ind = lv_obj_get_child(pg, 0);
    expect_near("progress 25 quarter track", lv_obj_get_width(ind), pw / 4, 2);
    vqeaf_m3_progress_set(pg, 0);
    lv_obj_update_layout(screen);
    expect("progress 0 empty", lv_obj_get_width(ind), 0);
    vqeaf_m3_progress_set(pg, 100);
    lv_obj_update_layout(screen);
    expect("progress 100 full", lv_obj_get_width(ind), pw);

    /* --- reusable Liquid Glass components -------------------------------- */
    {
        static const char *const labels[] = { "Một", "Hai", "Ba" };
        static const vqeaf_component_item_t items[] = {
            { "Tự động", NULL, 0 },
            { "Sáng", NULL, 0 },
            { "Không khả dụng", NULL, 1 },
        };
        lv_obj_t *root = lv_obj_create(screen);
        lv_obj_t *button, *action, *tabs, *badge, *slide, *card, *slider;
        lv_obj_t *toggle, *check, *select, *group, *dropdown, *popover, *menu;
        lv_obj_t *lens;

        lv_obj_remove_style_all(root);
        lv_obj_set_size(root, 600, 1000);
        lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(root, 8, 0);
        lv_obj_update_layout(root);

        button = vqeaf_lg_button(root, "Chạm", VQEAF_M3_FILLED);
        lv_obj_update_layout(root);
        expect("liquid button 56 high", lv_obj_get_height(button), 56);
        expect("liquid button clickable",
               lv_obj_has_flag(button, LV_OBJ_FLAG_CLICKABLE), 1);
        action = vqeaf_lg_action_button(root, VQEAF_SYM_ARROW_FORWARD,
                                        component_click_cb, NULL);
        lv_obj_send_event(action, LV_EVENT_CLICKED, NULL);
        expect("liquid action invokes callback", component_clicks, 1);

        tabs = vqeaf_lg_tabs(root, labels, 3, 1);
        vqeaf_lg_tabs_select(tabs, 99);
        expect("liquid tabs clamp selection", vqeaf_lg_tabs_selected(tabs), 2);
        lens = lv_obj_get_child(vqeaf_lg_tab_item(tabs, 2), 0);
        expect("liquid tabs lens starts traveling",
               lv_obj_get_style_translate_x(lens, 0) != 0, 1);
        step(300);
        expect("liquid tabs selected lens visible",
               lv_obj_get_style_bg_opa(lens, 0) > LV_OPA_TRANSP, 1);
        lens = lv_obj_get_child(vqeaf_lg_tab_item(tabs, 0), 0);
        expect("liquid tabs unselected lens clear",
               lv_obj_get_style_bg_opa(lens, 0), LV_OPA_TRANSP);
        badge = vqeaf_lg_badge(root, "Mới");
        expect("liquid badge has one text layer", lv_obj_get_child_count(badge), 1);

        slide = vqeaf_lg_slide_confirm(root, "Trượt", NULL, NULL);
        expect("liquid slide starts unconfirmed", vqeaf_lg_slide_confirmed(slide), 0);
        vqeaf_lg_slide_reset(slide);
        expect("liquid slide reset remains unconfirmed",
               vqeaf_lg_slide_confirmed(slide), 0);
        card = vqeaf_lg_card(root, lv_pct(100), 100);
        expect("liquid card XL radius", lv_obj_get_style_radius(card, 0),
               VQEAF_M3_SHAPE_XL);

        slider = vqeaf_lg_slider(root, 50);
        vqeaf_lg_slider_set(slider, -10);
        expect("liquid slider clamps negative", vqeaf_lg_slider_get(slider), 0);
        vqeaf_lg_slider_set(slider, 110);
        expect("liquid slider clamps above 100", vqeaf_lg_slider_get(slider), 100);
        lv_obj_update_layout(root);
        expect("liquid slider full active track",
               lv_obj_get_width(lv_obj_get_child(slider, 0)), lv_obj_get_width(slider));

        toggle = vqeaf_lg_switch(root, 0);
        expect("liquid switch starts off", vqeaf_lg_switch_get(toggle), 0);
        vqeaf_lg_switch_set(toggle, 1);
        expect("liquid switch stores real state", vqeaf_lg_switch_get(toggle), 1);
        lv_obj_send_event(toggle, LV_EVENT_CLICKED, NULL);
        expect("liquid switch click toggles state", vqeaf_lg_switch_get(toggle), 0);
        check = vqeaf_lg_checkbox(root, "Đồng ý", 1);
        expect("liquid checkbox starts checked", vqeaf_lg_checkbox_get(check), 1);
        lv_obj_send_event(check, LV_EVENT_CLICKED, NULL);
        expect("liquid checkbox click toggles state", vqeaf_lg_checkbox_get(check), 0);

        select = vqeaf_lg_select(root, items, 3, 0, NULL, NULL);
        vqeaf_lg_select_set(select, 1);
        expect("liquid select stores selection", vqeaf_lg_selected_index(select), 1);
        vqeaf_lg_select_set(select, 99);
        expect("liquid select clamps selection", vqeaf_lg_selected_index(select), 2);
        vqeaf_lg_overlay_open(select);
        expect("liquid select opens anchored surface",
               vqeaf_lg_overlay_visible(select), 1);
        expect("liquid overlay uses three top-layer siblings",
               lv_obj_get_child_count(lv_layer_top()), 3);
        vqeaf_lg_overlay_close(select);
        expect("liquid select closes anchored surface",
               vqeaf_lg_overlay_visible(select), 0);

        group = vqeaf_lg_button_group(root, labels, 3, 0);
        vqeaf_lg_button_group_select(group, 2);
        expect("liquid button group stores selection",
               vqeaf_lg_button_group_selected(group), 2);
        lv_obj_send_event(vqeaf_lg_button_group_item(group, 1), LV_EVENT_CLICKED, NULL);
        expect("liquid button group item click selects",
               vqeaf_lg_button_group_selected(group), 1);
        dropdown = vqeaf_lg_dropdown(root, "Hành động", items, 3, NULL, NULL);
        vqeaf_lg_overlay_open(dropdown);
        expect("liquid dropdown opens actions", vqeaf_lg_overlay_visible(dropdown), 1);
        vqeaf_lg_overlay_close(dropdown);
        popover = vqeaf_lg_popover(root, "Tiêu đề", "Nội dung", "Đóng");
        vqeaf_lg_overlay_open(popover);
        expect("liquid popover opens one action",
               vqeaf_lg_overlay_visible(popover), 1);
        vqeaf_lg_overlay_close(popover);
        menu = vqeaf_lg_menu(root, "Menu", items, 3, -1, NULL, NULL);
        vqeaf_lg_overlay_open(menu);
        expect("liquid menu opens on requested side",
               vqeaf_lg_overlay_visible(menu), 1);
        lv_obj_delete(root);
        expect("deleting anchor removes its liquid overlay",
               vqeaf_lg_any_overlay_visible(), 0);
    }

    /* Render one composed frame so the gate also proves the components draw. */
    lv_obj_update_layout(screen);
    lv_refr_now(NULL);
    unsigned long ink = 0;
    for(int i = 0; i < width * height; i++) {
        uint32_t p = framebuffer[i];
        ink += ((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF);
    }
    expect("composed M3 frame has ink", ink > 0, 1);

    test_liquid_hardening(screen);

    if(failures) {
        printf("vqeaf_m3: %d FAILURES\n", failures);
        return 1;
    }
    printf("vqeaf_m3: ALL OK\n");
    return 0;
}