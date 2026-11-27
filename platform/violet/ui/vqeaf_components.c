/* SPDX-License-Identifier: MIT */
#include "vqeaf_components.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void lg_delete_cb(lv_event_t *event);
static void lg_free_items(vqeaf_component_item_t *items, int count);
static void lg_melt_cb(lv_event_t *event);
static void lg_overlay_click(lv_event_t *event);
void vqeaf_lg_overlay_open(lv_obj_t *control);
void vqeaf_lg_overlay_close(lv_obj_t *control);
#define LG_OPA(percent) ((lv_opa_t)((int)LV_OPA_COVER * (percent) / 100))

#ifdef VQEAF_LG_TESTING
static unsigned int lg_test_alloc_fail;
static unsigned int lg_test_object_fail;
static unsigned int lg_test_event_fail;
static int lg_test_async_fail;

void vqeaf_lg_test_allocation_fault(unsigned int after)
{
    lg_test_alloc_fail = after;
}

void vqeaf_lg_test_object_fault(unsigned int after)
{
    lg_test_object_fail = after;
}

void vqeaf_lg_test_event_fault(unsigned int after)
{
    lg_test_event_fail = after;
}

void vqeaf_lg_test_async_fault(int enabled)
{
    lg_test_async_fail = enabled;
}

void vqeaf_lg_test_clear_faults(void)
{
    lg_test_alloc_fail = 0;
    lg_test_object_fail = 0;
    lg_test_event_fail = 0;
    lg_test_async_fail = 0;
}
#endif

static void *lg_alloc(size_t size)
{
#ifdef VQEAF_LG_TESTING
    if(lg_test_alloc_fail > 0 && --lg_test_alloc_fail == 0) return NULL;
#endif
    return lv_malloc(size);
}

static void *lg_alloc_zeroed(size_t size)
{
#ifdef VQEAF_LG_TESTING
    if(lg_test_alloc_fail > 0 && --lg_test_alloc_fail == 0) return NULL;
#endif
    return lv_malloc_zeroed(size);
}

static char *lg_strdup(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = lg_alloc(size);

    if(copy != NULL) memcpy(copy, text, size);
    return copy;
}

static lv_obj_t *lg_obj_create(lv_obj_t *parent)
{
    if(parent == NULL) return NULL;
#ifdef VQEAF_LG_TESTING
    if(lg_test_object_fail > 0 && --lg_test_object_fail == 0) return NULL;
#endif
    return lv_obj_create(parent);
}

static lv_obj_t *lg_label_create(lv_obj_t *parent)
{
    if(parent == NULL) return NULL;
#ifdef VQEAF_LG_TESTING
    if(lg_test_object_fail > 0 && --lg_test_object_fail == 0) return NULL;
#endif
    return lv_label_create(parent);
}

static bool lg_add_event_cb(lv_obj_t *obj, lv_event_cb_t cb,
                            lv_event_code_t code, void *user_data)
{
    if(obj == NULL) return false;
#ifdef VQEAF_LG_TESTING
    if(lg_test_event_fail > 0 && --lg_test_event_fail == 0) return false;
#endif
    return lv_obj_add_event_cb(obj, cb, code, user_data) != NULL;
}

typedef enum {
    LG_BUTTON,
    LG_TABS,
    LG_SLIDE,
    LG_SLIDER,
    LG_SWITCH,
    LG_CHECKBOX,
    LG_SELECT,
    LG_GROUP,
    LG_MENU
} lg_kind_t;

typedef struct lg_state {
    lg_kind_t kind;
    lv_obj_t *owner;
    lv_obj_t *visual;
    lv_obj_t *active;
    lv_obj_t *thumb;
    lv_obj_t *mark;
    int value;
    int dragging;
    int melted;
    int lens_travel;
    vqeaf_component_item_t *items;
    int count;
    struct lg_state *next;
} lg_state_t;

static lg_state_t *g_states;

#ifdef VQEAF_LG_TESTING
int vqeaf_lg_test_live_states(void)
{
    int count = 0;
    lg_state_t *state;

    for(state = g_states; state != NULL; state = state->next) count++;
    return count;
}
#endif

static lg_state_t *lg_state(lv_obj_t *obj)
{
    lg_state_t *state;

    if(obj == NULL) return NULL;
    for(state = g_states; state != NULL; state = state->next)
        if(state->owner == obj) return state;
    return NULL;
}

static void lg_state_unlink(lg_state_t *state)
{
    lg_state_t **link = &g_states;

    while(*link != NULL) {
        if(*link == state) {
            *link = state->next;
            return;
        }
        link = &(*link)->next;
    }
}

static lg_state_t *lg_new_state(lv_obj_t *obj, lg_kind_t kind)
{
    lg_state_t *state = lg_alloc_zeroed(sizeof(*state));

    if(state == NULL) return NULL;
    state->kind = kind;
    state->value = -1;
    state->owner = obj;
    state->next = g_states;
    g_states = state;
    if(!lg_add_event_cb(obj, lg_delete_cb, LV_EVENT_DELETE, NULL)) {
        lg_state_unlink(state);
        lv_free(state);
        return NULL;
    }
    return state;
}

static void lg_delete_cb(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target_obj(event);
    lg_state_t *state = lg_state(obj);

    if(state == NULL) return;
    if(state->kind == LG_SELECT || state->kind == LG_MENU)
        vqeaf_lg_overlay_close(obj);
    lg_state_unlink(state);
    lg_free_items(state->items, state->count);
    lv_free(state);
}

static lv_obj_t *lg_label(lv_obj_t *parent, const char *text,
                          const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lg_label_create(parent);

    if(label == NULL) return NULL;
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_add_flag(label, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return label;
}

static lv_obj_t *lg_glyph(lv_obj_t *parent, const char *symbol,
                          const lv_font_t *font, lv_color_t color)
{
    return lg_label(parent, symbol, font, color);
}

static void lg_clickable(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
}

static void lg_glass(lv_obj_t *obj, int32_t radius)
{
    vqeaf_glass_surface(obj, radius, LG_OPA(55));
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_clip_corner(obj, true, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_90, 0);
}

static void lg_scale_cb(void *var, int32_t value)
{
    lv_obj_set_style_transform_scale_x((lv_obj_t *)var, value, 0);
    lv_obj_set_style_transform_scale_y((lv_obj_t *)var, value, 0);
}

static void lg_animate_scale(lv_obj_t *obj, int from, int to, int duration)
{
    lv_anim_t anim;

    lv_anim_delete(obj, NULL);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, lg_scale_cb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_duration(&anim, duration);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

static void lg_settle_scale(lv_obj_t *obj, int duration)
{
    int current = lv_obj_get_style_transform_scale_x(obj, 0);
    lg_animate_scale(obj, current, 256, duration);
}

static void lg_press_cb(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target_obj(event);
    lg_state_t *state = lg_state(obj);
    lv_event_code_t code = lv_event_get_code(event);

    if(state == NULL) return;
    if(code == LV_EVENT_PRESSED) lg_animate_scale(obj, 256, 272, 100);
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
        lg_settle_scale(obj, 170);
}

static void lg_translate_cb(void *var, int32_t value)
{
    lv_obj_set_style_translate_x((lv_obj_t *)var, value, 0);
}

static void lg_letter_space_cb(void *var, int32_t value)
{
    lv_obj_set_style_text_letter_space((lv_obj_t *)var, value, 0);
}

lv_obj_t *vqeaf_lg_button(lv_obj_t *parent, const char *text,
                          vqeaf_m3_button_style_t style)
{
    lv_obj_t *button = lg_obj_create(parent);
    lg_state_t *state;

    if(button == NULL) return NULL;
    lg_clickable(button);
    lv_obj_set_size(button, 168, 56);
    lg_glass(button, LV_RADIUS_CIRCLE);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if(style == VQEAF_M3_OUTLINED)
        lv_obj_set_style_border_color(button, VQEAF_C_OUTLINE, 0);
    state = lg_new_state(button, LG_BUTTON);
    if(state == NULL) goto fail;
    state->visual = lg_label(button, text,
        vqeaf_m3_type(VQEAF_M3_TYPE_LABEL_LARGE),
        style == VQEAF_M3_FILLED ? VQEAF_C_ON_PRIMARY : VQEAF_C_PRIMARY);
    if(state->visual == NULL) goto fail;
    lv_obj_set_style_text_letter_space(state->visual, 1, 0);
    if(style != VQEAF_M3_TEXT)
        lg_animate_scale(state->visual, 288, 256, 260);
    if(!lg_add_event_cb(button, lg_press_cb, LV_EVENT_PRESSED, NULL) ||
       !lg_add_event_cb(button, lg_press_cb, LV_EVENT_RELEASED, NULL) ||
       !lg_add_event_cb(button, lg_press_cb, LV_EVENT_PRESS_LOST, NULL))
        goto fail;
    return button;

fail:
    lv_obj_delete(button);
    return NULL;
}

lv_obj_t *vqeaf_lg_action_button(lv_obj_t *parent, const char *symbol,
                                 lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *button = lg_obj_create(parent);
    lg_state_t *state;

    if(button == NULL) return NULL;
    lg_clickable(button);
    lv_obj_set_size(button, 64, 64);
    lg_glass(button, LV_RADIUS_CIRCLE);
    vqeaf_m3_elevate(button, 2);
    state = lg_new_state(button, LG_BUTTON);
    if(state == NULL) goto fail;
    state->visual = lg_glyph(button, symbol, VQEAF_ICON, VQEAF_C_PRIMARY);
    if(state->visual == NULL) goto fail;
    lv_obj_center(state->visual);
    if(!lg_add_event_cb(button, lg_press_cb, LV_EVENT_PRESSED, NULL) ||
       !lg_add_event_cb(button, lg_press_cb, LV_EVENT_RELEASED, NULL) ||
       !lg_add_event_cb(button, lg_press_cb, LV_EVENT_PRESS_LOST, NULL) ||
       !lg_add_event_cb(button, lg_melt_cb, LV_EVENT_CLICKED, NULL))
        goto fail;
    if(cb != NULL &&
       !lg_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data))
        goto fail;
    return button;

fail:
    lv_obj_delete(button);
    return NULL;
}

static void lg_opa_cb(void *var, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)value, 0);
}

static void lg_animate_opa(lv_obj_t *obj, int to, int duration)
{
    lv_anim_t anim;
    int from = lv_obj_get_style_opa(obj, 0);

    lv_anim_delete(obj, NULL);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, lg_opa_cb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_duration(&anim, duration);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

static void lg_translate_y_cb(void *var, int32_t value)
{
    lv_obj_set_style_translate_y((lv_obj_t *)var, value, 0);
}

static void lg_animate_surface(lv_obj_t *surface)
{
    lv_anim_t anim;

    lv_obj_set_style_opa(surface, LV_OPA_TRANSP, 0);
    lv_obj_set_style_translate_y(surface, -12, 0);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, surface);
    lv_anim_set_exec_cb(&anim, lg_opa_cb);
    lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&anim, 220);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, surface);
    lv_anim_set_exec_cb(&anim, lg_translate_y_cb);
    lv_anim_set_values(&anim, -12, 0);
    lv_anim_set_duration(&anim, 220);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

static void lg_melt_done(lv_anim_t *anim)
{
    lv_obj_t *glyph = (lv_obj_t *)anim->var;
    lv_obj_t *button = lv_obj_get_parent(glyph);
    lg_state_t *state = lg_state(button);

    if(state == NULL || button == NULL) return;
    lv_obj_set_style_translate_x(glyph, 0, 0);
    lv_obj_set_style_translate_y(glyph, 0, 0);
    lv_obj_set_style_transform_scale_x(glyph, 32, 0);
    lv_obj_set_style_transform_scale_y(glyph, 32, 0);
    lv_obj_set_style_opa(glyph, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICKABLE);
}

static void lg_melt_cb(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_target_obj(event);
    lg_state_t *state = lg_state(button);
    lv_anim_t anim;

    if(state == NULL || state->visual == NULL || state->melted) return;
    state->melted = 1;
    lv_anim_delete(state->visual, NULL);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, state->visual);
    lv_anim_set_exec_cb(&anim, lg_translate_cb);
    lv_anim_set_values(&anim, 0, 22);
    lv_anim_set_duration(&anim, 260);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    lv_anim_start(&anim);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, state->visual);
    lv_anim_set_exec_cb(&anim, lg_scale_cb);
    lv_anim_set_values(&anim, 256, 32);
    lv_anim_set_duration(&anim, 260);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&anim, lg_melt_done);
    lv_anim_start(&anim);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, state->visual);
    lv_anim_set_exec_cb(&anim, lg_opa_cb);
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&anim, 220);
    lv_anim_start(&anim);
}

static int lg_clamp_index(int index, int count);

static void lg_tabs_lens_cb(void *var, int32_t value)
{
    lv_obj_t *lens = (lv_obj_t *)var;
    lv_obj_t *item = lv_obj_get_parent(lens);
    lv_obj_t *tabs = item != NULL ? lv_obj_get_parent(item) : NULL;
    lg_state_t *state = lg_state(tabs);
    int travel = state != NULL ? state->lens_travel : 0;
    int remaining = value < 0 ? -value : value;
    int progress = travel > 0 ? (travel - remaining) * 100 / travel : 100;

    if(progress < 0) progress = 0;
    if(progress > 100) progress = 100;
    lv_obj_set_style_translate_x(lens, value, 0);
    lv_obj_set_style_bg_opa(lens, LG_OPA(55 * progress / 100), 0);
}

static void lg_apply_tabs(lv_obj_t *tabs, lg_state_t *state, int requested)
{
    int next;

    lv_obj_update_layout(tabs);
    next = lg_clamp_index(requested, state->count);
    int previous = state->value;
    int child_count = (int)lv_obj_get_child_count(tabs);
    int from = 0;
    int i;

    if(previous >= 0 && previous < state->count && previous < child_count &&
       previous != next) {
        lv_obj_t *old_item = lv_obj_get_child(tabs, previous);
        from = (previous - next) * (int)lv_obj_get_width(old_item);
    }
    state->value = next;
    state->lens_travel = from < 0 ? -from : from;
    for(i = 0; i < state->count && i < child_count; i++) {
        lv_obj_t *item = lv_obj_get_child(tabs, i);
        lv_obj_t *lens = lv_obj_get_child(item, 0);
        lv_obj_t *label = lv_obj_get_child(item, 1);
        int selected = i == next;

        if(lens == NULL) continue;
        lv_anim_delete(lens, NULL);
        lv_obj_set_style_translate_x(lens, 0, 0);
        lv_obj_set_style_bg_opa(lens, LV_OPA_TRANSP, 0);
        lv_obj_set_style_transform_scale_x(lens, selected ? 280 : 256, 0);
        if(label != NULL)
            lv_obj_set_style_text_color(label,
                                        selected ? VQEAF_C_PRIMARY : VQEAF_C_TEXT_DIM, 0);
        if(selected) {
            if(from != 0) {
                lv_anim_t anim;
                lv_obj_set_style_translate_x(lens, from, 0);
                lv_anim_init(&anim);
                lv_anim_set_var(&anim, lens);
                lv_anim_set_exec_cb(&anim, lg_tabs_lens_cb);
                lv_anim_set_values(&anim, from, 0);
                lv_anim_set_duration(&anim, 260);
                lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
                lv_anim_start(&anim);
            }
            else lv_obj_set_style_bg_opa(lens, LG_OPA(55), 0);
        }
    }
}

static void lg_tabs_click(lv_event_t *event)
{
    lv_obj_t *item = lv_event_get_target_obj(event);
    lv_obj_t *tabs = lv_obj_get_parent(item);
    int index = (int)(intptr_t)lv_event_get_user_data(event);

    if(tabs == NULL) return;
    vqeaf_lg_tabs_select(tabs, index);
    lv_obj_send_event(tabs, LV_EVENT_VALUE_CHANGED, NULL);
}

lv_obj_t *vqeaf_lg_tabs(lv_obj_t *parent, const char *const *labels,
                        int count, int selected)
{
    lv_obj_t *tabs;
    lg_state_t *state;
    int i;

    if(labels == NULL || count <= 0 || count > VQEAF_LG_MAX_ITEMS) return NULL;
    tabs = lg_obj_create(parent);
    if(tabs == NULL) return NULL;
    lg_clickable(tabs);
    lv_obj_remove_flag(tabs, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(tabs, lv_pct(100), 56);
    lg_glass(tabs, LV_RADIUS_CIRCLE);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tabs, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tabs, 4, 0);
    state = lg_new_state(tabs, LG_TABS);
    if(state == NULL) goto fail;
    state->count = count;
    if(selected < 0 || selected >= count) selected = 0;
    for(i = 0; i < count; i++) {
        lv_obj_t *item = lg_obj_create(tabs);
        lv_obj_t *lens;
        lv_obj_t *label;

        if(item == NULL) goto fail;
        lg_clickable(item);
        lv_obj_set_height(item, lv_pct(100));
        lv_obj_set_flex_grow(item, 1);
        lv_obj_set_style_radius(item, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(item, false, 0);
        lens = lg_obj_create(item);
        if(lens == NULL) goto fail;
        lv_obj_remove_style_all(lens);
        lv_obj_set_size(lens, lv_pct(100), lv_pct(100));
        lv_obj_set_style_radius(lens, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(lens, VQEAF_C_SECONDARY_CONTAINER, 0);
        lv_obj_remove_flag(lens, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        label = lg_label(item, labels[i], VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
        if(label == NULL) goto fail;
        lv_obj_move_foreground(label);
        if(!lg_add_event_cb(item, lg_tabs_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i))
            goto fail;
    }
    vqeaf_lg_tabs_select(tabs, selected);
    return tabs;

fail:
    lv_obj_delete(tabs);
    return NULL;
}

static int lg_clamp_index(int index, int count)
{
    if(count <= 0) return -1;
    if(index < 0) return 0;
    if(index >= count) return count - 1;
    return index;
}

void vqeaf_lg_tabs_select(lv_obj_t *tabs, int index)
{
    lg_state_t *state = lg_state(tabs);

    if(state == NULL || state->kind != LG_TABS || state->count <= 0) return;
    lg_apply_tabs(tabs, state, index);
}

int vqeaf_lg_tabs_selected(lv_obj_t *tabs)
{
    lg_state_t *state = lg_state(tabs);
    return state != NULL && state->kind == LG_TABS ? state->value : -1;
}

lv_obj_t *vqeaf_lg_tab_item(lv_obj_t *tabs, int index)
{
    lg_state_t *state = lg_state(tabs);
    return state != NULL && state->kind == LG_TABS && index >= 0 &&
           index < state->count &&
           index < (int)lv_obj_get_child_count(tabs) ?
           lv_obj_get_child(tabs, index) : NULL;
}

lv_obj_t *vqeaf_lg_badge(lv_obj_t *parent, const char *text)
{
    lv_obj_t *badge = lg_obj_create(parent);
    lv_obj_t *label;
    lv_anim_t anim;

    if(badge == NULL) return NULL;
    lg_clickable(badge);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_height(badge, 34);
    lv_obj_set_style_pad_hor(badge, 14, 0);
    lg_glass(badge, LV_RADIUS_CIRCLE);
    label = lg_label(badge, text, VQEAF_FONT_CAPTION, VQEAF_C_PRIMARY);
    if(label == NULL) {
        lv_obj_delete(badge);
        return NULL;
    }
    lv_obj_center(label);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, label);
    lv_anim_set_exec_cb(&anim, lg_translate_cb);
    lv_anim_set_values(&anim, -8, 0);
    lv_anim_set_duration(&anim, 380);
    lv_anim_set_path_cb(&anim, lv_anim_path_overshoot);
    lv_anim_start(&anim);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, label);
    lv_anim_set_exec_cb(&anim, lg_letter_space_cb);
    lv_anim_set_values(&anim, 3, 0);
    lv_anim_set_duration(&anim, 380);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
    return badge;
}

#define LG_SLIDE_KNOB 56
#define LG_SLIDE_PAD 6

static int32_t lg_slide_travel(lv_obj_t *control)
{
    int32_t max = lv_obj_get_width(control) - LG_SLIDE_KNOB - 2 * LG_SLIDE_PAD;

    return max > 0 ? max : 0;
}

static void lg_slide_apply(lv_obj_t *control, lg_state_t *state, int done)
{
    int32_t max = lg_slide_travel(control);
    int32_t width = lv_obj_get_width(control);
    int32_t active_w = done ? width : LG_SLIDE_KNOB + 12;

    if(state->thumb == NULL || state->active == NULL || state->mark == NULL) return;
    if(width < 0) width = 0;
    if(active_w < 0) active_w = 0;
    if(active_w > width) active_w = width;
    state->value = done;
    lv_obj_set_pos(state->thumb, done ? max + LG_SLIDE_PAD : LG_SLIDE_PAD,
                   LG_SLIDE_PAD);
    lv_obj_set_width(state->active, active_w);
    lv_label_set_text(state->mark, done ? VQEAF_SYM_CHECK : VQEAF_SYM_ARROW_FORWARD);
}

static void lg_slide_event(lv_event_t *event)
{
    lv_obj_t *control = lv_event_get_target_obj(event);
    lg_state_t *state = lg_state(control);
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev;
    lv_point_t point;
    lv_area_t area;
    int32_t x, max;

    if(state == NULL) return;
    if(code == LV_EVENT_PRESSED) {
        if(state->value) {
            lg_slide_apply(control, state, 0);
            lv_obj_send_event(control, LV_EVENT_VALUE_CHANGED, NULL);
        } else {
            state->dragging = 1;
            lg_animate_scale(state->thumb, 256, 288, 120);
        }
    } else if(code == LV_EVENT_PRESSING && state->dragging) {
        int32_t width;
        int32_t active_w;

        indev = lv_indev_active();
        if(indev == NULL) return;
        lv_indev_get_point(indev, &point);
        lv_obj_get_coords(control, &area);
        width = area.x2 - area.x1 + 1;
        if(width < 0) width = 0;
        max = lg_slide_travel(control);
        x = point.x - area.x1 - LG_SLIDE_KNOB / 2;
        if(x < LG_SLIDE_PAD) x = LG_SLIDE_PAD;
        if(x > max + LG_SLIDE_PAD) x = max + LG_SLIDE_PAD;
        active_w = x + LG_SLIDE_KNOB;
        if(active_w > width) active_w = width;
        if(active_w < 0) active_w = 0;
        lv_obj_set_pos(state->thumb, x, LG_SLIDE_PAD);
        lv_obj_set_width(state->active, active_w);
    } else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if(!state->dragging) return;
        state->dragging = 0;
        lg_settle_scale(state->thumb, 150);
        max = lg_slide_travel(control);
        if(lv_obj_get_x(state->thumb) >= max + LG_SLIDE_PAD - 2) {
            lg_slide_apply(control, state, 1);
            lv_obj_send_event(control, LV_EVENT_VALUE_CHANGED, NULL);
        } else {
            lg_slide_apply(control, state, 0);
        }
    }
}

static void lg_slide_size(lv_event_t *event)
{
    lg_state_t *state = lg_state(lv_event_get_target_obj(event));
    if(state != NULL)
        lg_slide_apply(lv_event_get_target_obj(event), state, state->value);
}

lv_obj_t *vqeaf_lg_slide_confirm(lv_obj_t *parent, const char *label,
                                 lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *control = lg_obj_create(parent);
    lg_state_t *state;

    if(control == NULL) return NULL;
    lg_clickable(control);
    lv_obj_set_size(control, lv_pct(100), LG_SLIDE_KNOB + 12);
    lg_glass(control, LV_RADIUS_CIRCLE);
    state = lg_new_state(control, LG_SLIDE);
    if(state == NULL) goto fail;
    state->active = lg_obj_create(control);
    if(state->active == NULL) goto fail;
    lv_obj_remove_style_all(state->active);
    lv_obj_set_style_radius(state->active, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(state->active, VQEAF_C_PRIMARY_CONTAINER, 0);
    lv_obj_set_style_bg_opa(state->active, LG_OPA(55), 0);
    lv_obj_remove_flag(state->active, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    state->visual = lg_label(control, label != NULL ? label : "Trượt để xác nhận",
                             VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
    if(state->visual == NULL) goto fail;
    lv_obj_set_width(state->visual, lv_pct(100));
    lv_obj_set_style_text_align(state->visual, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(state->visual);
    state->thumb = lg_obj_create(control);
    if(state->thumb == NULL) goto fail;
    lg_glass(state->thumb, LV_RADIUS_CIRCLE);
    lv_obj_set_size(state->thumb, LG_SLIDE_KNOB, LG_SLIDE_KNOB);
    lv_obj_remove_flag(state->thumb, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    state->mark = lg_glyph(state->thumb, VQEAF_SYM_ARROW_FORWARD,
                           VQEAF_ICON, VQEAF_C_PRIMARY);
    if(state->mark == NULL) goto fail;
    lv_obj_center(state->mark);
    if(!lg_add_event_cb(control, lg_slide_event, LV_EVENT_PRESSED, NULL) ||
       !lg_add_event_cb(control, lg_slide_event, LV_EVENT_PRESSING, NULL) ||
       !lg_add_event_cb(control, lg_slide_event, LV_EVENT_RELEASED, NULL) ||
       !lg_add_event_cb(control, lg_slide_event, LV_EVENT_PRESS_LOST, NULL) ||
       !lg_add_event_cb(control, lg_slide_size, LV_EVENT_SIZE_CHANGED, NULL))
        goto fail;
    if(cb != NULL &&
       !lg_add_event_cb(control, cb, LV_EVENT_VALUE_CHANGED, user_data))
        goto fail;
    lg_slide_apply(control, state, 0);
    return control;

fail:
    lv_obj_delete(control);
    return NULL;
}

int vqeaf_lg_slide_confirmed(lv_obj_t *control)
{
    lg_state_t *state = lg_state(control);
    return state != NULL && state->kind == LG_SLIDE && state->value;
}

void vqeaf_lg_slide_reset(lv_obj_t *control)
{
    lg_state_t *state = lg_state(control);
    if(state != NULL && state->kind == LG_SLIDE && !state->dragging)
        lg_slide_apply(control, state, 0);
}

lv_obj_t *vqeaf_lg_card(lv_obj_t *parent, int32_t width, int32_t height)
{
    lv_obj_t *card = lg_obj_create(parent);

    if(card == NULL) return NULL;
    lg_clickable(card);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(card, width, height);
    lg_glass(card, VQEAF_M3_SHAPE_XL);
    vqeaf_m3_elevate(card, 1);
    return card;
}

static void lg_slider_apply(lv_obj_t *slider, lg_state_t *state);

static void lg_slider_size(lv_event_t *event)
{
    lg_slider_apply(lv_event_get_target_obj(event), lg_state(lv_event_get_target_obj(event)));
}

static void lg_slider_apply(lv_obj_t *slider, lg_state_t *state)
{
    int32_t w;
    int32_t active_w;
    int32_t thumb_x;
    int32_t thumb_max;

    if(state == NULL || state->active == NULL || state->thumb == NULL) return;
    w = lv_obj_get_width(slider);
    if(w < 0) w = 0;
    active_w = (int32_t)((int64_t)w * state->value / 100);
    thumb_max = w > 24 ? w - 24 : 0;
    thumb_x = active_w - 24;
    if(thumb_x < 0) thumb_x = 0;
    if(thumb_x > thumb_max) thumb_x = thumb_max;
    lv_obj_set_width(state->active, active_w);
    lv_obj_set_x(state->thumb, thumb_x);
}

static void lg_slider_scale_cb(void *var, int32_t value)
{
    lv_obj_t *thumb = (lv_obj_t *)var;
    lv_obj_set_style_transform_scale_x(thumb, value, 0);
    lv_obj_set_style_transform_scale_y(thumb, 256 + (value - 256) / 2, 0);
}

static void lg_animate_slider_thumb(lv_obj_t *thumb, int to, int duration)
{
    lv_anim_t anim;
    int from = lv_obj_get_style_transform_scale_x(thumb, 0);

    lv_anim_delete(thumb, NULL);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, thumb);
    lv_anim_set_exec_cb(&anim, lg_slider_scale_cb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_duration(&anim, duration);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

static void lg_height_cb(void *var, int32_t value)
{
    lv_obj_set_style_height((lv_obj_t *)var, value, 0);
}

static void lg_animate_height(lv_obj_t *obj, int to, int duration)
{
    lv_anim_t anim;
    int from = lv_obj_get_style_height(obj, 0);

    lv_anim_delete(obj, NULL);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, lg_height_cb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_duration(&anim, duration);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

static void lg_slider_event(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);
    lg_state_t *state = lg_state(slider);
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev;
    lv_point_t point;
    lv_area_t area;
    int32_t x, w;

    if(state == NULL) return;
    if(code == LV_EVENT_PRESSED) {
        lg_animate_slider_thumb(state->thumb, 384, 130);
        lg_animate_height(state->active, 20, 130);
    }
    if(code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        indev = lv_indev_active();
        if(indev == NULL) return;
        lv_indev_get_point(indev, &point);
        lv_obj_get_coords(slider, &area);
        w = area.x2 - area.x1 + 1;
        x = point.x - area.x1;
        if(x < 0) x = 0;
        if(x > w) x = w;
        int value = w > 0 ? (int)((int64_t)x * 100 / w) : 0;
        if(w > 0 && x >= w - 1) value = 100;
        vqeaf_lg_slider_set(slider, value);
        lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, NULL);
    } else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lg_animate_slider_thumb(state->thumb, 256, 180);
        lg_animate_height(state->active, 12, 180);
    }
}

lv_obj_t *vqeaf_lg_slider(lv_obj_t *parent, int value)
{
    lv_obj_t *slider = lg_obj_create(parent);
    lg_state_t *state;

    if(slider == NULL) return NULL;
    lg_clickable(slider);
    lv_obj_set_size(slider, lv_pct(100), 48);
    state = lg_new_state(slider, LG_SLIDER);
    if(state == NULL) goto fail;
    state->active = lg_obj_create(slider);
    if(state->active == NULL) goto fail;
    lg_glass(state->active, LV_RADIUS_CIRCLE);
    lv_obj_set_size(state->active, 0, 12);
    lv_obj_align(state->active, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(state->active, VQEAF_C_PRIMARY, 0);
    lv_obj_remove_flag(state->active, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    state->thumb = lg_obj_create(slider);
    if(state->thumb == NULL) goto fail;
    lg_glass(state->thumb, LV_RADIUS_CIRCLE);
    lv_obj_set_size(state->thumb, 24, 24);
    lv_obj_align(state->thumb, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_remove_flag(state->thumb, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    if(!lg_add_event_cb(slider, lg_slider_event, LV_EVENT_PRESSED, NULL) ||
       !lg_add_event_cb(slider, lg_slider_event, LV_EVENT_PRESSING, NULL) ||
       !lg_add_event_cb(slider, lg_slider_event, LV_EVENT_RELEASED, NULL) ||
       !lg_add_event_cb(slider, lg_slider_event, LV_EVENT_PRESS_LOST, NULL) ||
       !lg_add_event_cb(slider, lg_slider_size, LV_EVENT_SIZE_CHANGED, NULL))
        goto fail;
    vqeaf_lg_slider_set(slider, value);
    return slider;

fail:
    lv_obj_delete(slider);
    return NULL;
}

void vqeaf_lg_slider_set(lv_obj_t *slider, int value)
{
    lg_state_t *state = lg_state(slider);

    if(state == NULL || state->kind != LG_SLIDER) return;
    state->value = value < 0 ? 0 : (value > 100 ? 100 : value);
    lg_slider_apply(slider, state);
}

int vqeaf_lg_slider_get(lv_obj_t *slider)
{
    lg_state_t *state = lg_state(slider);
    return state != NULL && state->kind == LG_SLIDER ? state->value : 0;
}

static void lg_switch_apply(lv_obj_t *control, lg_state_t *state)
{
    (void)control;
    if(state->thumb == NULL || state->active == NULL) return;
    lv_obj_set_x(state->thumb, state->value ? 28 : 4);
    lv_obj_set_style_bg_color(state->active,
                              state->value ? VQEAF_C_PRIMARY : VQEAF_C_SURFACE_CONTAINER_HIGH,
                              0);
    lv_obj_set_style_bg_opa(state->active, state->value ? LG_OPA(65) : LG_OPA(45), 0);
}

static void lg_switch_motion_cb(void *var, int32_t value)
{
    lv_obj_t *thumb = (lv_obj_t *)var;
    lg_state_t *state = lg_state(lv_obj_get_parent(thumb));
    int travel = value - 256;
    int x;

    if(state == NULL) return;
    x = state->value ? 4 + travel * 24 / 64 : 28 - travel * 24 / 64;
    lv_obj_set_x(thumb, x);
    lv_obj_set_style_transform_scale_x(thumb, value, 0);
    lv_obj_set_style_transform_scale_y(thumb, value, 0);
    lv_obj_set_style_opa(thumb, LG_OPA(85 - travel * 40 / 64), 0);
}

static void lg_switch_settle_cb(void *var, int32_t value)
{
    lv_obj_t *thumb = (lv_obj_t *)var;
    lg_state_t *state = lg_state(lv_obj_get_parent(thumb));

    if(state == NULL) return;
    lv_obj_set_x(thumb, state->value ? 28 : 4);
    lv_obj_set_style_transform_scale_x(thumb, value, 0);
    lv_obj_set_style_transform_scale_y(thumb, value, 0);
    lv_obj_set_style_opa(thumb, LG_OPA(45 + (320 - value) * 40 / 64), 0);
}

static void lg_switch_settle_done(lv_anim_t *anim)
{
    lv_anim_t next;

    lv_anim_init(&next);
    lv_anim_set_var(&next, anim->var);
    lv_anim_set_exec_cb(&next, lg_switch_settle_cb);
    lv_anim_set_values(&next, 320, 256);
    lv_anim_set_duration(&next, 180);
    lv_anim_set_path_cb(&next, lv_anim_path_ease_out);
    lv_anim_start(&next);
}

static void lg_switch_click(lv_event_t *event)
{
    lv_obj_t *control = lv_event_get_target_obj(event);
    lg_state_t *state = lg_state(control);
    lv_anim_t anim;

    if(state == NULL) return;
    vqeaf_lg_switch_set(control, !state->value);
    lv_obj_set_style_transform_scale_x(state->thumb, 256, 0);
    lv_obj_set_style_transform_scale_y(state->thumb, 256, 0);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, state->thumb);
    lv_anim_set_exec_cb(&anim, lg_switch_motion_cb);
    lv_anim_set_values(&anim, 256, 320);
    lv_anim_set_duration(&anim, 220);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&anim, lg_switch_settle_done);
    lv_anim_start(&anim);
    lv_obj_send_event(control, LV_EVENT_VALUE_CHANGED, NULL);
}

lv_obj_t *vqeaf_lg_switch(lv_obj_t *parent, int on)
{
    lv_obj_t *control = lg_obj_create(parent);
    lg_state_t *state;

    if(control == NULL) return NULL;
    lg_clickable(control);
    lv_obj_set_size(control, 56, 32);
    state = lg_new_state(control, LG_SWITCH);
    if(state == NULL) goto fail;
    state->active = lg_obj_create(control);
    if(state->active == NULL) goto fail;
    lg_glass(state->active, LV_RADIUS_CIRCLE);
    lv_obj_set_size(state->active, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(state->active, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    state->thumb = lg_obj_create(control);
    if(state->thumb == NULL) goto fail;
    lg_glass(state->thumb, LV_RADIUS_CIRCLE);
    lv_obj_set_size(state->thumb, 24, 24);
    lv_obj_set_y(state->thumb, 4);
    lv_obj_set_style_bg_color(state->thumb, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(state->thumb, LG_OPA(85), 0);
    lv_obj_remove_flag(state->thumb, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    if(!lg_add_event_cb(control, lg_switch_click, LV_EVENT_CLICKED, NULL))
        goto fail;
    vqeaf_lg_switch_set(control, on);
    return control;

fail:
    lv_obj_delete(control);
    return NULL;
}

void vqeaf_lg_switch_set(lv_obj_t *control, int on)
{
    lg_state_t *state = lg_state(control);
    if(state == NULL || state->kind != LG_SWITCH || state->thumb == NULL) return;
    lv_anim_delete(state->thumb, NULL);
    lv_obj_set_style_transform_scale_x(state->thumb, 256, 0);
    lv_obj_set_style_transform_scale_y(state->thumb, 256, 0);
    lv_obj_set_style_opa(state->thumb, LG_OPA(85), 0);
    state->value = !!on;
    lg_switch_apply(control, state);
}

int vqeaf_lg_switch_get(lv_obj_t *control)
{
    lg_state_t *state = lg_state(control);
    return state != NULL && state->kind == LG_SWITCH && state->value;
}

static void lg_checkbox_apply(lv_obj_t *control, lg_state_t *state)
{
    (void)control;
    if(state->mark == NULL || state->active == NULL) return;
    lv_obj_set_style_opa(state->mark, state->value ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(state->active,
                              state->value ? VQEAF_C_PRIMARY : VQEAF_C_SURFACE_CONTAINER_HIGH,
                              0);
    lv_obj_set_style_bg_opa(state->active, state->value ? LG_OPA(55) : LG_OPA(35), 0);
}

static void lg_checkbox_press(lv_event_t *event)
{
    lv_obj_t *control = lv_event_get_target_obj(event);
    lg_state_t *state = lg_state(control);
    lv_event_code_t code = lv_event_get_code(event);

    if(state == NULL) return;
    if(code == LV_EVENT_PRESSED) lg_animate_scale(state->thumb, 256, 320, 120);
    else lg_settle_scale(state->thumb, 180);
}

static void lg_checkbox_click(lv_event_t *event)
{
    lv_obj_t *control = lv_event_get_target_obj(event);
    lg_state_t *state = lg_state(control);
    int old_opa;
    int new_opa;

    if(state == NULL) return;
    old_opa = lv_obj_get_style_opa(state->mark, 0);
    vqeaf_lg_checkbox_set(control, !state->value);
    new_opa = state->value ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_obj_set_style_opa(state->mark, (lv_opa_t)old_opa, 0);
    lg_animate_opa(state->mark, new_opa, 180);
    lv_obj_send_event(control, LV_EVENT_VALUE_CHANGED, NULL);
}

lv_obj_t *vqeaf_lg_checkbox(lv_obj_t *parent, const char *label, int checked)
{
    lv_obj_t *control = lg_obj_create(parent);
    lg_state_t *state;

    if(control == NULL) return NULL;
    lg_clickable(control);
    lv_obj_set_size(control, lv_pct(100), 48);
    lv_obj_set_flex_flow(control, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(control, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(control, 14, 0);
    state = lg_new_state(control, LG_CHECKBOX);
    if(state == NULL) goto fail;
    state->active = lg_obj_create(control);
    if(state->active == NULL) goto fail;
    lg_glass(state->active, VQEAF_M3_SHAPE_S);
    lv_obj_set_size(state->active, 32, 32);
    lv_obj_remove_flag(state->active, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    state->mark = lg_glyph(state->active, VQEAF_SYM_CHECK,
                           VQEAF_ICON_SM, VQEAF_C_ON_PRIMARY);
    if(state->mark == NULL) goto fail;
    lv_obj_center(state->mark);
    state->thumb = lg_obj_create(state->active);
    if(state->thumb == NULL) goto fail;
    lg_glass(state->thumb, VQEAF_M3_SHAPE_S);
    lv_obj_set_size(state->thumb, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(state->thumb, LV_OPA_20, 0);
    lv_obj_remove_flag(state->thumb, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    state->visual = lg_label(control, label, VQEAF_FONT_BODY, VQEAF_C_TEXT);
    if(state->visual == NULL) goto fail;
    if(!lg_add_event_cb(control, lg_checkbox_press, LV_EVENT_PRESSED, NULL) ||
       !lg_add_event_cb(control, lg_checkbox_press, LV_EVENT_RELEASED, NULL) ||
       !lg_add_event_cb(control, lg_checkbox_press, LV_EVENT_PRESS_LOST, NULL) ||
       !lg_add_event_cb(control, lg_checkbox_click, LV_EVENT_CLICKED, NULL))
        goto fail;
    vqeaf_lg_checkbox_set(control, checked);
    return control;

fail:
    lv_obj_delete(control);
    return NULL;
}

void vqeaf_lg_checkbox_set(lv_obj_t *control, int checked)
{
    lg_state_t *state = lg_state(control);
    if(state == NULL || state->kind != LG_CHECKBOX || state->mark == NULL ||
       state->thumb == NULL) return;
    lv_anim_delete(state->mark, NULL);
    lv_anim_delete(state->thumb, NULL);
    lv_obj_set_style_transform_scale_x(state->thumb, 256, 0);
    lv_obj_set_style_transform_scale_y(state->thumb, 256, 0);
    state->value = !!checked;
    lg_checkbox_apply(control, state);
}

int vqeaf_lg_checkbox_get(lv_obj_t *control)
{
    lg_state_t *state = lg_state(control);
    return state != NULL && state->kind == LG_CHECKBOX && state->value;
}

/* Scrim, neck and panel stay siblings on the top layer so anchored cards do
 * not depend on full-screen wrapper hit testing. */
typedef struct lg_overlay {
    lv_obj_t *anchor;
    lg_state_t *state;
    lv_obj_t *scrim;
    lv_obj_t *surface;
    lv_obj_t *neck;
    uint32_t generation;
    int queued;
    struct lg_overlay *next;
} lg_overlay_t;

static lg_overlay_t *g_overlays;
static lg_overlay_t *g_active_overlay;
static uint32_t g_overlay_generation;
static void overlay_dismiss_async(void *arg);

static int overlay_contains(const lg_overlay_t *overlay)
{
    const lg_overlay_t *item;

    for(item = g_overlays; item != NULL; item = item->next)
        if(item == overlay) return 1;
    return 0;
}

static int overlay_live(const lg_overlay_t *overlay, uint32_t generation)
{
    const lg_overlay_t *item;

    for(item = g_overlays; item != NULL; item = item->next)
        if(item == overlay) return item->generation == generation;
    return 0;
}

static lg_overlay_t *overlay_find_anchor(lv_obj_t *anchor)
{
    lg_overlay_t *overlay;

    for(overlay = g_overlays; overlay != NULL; overlay = overlay->next)
        if(overlay->anchor == anchor) return overlay;
    return NULL;
}

static lg_overlay_t *overlay_find_scrim(lv_obj_t *scrim)
{
    lg_overlay_t *overlay;

    for(overlay = g_overlays; overlay != NULL; overlay = overlay->next)
        if(overlay->scrim == scrim) return overlay;
    return NULL;
}

static lg_overlay_t *overlay_find_surface(lv_obj_t *surface)
{
    lg_overlay_t *overlay;

    for(overlay = g_overlays; overlay != NULL; overlay = overlay->next)
        if(overlay->surface == surface) return overlay;
    return NULL;
}

static void overlay_unlink(lg_overlay_t *overlay)
{
    lg_overlay_t **link = &g_overlays;

    while(*link != NULL) {
        if(*link == overlay) {
            *link = overlay->next;
            return;
        }
        link = &(*link)->next;
    }
}

static void overlay_destroy(lg_overlay_t *overlay, uint32_t generation,
                            int delete_scrim)
{
    lv_obj_t *scrim;
    lv_obj_t *surface;
    lv_obj_t *neck;

    if(!overlay_live(overlay, generation)) return;
    scrim = overlay->scrim;
    surface = overlay->surface;
    neck = overlay->neck;
    overlay_unlink(overlay);
    if(g_active_overlay == overlay) {
        lg_overlay_t *next;

        g_active_overlay = NULL;
        for(next = g_overlays; next != NULL; next = next->next)
            if(!next->queued) {
                g_active_overlay = next;
                break;
            }
    }
    if(overlay->queued)
        lv_async_call_cancel(overlay_dismiss_async, overlay);
    lv_anim_delete(surface, NULL);
    lv_anim_delete(neck, NULL);
    if(neck != NULL) lv_obj_delete(neck);
    if(surface != NULL) lv_obj_delete(surface);
    if(delete_scrim && scrim != NULL) lv_obj_delete(scrim);
    lv_free(overlay);
}

static void overlay_dismiss_async(void *arg)
{
    lg_overlay_t *overlay = (lg_overlay_t *)arg;
    uint32_t generation;

    if(!overlay_contains(overlay)) return;
    generation = overlay->generation;
    overlay->queued = 0;
    overlay_destroy(overlay, generation, 1);
}

static lv_result_t lg_async_call(lv_async_cb_t cb, void *user_data)
{
#ifdef VQEAF_LG_TESTING
    if(lg_test_async_fail) return LV_RESULT_INVALID;
#endif
    return lv_async_call(cb, user_data);
}

static void overlay_dismiss(lg_overlay_t *overlay)
{
    lv_indev_t *indev = lv_indev_active();
    uint32_t generation;

    if(!overlay_contains(overlay)) return;
    generation = overlay->generation;
    if(overlay->queued) return;
    if(indev != NULL) lv_indev_wait_release(indev);
    if(!overlay_live(overlay, generation)) return;
    overlay->queued = 1;
    if(lg_async_call(overlay_dismiss_async, overlay) != LV_RESULT_OK) {
        overlay->queued = 0;
        overlay_destroy(overlay, generation, 1);
    }
}

static void overlay_backdrop(lv_event_t *event)
{
    lg_overlay_t *overlay = overlay_find_scrim(lv_event_get_target_obj(event));

    if(overlay != NULL) overlay_dismiss(overlay);
}

static void overlay_delete_cb(lv_event_t *event)
{
    lg_overlay_t *overlay = overlay_find_scrim(lv_event_get_target_obj(event));

    if(overlay != NULL)
        overlay_destroy(overlay, overlay->generation, 0);
}

static void lg_item_click(lv_event_t *event)
{
    lv_obj_t *item = lv_event_get_target_obj(event);
    lg_overlay_t *overlay = overlay_find_surface(lv_obj_get_parent(item));
    lg_state_t *state;
    lv_obj_t *anchor;
    uint32_t generation;
    int index = (int)(intptr_t)lv_event_get_user_data(event);

    if(overlay == NULL) return;
    state = overlay->state;
    anchor = overlay->anchor;
    generation = overlay->generation;
    if(state == NULL || anchor == NULL || index < 0 || index >= state->count ||
       state->items == NULL || state->items[index].disabled) return;
    if(state->kind == LG_SELECT) {
        state->value = index;
        if(state->visual != NULL)
            lv_label_set_text(state->visual, state->items[index].text);
        if(!overlay_live(overlay, generation) || overlay->state != state) return;
        if(lv_obj_send_event(anchor, LV_EVENT_VALUE_CHANGED, NULL) == LV_RESULT_INVALID)
            return;
    }
    if(!overlay_live(overlay, generation) || overlay->state != state ||
       anchor == NULL) return;
    if(lv_obj_send_event(anchor, LV_EVENT_CLICKED, NULL) == LV_RESULT_INVALID) return;
    if(overlay_live(overlay, generation)) overlay_dismiss(overlay);
}

int vqeaf_lg_any_overlay_visible(void)
{
    return g_overlays != NULL;
}

int vqeaf_lg_overlay_visible(lv_obj_t *control)
{
    return control != NULL && g_active_overlay != NULL &&
           g_active_overlay->anchor == control && !g_active_overlay->queued;
}

void vqeaf_lg_overlay_close(lv_obj_t *control)
{
    lg_overlay_t *overlay;

    if(control == NULL || g_active_overlay == NULL ||
       g_active_overlay->anchor != control) return;
    overlay = g_active_overlay;
    overlay->anchor = NULL;
    overlay->state = NULL;
    if(lv_indev_active() != NULL) overlay_dismiss(overlay);
    else overlay_destroy(overlay, overlay->generation, 1);
}

#ifdef VQEAF_LG_TESTING
void vqeaf_lg_test_queue_close(lv_obj_t *control)
{
    lg_overlay_t *overlay = overlay_find_anchor(control);

    if(overlay != NULL) overlay_dismiss(overlay);
}
#endif

static int32_t lg_clamp_i32(int32_t value, int32_t minimum, int32_t maximum)
{
    if(maximum < minimum) maximum = minimum;
    if(value < minimum) return minimum;
    if(value > maximum) return maximum;
    return value;
}

static void overlay_position(lv_obj_t *anchor, lv_obj_t *panel, lv_obj_t *neck,
                             int side)
{
    lv_display_t *display;

    lv_obj_update_layout(anchor);
    lv_obj_update_layout(panel);
    display = lv_obj_get_display(anchor);
    lv_area_t a;
    int32_t sw = lv_display_get_horizontal_resolution(display);
    int32_t sh = lv_display_get_vertical_resolution(display);
    int32_t pw = lv_obj_get_width(panel);
    int32_t ph = lv_obj_get_height(panel);
    int32_t margin_x = sw > 32 ? 16 : 0;
    int32_t margin_y = sh > 32 ? 16 : 0;
    int32_t x, y, nx, ny, nw, nh;

    if(sw < 0) sw = 0;
    if(sh < 0) sh = 0;
    if(pw > sw) pw = sw > 0 ? sw : 1;
    if(ph > sh) ph = sh > 0 ? sh : 1;
    lv_obj_get_coords(anchor, &a);
    if(side == 0) {
        y = a.y2 + 12;
        if(y > sh - ph - margin_y) y = a.y1 - ph - 12;
    } else {
        y = a.y1;
    }
    y = lg_clamp_i32(y, margin_y, sh - ph - margin_y);
    if(side < 0) x = a.x1 - pw + 44;
    else if(side > 0) x = a.x2 - 44;
    else x = a.x1;
    x = lg_clamp_i32(x, margin_x, sw - pw - margin_x);
    lv_obj_set_size(panel, pw, ph);
    lv_obj_set_pos(panel, x, y);

    nw = side == 0 ? 24 : 12;
    nh = side == 0 ? 12 : 48;
    if(nw > pw) nw = pw;
    if(nh > ph) nh = ph;
    if(nw < 0) nw = 0;
    if(nh < 0) nh = 0;
    if(side == 0) {
        nx = (a.x1 + a.x2) / 2 - nw / 2;
        ny = y > a.y2 ? y - nh : y + ph;
    } else {
        nx = x < a.x1 ? x + pw : x - nw;
        ny = y + 24;
    }
    nx = lg_clamp_i32(nx, 0, sw - nw);
    ny = lg_clamp_i32(ny, 0, sh - nh);
    lv_obj_set_pos(neck, nx, ny);
    lv_obj_set_size(neck, nw, nh);
}

void vqeaf_lg_overlay_open(lv_obj_t *control)
{
    lg_state_t *state = lg_state(control);
    lg_overlay_t *overlay;
    lv_display_t *display;
    lv_area_t anchor_area;
    lv_obj_t *scrim = NULL;
    lv_obj_t *surface = NULL;
    lv_obj_t *neck = NULL;
    int32_t sw, sh, pw, ph, max_pw, max_ph, side;
    int i;

    if(state == NULL || (state->kind != LG_SELECT && state->kind != LG_MENU) ||
       state->count <= 0 || state->count > VQEAF_LG_MAX_ITEMS ||
       state->items == NULL) return;
    if(vqeaf_lg_overlay_visible(control)) {
        vqeaf_lg_overlay_close(control);
        return;
    }
    overlay = lg_alloc_zeroed(sizeof(*overlay));
    if(overlay == NULL) return;
    display = lv_obj_get_display(control);
    if(display == NULL || lv_layer_top() == NULL) {
        lv_free(overlay);
        return;
    }
    sw = lv_display_get_horizontal_resolution(display);
    sh = lv_display_get_vertical_resolution(display);
    if(sw < 0) sw = 0;
    if(sh < 0) sh = 0;
    scrim = lg_obj_create(lv_layer_top());
    if(scrim == NULL) goto fail;
    lv_obj_remove_style_all(scrim);
    lv_obj_remove_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(scrim, sw, sh);
    lv_obj_set_style_bg_color(scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scrim, LG_OPA(35), 0);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
    if(!lg_add_event_cb(scrim, overlay_backdrop, LV_EVENT_CLICKED, NULL) ||
       !lg_add_event_cb(scrim, overlay_delete_cb, LV_EVENT_DELETE, NULL))
        goto fail;

    neck = lg_obj_create(lv_layer_top());
    if(neck == NULL) goto fail;
    lg_glass(neck, VQEAF_M3_SHAPE_S);
    surface = lg_obj_create(lv_layer_top());
    if(surface == NULL) goto fail;
    lg_glass(surface, VQEAF_M3_SHAPE_XL);
    lv_obj_get_coords(control, &anchor_area);
    max_pw = sw > 32 ? sw - 32 : sw;
    if(max_pw < 1) max_pw = 1;
    pw = anchor_area.x2 - anchor_area.x1 + 1;
    if(pw < 280) pw = 280;
    if(pw > max_pw) pw = max_pw;
    max_ph = sh > 40 ? sh - 40 : sh;
    if(max_ph < 1) max_ph = 1;
    ph = (int32_t)((int64_t)state->count * 52 + 16);
    if(ph > max_ph) ph = max_ph;
    if(ph < 68) ph = max_ph < 68 ? max_ph : 68;
    lv_obj_set_size(surface, pw, ph);
    lv_obj_set_style_pad_all(surface, 8, 0);
    lv_obj_add_flag(surface, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(surface, LV_DIR_VER);
    lv_obj_set_flex_flow(surface, LV_FLEX_FLOW_COLUMN);
    vqeaf_m3_elevate(surface, 3);
    for(i = 0; i < state->count; i++) {
        lv_obj_t *item = lg_obj_create(surface);
        lv_obj_t *label;

        if(item == NULL) goto fail;
        lg_clickable(item);
        lv_obj_set_size(item, lv_pct(100), 52);
        lv_obj_set_style_radius(item, VQEAF_M3_SHAPE_M, 0);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(item, 12, 0);
        if(state->items[i].symbol != NULL) {
            lv_obj_t *glyph = lg_glyph(item, state->items[i].symbol,
                                       VQEAF_ICON_SM, VQEAF_C_PRIMARY);
            if(glyph == NULL) goto fail;
        }
        label = lg_label(item, state->items[i].text, VQEAF_FONT_BODY,
                         state->items[i].disabled ? VQEAF_C_TEXT_MUTE : VQEAF_C_TEXT);
        if(label == NULL) goto fail;
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
        if(!lg_add_event_cb(item, lg_item_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i))
            goto fail;
        if(state->items[i].disabled) {
            lv_obj_remove_flag(item, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        }
    }
    side = state->kind == LG_SELECT ? 0 : state->value;
    overlay_position(control, surface, neck, side);
    if(g_active_overlay != NULL && g_active_overlay->anchor != NULL)
        vqeaf_lg_overlay_close(g_active_overlay->anchor);
    if(++g_overlay_generation == 0) g_overlay_generation = 1;
    overlay->anchor = control;
    overlay->state = state;
    overlay->scrim = scrim;
    overlay->surface = surface;
    overlay->neck = neck;
    overlay->generation = g_overlay_generation;
    overlay->next = g_overlays;
    g_overlays = overlay;
    g_active_overlay = overlay;
    lg_animate_surface(surface);
    lg_animate_scale(neck, 32, 256, 220);
    lv_obj_update_layout(lv_layer_top());
    lv_obj_invalidate(lv_layer_top());
    return;

fail:
    lv_anim_delete(surface, NULL);
    lv_anim_delete(neck, NULL);
    if(neck != NULL) lv_obj_delete(neck);
    if(surface != NULL) lv_obj_delete(surface);
    if(scrim != NULL) lv_obj_delete(scrim);
    lv_free(overlay);
}

static void lg_free_items(vqeaf_component_item_t *items, int count)
{
    int i;

    if(items == NULL) return;
    for(i = 0; i < count; i++) {
        if(items[i].text != NULL) lv_free((void *)items[i].text);
        if(items[i].symbol != NULL) lv_free((void *)items[i].symbol);
    }
    lv_free(items);
}

static vqeaf_component_item_t *lg_copy_items(const vqeaf_component_item_t *items,
                                             int count)
{
    vqeaf_component_item_t *copy;
    int i;

    if(items == NULL || count <= 0 || count > VQEAF_LG_MAX_ITEMS ||
       (size_t)count > SIZE_MAX / sizeof(*copy)) return NULL;
    copy = lg_alloc_zeroed((size_t)count * sizeof(*copy));
    if(copy == NULL) return NULL;
    for(i = 0; i < count; i++) {
        const char *text = items[i].text != NULL ? items[i].text : "";
        const char *symbol = items[i].symbol != NULL ? items[i].symbol : "";

        copy[i].text = lg_strdup(text);
        if(copy[i].text == NULL) goto fail;
        if(items[i].symbol != NULL) {
            copy[i].symbol = lg_strdup(symbol);
            if(copy[i].symbol == NULL) goto fail;
        }
        copy[i].disabled = !!items[i].disabled;
    }
    return copy;

fail:
    for(i = 0; i < count; i++) {
        if(copy[i].text != NULL) lv_free((void *)copy[i].text);
        if(copy[i].symbol != NULL) lv_free((void *)copy[i].symbol);
    }
    lv_free(copy);
    return NULL;
}

static lg_state_t *lg_menu_state(lv_obj_t *parent, const char *label,
                                 const vqeaf_component_item_t *items, int count,
                                 int selected)
{
    vqeaf_component_item_t *copy = lg_copy_items(items, count);
    lv_obj_t *control;
    lv_obj_t *glyph;
    lg_state_t *state;

    if(copy == NULL) return NULL;
    control = lg_obj_create(parent);
    if(control == NULL) {
        lg_free_items(copy, count);
        return NULL;
    }
    lg_clickable(control);
    lv_obj_set_size(control, lv_pct(100), 56);
    lg_glass(control, LV_RADIUS_CIRCLE);
    lv_obj_set_flex_flow(control, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(control, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(control, 18, 0);
    state = lg_new_state(control, LG_MENU);
    if(state == NULL) {
        lg_free_items(copy, count);
        lv_obj_delete(control);
        return NULL;
    }
    state->value = selected;
    state->count = count;
    state->items = copy;
    state->visual = lg_label(control, label != NULL ? label : "Mở",
                             VQEAF_FONT_BODY, VQEAF_C_TEXT);
    if(state->visual == NULL) goto fail;
    lv_obj_set_flex_grow(state->visual, 1);
    lv_obj_set_style_text_align(state->visual, LV_TEXT_ALIGN_LEFT, 0);
    glyph = lg_glyph(control, VQEAF_SYM_KEYBOARD_ARROW_DOWN,
                     VQEAF_ICON_SM, VQEAF_C_PRIMARY);
    if(glyph == NULL) goto fail;
    if(!lg_add_event_cb(control, lg_press_cb, LV_EVENT_PRESSED, NULL) ||
       !lg_add_event_cb(control, lg_press_cb, LV_EVENT_RELEASED, NULL) ||
       !lg_add_event_cb(control, lg_press_cb, LV_EVENT_PRESS_LOST, NULL) ||
       !lg_add_event_cb(control, lg_overlay_click, LV_EVENT_CLICKED, NULL))
        goto fail;
    return state;

fail:
    lv_obj_delete(control);
    return NULL;
}

static void lg_overlay_click(lv_event_t *event)
{
    vqeaf_lg_overlay_open(lv_event_get_target_obj(event));
}

lv_obj_t *vqeaf_lg_select(lv_obj_t *parent,
                          const vqeaf_component_item_t *items, int count,
                          int selected, lv_event_cb_t cb, void *user_data)
{
    lg_state_t *state = lg_menu_state(parent,
        items != NULL && selected >= 0 && selected < count ? items[selected].text : "Chọn",
        items, count, selected);
    lv_obj_t *control;

    if(state == NULL) return NULL;
    control = lv_obj_get_parent(state->visual);
    state->kind = LG_SELECT;
    state->value = lg_clamp_index(selected, count);
    if(cb != NULL &&
       !lg_add_event_cb(control, cb, LV_EVENT_VALUE_CHANGED, user_data)) {
        lv_obj_delete(control);
        return NULL;
    }
    return control;
}

static void lg_group_click(lv_event_t *event)
{
    lv_obj_t *item = lv_event_get_target_obj(event);
    lv_obj_t *group = lv_obj_get_parent(item);
    int index = (int)(intptr_t)lv_event_get_user_data(event);

    if(group == NULL) return;
    vqeaf_lg_button_group_select(group, index);
    lv_obj_send_event(group, LV_EVENT_VALUE_CHANGED, NULL);
}

static void lg_group_apply(lv_obj_t *group, lg_state_t *state)
{
    int child_count = (int)lv_obj_get_child_count(group);
    int i;

    for(i = 0; i < state->count && i < child_count; i++) {
        lv_obj_t *item = lv_obj_get_child(group, i);
        lv_obj_t *label = lv_obj_get_child(item, 0);

        if(label == NULL) continue;
        lv_obj_set_style_bg_opa(item, i == state->value ? LG_OPA(55) : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(label,
                                    i == state->value ? VQEAF_C_PRIMARY
                                                      : VQEAF_C_TEXT_DIM, 0);
    }
}

lv_obj_t *vqeaf_lg_button_group(lv_obj_t *parent, const char *const *labels,
                                int count, int selected)
{
    lv_obj_t *group;
    lg_state_t *state;
    int i;

    if(labels == NULL || count <= 0 || count > VQEAF_LG_MAX_ITEMS) return NULL;
    group = lg_obj_create(parent);
    if(group == NULL) return NULL;
    lg_clickable(group);
    lv_obj_remove_flag(group, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(group, lv_pct(100), 56);
    lg_glass(group, LV_RADIUS_CIRCLE);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(group, 4, 0);
    state = lg_new_state(group, LG_GROUP);
    if(state == NULL) goto fail;
    state->count = count;
    for(i = 0; i < count; i++) {
        lv_obj_t *item = lg_obj_create(group);
        lv_obj_t *label;

        if(item == NULL) goto fail;
        lg_clickable(item);
        lv_obj_set_height(item, lv_pct(100));
        lv_obj_set_flex_grow(item, 1);
        lv_obj_set_style_radius(item, LV_RADIUS_CIRCLE, 0);
        label = lg_label(item, labels[i], VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
        if(label == NULL) goto fail;
        if(!lg_add_event_cb(item, lg_group_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i))
            goto fail;
    }
    vqeaf_lg_button_group_select(group, selected);
    return group;

fail:
    lv_obj_delete(group);
    return NULL;
}

void vqeaf_lg_button_group_select(lv_obj_t *group, int index)
{
    lg_state_t *state = lg_state(group);
    if(state == NULL || state->kind != LG_GROUP || state->count <= 0) return;
    state->value = lg_clamp_index(index, state->count);
    lg_group_apply(group, state);
}

int vqeaf_lg_button_group_selected(lv_obj_t *group)
{
    lg_state_t *state = lg_state(group);
    return state != NULL && state->kind == LG_GROUP ? state->value : -1;
}

lv_obj_t *vqeaf_lg_button_group_item(lv_obj_t *group, int index)
{
    lg_state_t *state = lg_state(group);
    return state != NULL && state->kind == LG_GROUP && index >= 0 &&
           index < state->count &&
           index < (int)lv_obj_get_child_count(group) ?
           lv_obj_get_child(group, index) : NULL;
}

lv_obj_t *vqeaf_lg_dropdown(lv_obj_t *parent, const char *label,
                            const vqeaf_component_item_t *items, int count,
                            lv_event_cb_t cb, void *user_data)
{
    lg_state_t *state = lg_menu_state(parent, label, items, count, 0);
    lv_obj_t *control = state != NULL ? lv_obj_get_parent(state->visual) : NULL;

    if(control != NULL && cb != NULL &&
       !lg_add_event_cb(control, cb, LV_EVENT_CLICKED, user_data)) {
        lv_obj_delete(control);
        return NULL;
    }
    return control;
}

lv_obj_t *vqeaf_lg_popover(lv_obj_t *parent, const char *title,
                           const char *message, const char *action)
{
    const vqeaf_component_item_t item[] = {
        { action != NULL ? action : "Đã hiểu", NULL, 0 }
    };
    lg_state_t *state = lg_menu_state(parent, title, item, 1, 1);
    lv_obj_t *control = state != NULL ? lv_obj_get_parent(state->visual) : NULL;
    lv_obj_t *heading;

    if(state == NULL) return NULL;
    lv_label_set_text(state->visual, message != NULL ? message : "");
    lv_obj_set_height(control, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(control, 18, 0);
    lv_obj_set_flex_flow(control, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(control, 8, 0);
    heading = lg_label(control, title, VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    if(heading == NULL) {
        lv_obj_delete(control);
        return NULL;
    }
    state->visual = heading;
    lv_obj_set_width(heading, lv_pct(100));
    return control;
}

lv_obj_t *vqeaf_lg_menu(lv_obj_t *parent, const char *label,
                        const vqeaf_component_item_t *items, int count,
                        int side, lv_event_cb_t cb, void *user_data)
{
    lg_state_t *state = lg_menu_state(parent, label, items, count, side);
    lv_obj_t *control = state != NULL ? lv_obj_get_parent(state->visual) : NULL;

    if(control != NULL && cb != NULL &&
       !lg_add_event_cb(control, cb, LV_EVENT_CLICKED, user_data)) {
        lv_obj_delete(control);
        return NULL;
    }
    return control;
}

void vqeaf_lg_select_set(lv_obj_t *control, int index)
{
    lg_state_t *state = lg_state(control);

    if(state == NULL || state->kind != LG_SELECT || state->count <= 0 ||
       state->items == NULL || state->visual == NULL) return;
    state->value = lg_clamp_index(index, state->count);
    lv_label_set_text(state->visual, state->items[state->value].text);
}

int vqeaf_lg_selected_index(lv_obj_t *control)
{
    lg_state_t *state = lg_state(control);
    return state != NULL && state->kind == LG_SELECT ? state->value : -1;
}
