/* SPDX-License-Identifier: MIT */
/* platform/violet/drm_mode_pick.h - the DRM mode choice rule.
 *
 * Header-only and free of ioctls so the host test in
 * tests/test_mt689x_mode_pick.c exercises exactly the code drm_kms.c runs.
 *
 * chopin lists five timings for the same 1080x2400 size (30/48/50/60/120 Hz)
 * and marks 120 Hz DRM_MODE_TYPE_PREFERRED. Choosing the first exact-size
 * match would silently pin the UI to 30 Hz, so PREFERRED has to win among
 * same-size modes. Violet has a single mode, where both rules agree.
 */
#ifndef HOMO_DRM_MODE_PICK_H
#define HOMO_DRM_MODE_PICK_H

#include <stdint.h>

#include <drm/drm_mode.h>

static inline int vio_pick_mode_index(const struct drm_mode_modeinfo *modes,
                                      uint32_t count,
                                      const struct drm_mode_modeinfo *current,
                                      int prefer_w, int prefer_h)
{
    uint32_t i;
    int best = -1;
    long best_area = -1;

    /* 1. right size and PREFERRED (the 120 Hz timing on chopin) */
    for(i = 0; i < count; i++) {
        if(modes[i].hdisplay == prefer_w && modes[i].vdisplay == prefer_h &&
           (modes[i].type & DRM_MODE_TYPE_PREFERRED))
            return (int)i;
    }
    /* 2. right size (single-mode panels such as violet) */
    for(i = 0; i < count; i++) {
        if(modes[i].hdisplay == prefer_w && modes[i].vdisplay == prefer_h)
            return (int)i;
    }
    /* 3. any PREFERRED */
    for(i = 0; i < count; i++) {
        if(modes[i].type & DRM_MODE_TYPE_PREFERRED)
            return (int)i;
    }
    /* 4. the mode already programmed */
    if(current && current->hdisplay && current->vdisplay) {
        for(i = 0; i < count; i++) {
            if(modes[i].hdisplay == current->hdisplay &&
               modes[i].vdisplay == current->vdisplay)
                return (int)i;
        }
    }
    /* 5. largest area */
    for(i = 0; i < count; i++) {
        long area = (long)modes[i].hdisplay * (long)modes[i].vdisplay;
        if(area > best_area) {
            best_area = area;
            best = (int)i;
        }
    }
    return best;
}

#endif /* HOMO_DRM_MODE_PICK_H */
