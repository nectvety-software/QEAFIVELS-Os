#ifndef VQEAF_ELF_H
#define VQEAF_ELF_H
#include <stdint.h>
/* Static ELF64 executable loader for ring-3 tasks. Only ET_EXEC with PT_LOAD
 * segments inside the user region is accepted; dynamic, relocatable, 32-bit
 * and out-of-range images are rejected explicitly. */
#define VQEAF_ELF_USER_MAX_BYTES (16u*1024u*1024u)
int vqeaf_elf_load(uint64_t root,const void *image,uint64_t image_size,uint64_t *entry_out);
#endif
