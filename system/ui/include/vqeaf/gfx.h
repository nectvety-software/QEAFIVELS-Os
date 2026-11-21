/* Framebuffer primitives shared by the UEFI Boot Manager (before
 * ExitBootServices) and the native kernel UI (after it). Freestanding: no
 * libc, no UEFI calls, no allocations. All coordinates are clipped. */
#ifndef VQEAF_GFX_H
#define VQEAF_GFX_H
#include <stdint.h>
#define VQEAF_GFX_FMT_RGB 0u /* byte order R,G,B,reserved -> 0xXXBBGGRR */
#define VQEAF_GFX_FMT_BGR 1u /* byte order B,G,R,reserved -> 0xXXRRGGBB */
typedef struct {
 volatile uint32_t *pixels;
 uint32_t width,height,stride; /* stride is measured in 32-bit pixels */
 uint32_t format;
} VqeafFramebuffer;
int vqeaf_gfx_setup(VqeafFramebuffer *fb,uint64_t physical_base,uint32_t width,
                    uint32_t height,uint32_t stride,uint32_t pixel_format);
uint32_t vqeaf_gfx_rgb(const VqeafFramebuffer *fb,uint32_t rgb);
void vqeaf_gfx_pixel(const VqeafFramebuffer *fb,uint32_t x,uint32_t y,uint32_t color);
void vqeaf_gfx_clear(const VqeafFramebuffer *fb,uint32_t color);
void vqeaf_gfx_rect(const VqeafFramebuffer *fb,uint32_t x,uint32_t y,
                    uint32_t w,uint32_t h,uint32_t color);
void vqeaf_gfx_border(const VqeafFramebuffer *fb,uint32_t x,uint32_t y,
                      uint32_t w,uint32_t h,uint32_t thickness,uint32_t color);
void vqeaf_gfx_line(const VqeafFramebuffer *fb,uint32_t x0,uint32_t y0,
                    uint32_t x1,uint32_t y1,uint32_t color);
void vqeaf_gfx_circle(const VqeafFramebuffer *fb,uint32_t cx,uint32_t cy,
                      uint32_t radius,uint32_t thickness,uint32_t color);
void vqeaf_gfx_disc(const VqeafFramebuffer *fb,uint32_t cx,uint32_t cy,
                    uint32_t radius,uint32_t color);
/* Alpha-blends `color` over an already packed destination pixel. */
uint32_t vqeaf_gfx_blend(const VqeafFramebuffer *fb,uint32_t dst,uint32_t color,
                         uint32_t alpha);
/* Filled rectangle with circular corners (clamped to half the box). */
void vqeaf_gfx_round_rect(const VqeafFramebuffer *fb,uint32_t x,uint32_t y,
                          uint32_t w,uint32_t h,uint32_t radius,uint32_t color);
uint32_t vqeaf_gfx_text_width(const char *text,uint32_t scale);
void vqeaf_gfx_text(const VqeafFramebuffer *fb,uint32_t x,uint32_t y,
                    const char *text,uint32_t scale,uint32_t color);
void vqeaf_gfx_text_centered(const VqeafFramebuffer *fb,uint32_t y,
                             const char *text,uint32_t scale,uint32_t color);
void vqeaf_gfx_progress(const VqeafFramebuffer *fb,uint32_t x,uint32_t y,
                        uint32_t w,uint32_t h,uint32_t percent,
                        uint32_t fill,uint32_t track);
#endif
