/* SPDX-License-Identifier: MIT */
/* platform/violet/vio_display.h - glue between the VQEAF UI canvas and the
 * display_* rotation seam that fs/rescue.c uses. */
#ifndef HOMO_VIO_DISPLAY_H
#define HOMO_VIO_DISPLAY_H

#include "panel_scale.h"

#include "hal/include/display.h"

void vio_display_bind(vio_panel_t *panel);

/* The canvas the shell wants for an orientation: 720x1560 portrait or
 * 1560x720 landscape, both exact 1.5x fits of the 1080x2340 panel. */
void vio_ui_canvas_size(enum display_orientation o, int *w, int *h);

/* Returns 1 once after a rotation change, so the next flush repaints the
 * whole panel (the old orientation's pixels are still on screen). */
int vio_display_take_redraw(void);

#endif /* HOMO_VIO_DISPLAY_H */
