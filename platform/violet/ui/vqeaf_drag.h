/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_DRAG_H
#define VQEAF_DRAG_H

#include "lvgl.h"

typedef int (*vqeaf_drag_target_fn)(lv_point_t point, void *context);
typedef void (*vqeaf_drag_commit_fn)(lv_obj_t *source, int target, void *context);
typedef void (*vqeaf_drag_fallback_fn)(lv_obj_t *source, void *context);

int vqeaf_drag_attach(lv_obj_t *obj, int source_id, const char *symbol,
                      vqeaf_drag_target_fn target, vqeaf_drag_commit_fn commit,
                      vqeaf_drag_fallback_fn fallback, void *context);
int vqeaf_drag_active(void);

#endif
