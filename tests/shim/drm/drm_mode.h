/* Minimal stand-in for the UAPI <drm/drm_mode.h> so the host mode-pick test
 * can compile without libdrm-dev. Only the fields the pick rule reads are
 * exercised, but the layout mirrors the real UAPI struct. */
#ifndef QEAF_TEST_DRM_MODE_SHIM_H
#define QEAF_TEST_DRM_MODE_SHIM_H

#include <stdint.h>

#define DRM_DISPLAY_MODE_LEN 32
#define DRM_MODE_TYPE_PREFERRED (1 << 3)
#define DRM_MODE_TYPE_DRIVER (1 << 6)

struct drm_mode_modeinfo {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
    char name[DRM_DISPLAY_MODE_LEN];
};

#endif
