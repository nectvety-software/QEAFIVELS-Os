/* Shipped display default for the VQEAF reference OS: portrait 9:16.
 *
 * UEFI firmware usually boots a landscape desktop mode, so the Boot Manager
 * and the Recovery environment both ask this module for the richest exact
 * 9:16 framebuffer the platform can provide:
 *   1. an exact 9:16 portrait GOP mode (1080x1920, then 720x1280, then any);
 *   2. otherwise a custom Bochs/QEMU VBE mode 720x1280x32 on the linear
 *      framebuffer (QEMU std VGA / virtio-vga VGA compatibility);
 *   3. otherwise the current GOP mode unchanged, and the shell draws a
 *      centred 9:16 column inside it.
 * No mode is ever invented: geometry is read back from the hardware. */
#ifndef VQEAF_DISPLAY_MODE_H
#define VQEAF_DISPLAY_MODE_H
#include "efi_minimal.h"

/* Mirror VQEAF_FB_FORMAT_* / VQEAF_GFX_FMT_* (0 = R,G,B bytes; 1 = B,G,R). */
#define VQEAF_DISPLAY_RGB 0u
#define VQEAF_DISPLAY_BGR 1u

typedef struct {
    uint64_t base;
    uint64_t bytes;
    uint32_t width, height, stride, format;
    int portrait; /* framebuffer is exactly 9:16 portrait */
    int custom;   /* Bochs VBE registers programmed by us */
} VqeafDisplayChoice;

/* Fills *out and logs the decision on COM1. Returns 0 when a framebuffer is
 * usable, -1 when the platform exposes no linear framebuffer at all. */
int vqeaf_display_choose_portrait(EFI_SYSTEM_TABLE *st,
                                  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
                                  VqeafDisplayChoice *out);

#endif
