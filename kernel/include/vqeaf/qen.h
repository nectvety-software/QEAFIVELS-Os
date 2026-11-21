#ifndef VQEAF_QEN_H
#define VQEAF_QEN_H
#include <stdint.h>
/* Loader for the shipping .qen native module format (see sdk/compiler/qen.py)
 * on the independent kernel. Validates the 64-byte header + payload hash, maps
 * code|rodata|data|bss into a per-task user root at a caller-chosen base, and
 * applies aarch64 relocations from a table appended after the exports table.
 * The flat layout preserves link-time virtual addresses, so a relocation
 * offset is a byte offset into the loaded image. Position-independent modules
 * can therefore run at any base (the "load app code" step). */
#define VQEAF_QEN_MAX_BYTES (16u*1024u*1024u)
#define VQEAF_QEN_ARCH_ARM64 1u
#define VQEAF_QEN_R_AARCH64_RELATIVE 1027u
/* Load image into root at base (page-aligned, inside the user region). On
 * success *entry_out = base + entry_offset. Returns 0 or a negative error. */
int vqeaf_qen_load(uint64_t root,const void *image,uint64_t image_size,
                   uint64_t base,uint64_t *entry_out);
#endif