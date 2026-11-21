#ifndef VQEAF_DISPLAY_AARCH64_H
#define VQEAF_DISPLAY_AARCH64_H
#include <stdint.h>
/* Minimal QEMU virt display path: fw_cfg MMIO (0x09020000) + ramfb standalone
 * framebuffer. The compositor owns a guest-RAM framebuffer; ramfb tells QEMU to
 * scan it out. EMULATED evidence only (QEMU virt), not physical violet hardware. */
/* F3 needs a canvas big enough for real overlapping windows: 320x240
 * XRGB8888 (300 KiB). QEMU ramfb scans this guest-RAM framebuffer out. */
#define VQEAF_FB_WIDTH 320u
#define VQEAF_FB_HEIGHT 240u
#define VQEAF_FB_BPP 4u
#define VQEAF_FB_BYTES (VQEAF_FB_WIDTH * VQEAF_FB_HEIGHT * VQEAF_FB_BPP)
/* Bring up fw_cfg + locate the ramfb cfg file. Returns 0 on success. */
int vqeaf_display_init(void);
/* Point ramfb at a guest-physical framebuffer (XRGB8888). Returns 0 on success. */
int vqeaf_ramfb_set(uint64_t fb_pa, uint32_t width, uint32_t height, uint32_t stride);
/* Physical address of the compositor framebuffer (0 if not allocated). */
uint64_t vqeaf_display_fb_pa(void);
#endif