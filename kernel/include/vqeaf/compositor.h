#ifndef VQEAF_COMPOSITOR_H
#define VQEAF_COMPOSITOR_H
#include <stdint.h>
/* Minimal SurfaceFlinger-like compositor. EL0 clients submit surfaces via
 * sys_submit_surface; a kernel compositor task blits them into the ramfb
 * framebuffer and reads back pixels. EMULATED QEMU virt, not violet hardware. */
void vqeaf_compositor_init(void);
/* Queue a surface copied from a validated EL0 buffer. 0 on success. */
int vqeaf_compositor_submit(const void *user_surface, uint64_t len);
/* Kernel task entry: drain the surface queue, compose, read back pixels. */
void vqeaf_compositor_task(void *arg);
#endif