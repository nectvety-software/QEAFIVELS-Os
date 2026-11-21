/* SPDX-License-Identifier: MIT */
/* platform/violet/splash.h - boot splash for the VQEAF-OS violet port.
 *
 * The first violet image of this tree renders a static splash into the
 * XRGB8888 UI canvas (720x1560 portrait or 1560x720 landscape); the panel
 * scaler and DRM scanout in this same directory put it on the glass. No
 * widget toolkit: pure integer drawing with the embedded 8x8 font.
 */
#ifndef VQEAF_VIOLET_SPLASH_H
#define VQEAF_VIOLET_SPLASH_H

#include <stdint.h>

/* Fills the whole canvas: black background, an accent bar, the OS name and
 * the boot lines passed as a NULL-terminated array of ASCII strings. */
void vio_splash_render(uint32_t *buf, int w, int h,
                       const char *title, const char *subtitle,
                       const char *const *lines);

#endif /* VQEAF_VIOLET_SPLASH_H */
