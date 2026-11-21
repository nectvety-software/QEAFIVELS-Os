/* Dedicated "Installing System Update" renderer. Progress, the active stage
 * and availability are supplied by the caller; this file never invents
 * progress and never touches storage. */
#ifndef VQEAF_UPDATE_SCREEN_H
#define VQEAF_UPDATE_SCREEN_H
#include "gfx.h"

#define VQEAF_UPDATE_STAGE_COUNT 4u

typedef struct {
    uint32_t percent;      /* 0..100 completed work units */
    uint32_t stage;        /* active stage 0..3; 4 = all complete */
    const char *detail;    /* current stage text, may be NULL */
    int failed;            /* render the failure state */
    int available;         /* 0 = no update service/package: blocked */
} VqeafUpdateView;

void vqeaf_update_screen_render(const VqeafFramebuffer *fb, uint32_t percent,
                                const char *stage, int failed);
void vqeaf_update_screen_render_view(const VqeafFramebuffer *fb,
                                     const VqeafUpdateView *view);
const char *vqeaf_update_stage_label(uint32_t index);
#endif
