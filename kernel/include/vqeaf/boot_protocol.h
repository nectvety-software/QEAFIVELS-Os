#ifndef VQEAF_BOOT_PROTOCOL_H
#define VQEAF_BOOT_PROTOCOL_H
#include <stdint.h>
#define VQEAF_BOOT_MAGIC "VQEAFBT0"
#define VQEAF_BOOT_PROTOCOL_VERSION 3u
#define VQEAF_ARCH_X86_64 1u
#define VQEAF_ARCH_ARM64 2u
#define VQEAF_BOOT_MODE_NORMAL 0u
#define VQEAF_BOOT_MODE_RECOVERY 1u
/* VQEAF_FB_FORMAT_* mirror VQEAF_GFX_FMT_* in system/ui/include/vqeaf/gfx.h. */
#define VQEAF_FB_FORMAT_RGB 0u
#define VQEAF_FB_FORMAT_BGR 1u
#define VQEAF_FB_FORMAT_NONE 0xFFFFFFFFu
/* v2: memory-map handoff ABI (kept verbatim for the protocol record). */
typedef struct { char magic[8]; uint32_t version,size,architecture,boot_mode; uint64_t flags; void *system_table; void *image_handle; void *memory_map; uint64_t memory_map_size,memory_descriptor_size; uint32_t memory_descriptor_version,reserved; } VqeafBootInfoV2;
/* v3: adds the GOP linear framebuffer. The Boot Manager must fill these before
 * StartImage; the kernel may use the framebuffer after ExitBootServices without
 * ever calling a UEFI Boot Service again. framebuffer_size == 0 means "no
 * usable linear framebuffer" and the kernel falls back to serial-only output. */
typedef struct {
 char magic[8]; uint32_t version,size,architecture,boot_mode;
 uint64_t flags; void *system_table; void *image_handle;
 void *memory_map; uint64_t memory_map_size,memory_descriptor_size;
 uint32_t memory_descriptor_version,reserved;
 uint64_t framebuffer_base,framebuffer_size;
 uint32_t framebuffer_width,framebuffer_height,framebuffer_stride,framebuffer_format;
} VqeafBootInfoV3;
#endif
