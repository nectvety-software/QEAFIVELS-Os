/* SPDX-License-Identifier: MIT */
#include "vqeaf_drag.h"

#include <stdlib.h>

#include "vqeaf_fonts.h"
#include "vqeaf_theme.h"

typedef struct {
    lv_obj_t *source;
    int source_id;
    const char *symbol;
    vqeaf_drag_target_fn target;
    vqeaf_drag_commit_fn commit;
    vqeaf_drag_fallback_fn fallback;
    void *context;
} drag_binding_t;

static drag_binding_t *g_active;
static lv_obj_t *g_ghost;
static lv_point_t g_start;
static int g_moved;
static int g_suppress_click;
static lv_obj_t *g_suppress_source;
static lv_opa_t g_source_opa;
static int g_added_press_lock;
static lv_obj_t *g_press_lock_obj;
static lv_obj_t *g_scroll_parents[8];
static int g_scroll_count;

static int point_distance(lv_point_t a, lv_point_t b)
{
    int dx = a.x - b.x;
    int dy = a.y - b.y;
    return abs(dx) + abs(dy);
}

static void drag_cancel(void)
{
    if(g_active != NULL && lv_obj_is_valid(g_active->source)) {
        lv_obj_set_style_opa(g_active->source, g_source_opa, 0);
        if(g_added_press_lock && lv_obj_is_valid(g_press_lock_obj))
            lv_obj_remove_flag(g_press_lock_obj, LV_OBJ_FLAG_PRESS_LOCK);
    }
    g_added_press_lock = 0;
    g_press_lock_obj = NULL;
    for(int i = 0; i < g_scroll_count; i++) {
        if(lv_obj_is_valid(g_scroll_parents[i]))
            lv_obj_add_flag(g_scroll_parents[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    g_scroll_count = 0;
    if(g_ghost != NULL) {
        if(lv_obj_is_valid(g_ghost)) lv_obj_delete(g_ghost);
        g_ghost = NULL;
    }
    g_active = NULL;
    g_moved = 0;
}

static void drag_event(lv_event_t *event)
{
    drag_binding_t *binding = (drag_binding_t *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev;
    lv_point_t point;
    int target;
    int moved;

    if(code == LV_EVENT_DELETE) {
        if(g_active == binding) drag_cancel();
        if(g_suppress_source == binding->source) {
            g_suppress_click = 0;
            g_suppress_source = NULL;
        }
        lv_free(binding);
        return;
    }
    if(code == LV_EVENT_PRESSED) {
        g_suppress_click = 0;
        g_suppress_source = NULL;
        return;
    }
    if(code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_CLICKED) {
        if(g_suppress_click && binding->source == g_suppress_source) {
            if(code == LV_EVENT_CLICKED) {
                g_suppress_click = 0;
                g_suppress_source = NULL;
            }
            lv_event_stop_processing(event);
        }
        return;
    }
    if(code == LV_EVENT_PRESS_LOST) {
        if(g_active == binding) drag_cancel();
        if(g_suppress_source == binding->source) {
            g_suppress_click = 0;
            g_suppress_source = NULL;
        }
        return;
    }

    indev = lv_indev_active();
    if(indev == NULL) return;
    lv_indev_get_point(indev, &point);

    if(code == LV_EVENT_LONG_PRESSED) {
        if(g_active != NULL) drag_cancel();
        lv_event_stop_processing(event);
        g_active = binding;
        g_start = point;
        g_moved = 0;
        g_suppress_click = 1;
        g_suppress_source = binding->source;
        g_source_opa = lv_obj_get_style_opa(binding->source, 0);
        g_press_lock_obj = lv_event_get_target(event);
        g_added_press_lock = !lv_obj_has_flag(g_press_lock_obj,
                                               LV_OBJ_FLAG_PRESS_LOCK);
        if(g_added_press_lock)
            lv_obj_add_flag(g_press_lock_obj, LV_OBJ_FLAG_PRESS_LOCK);
        g_scroll_count = 0;
        lv_obj_t *parent = lv_obj_get_parent(binding->source);
        while(parent != NULL && g_scroll_count < (int)(sizeof(g_scroll_parents) / sizeof(g_scroll_parents[0]))) {
            if(lv_obj_has_flag(parent, LV_OBJ_FLAG_SCROLLABLE)) {
                g_scroll_parents[g_scroll_count++] = parent;
                lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
            }
            parent = lv_obj_get_parent(parent);
        }
        return;
    }
    if(code == LV_EVENT_PRESSING && g_active == binding) {
        if(!g_moved && point_distance(point, g_start) < 12) return;
        if(!g_moved) {
            g_ghost = lv_obj_create(lv_layer_top());
            if(g_ghost == NULL) {
                drag_cancel();
                return;
            }
            lv_obj_remove_style_all(g_ghost);
            lv_obj_set_size(g_ghost, 104, 104);
            lv_obj_set_style_radius(g_ghost, 26, 0);
            lv_obj_set_style_bg_color(g_ghost, lv_color_hex(0x20304A), 0);
            lv_obj_set_style_bg_opa(g_ghost, 238, 0);
            lv_obj_set_style_border_width(g_ghost, 2, 0);
            lv_obj_set_style_border_color(g_ghost, lv_color_hex(0x67D9F2), 0);
            lv_obj_set_style_shadow_width(g_ghost, 24, 0);
            lv_obj_set_style_shadow_opa(g_ghost, 90, 0);
            lv_obj_remove_flag(g_ghost, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t *glyph = vqeaf_glyph(g_ghost, binding->symbol, VQEAF_ICON,
                                          lv_color_white());
            if(glyph != NULL) lv_obj_center(glyph);
            lv_obj_set_style_opa(binding->source, 90, 0);
            g_moved = 1;
        }
        if(!lv_obj_is_valid(g_ghost)) {
            drag_cancel();
            return;
        }
        lv_obj_set_pos(g_ghost, point.x - 52, point.y - 52);
        lv_obj_move_foreground(g_ghost);
        return;
    }
    if(code == LV_EVENT_RELEASED && g_active == binding) {
        moved = g_moved;
        target = moved && binding->target != NULL ?
                 binding->target(point, binding->context) : -1;
        drag_cancel();
        if(moved) {
            if(target >= 0 && binding->commit != NULL)
                binding->commit(binding->source, target, binding->context);
        }
        else if(binding->fallback != NULL) {
            binding->fallback(binding->source, binding->context);
        }
    }
}

int vqeaf_drag_attach(lv_obj_t *obj, int source_id, const char *symbol,
                      vqeaf_drag_target_fn target, vqeaf_drag_commit_fn commit,
                      vqeaf_drag_fallback_fn fallback, void *context)
{
    static const lv_event_code_t codes[] = {
        LV_EVENT_PRESSED,
        LV_EVENT_LONG_PRESSED,
        LV_EVENT_PRESSING,
        LV_EVENT_RELEASED,
        LV_EVENT_PRESS_LOST,
        LV_EVENT_SHORT_CLICKED,
        LV_EVENT_CLICKED,
        LV_EVENT_DELETE,
    };
    drag_binding_t *binding;
    lv_event_dsc_t *descriptors[sizeof(codes) / sizeof(codes[0])] = { 0 };
    int was_clickable;

    if(obj == NULL || symbol == NULL) return -1;
    binding = lv_malloc(sizeof(*binding));
    if(binding == NULL) return -1;
    *binding = (drag_binding_t) {
        .source = obj,
        .source_id = source_id,
        .symbol = symbol,
        .target = target,
        .commit = commit,
        .fallback = fallback,
        .context = context,
    };
    was_clickable = lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    for(size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        descriptors[i] = lv_obj_add_event_cb(obj, drag_event, codes[i], binding);
        if(descriptors[i] == NULL) {
            for(size_t j = 0; j < i; j++)
                lv_obj_remove_event_dsc(obj, descriptors[j]);
            if(!was_clickable) lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
            lv_free(binding);
            return -1;
        }
    }
    return 0;
}

int vqeaf_drag_active(void)
{
    return g_active != NULL && g_moved;
}
