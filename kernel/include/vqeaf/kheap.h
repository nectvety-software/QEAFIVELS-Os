#ifndef VQEAF_KHEAP_H
#define VQEAF_KHEAP_H
#include <stdint.h>
/* Kernel heap: arena-based first-fit allocator with coalescing.
 * The arena is supplied by the caller so the same code is unit-testable on the
 * host (static buffer) and usable in the kernel (identity-mapped RAM pages). */
int vqeaf_kheap_init(void *base, uint64_t size);
void *vqeaf_kmalloc(uint64_t size);
void vqeaf_kfree(void *ptr);
typedef struct { uint64_t arena_size, free_bytes, largest_free, used_bytes, allocations, frees; } VqeafKHeapStats;
int vqeaf_kheap_stats(VqeafKHeapStats *out);
#endif
