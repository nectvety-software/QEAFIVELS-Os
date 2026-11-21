#ifndef VQEAF_MEMORY_H
#define VQEAF_MEMORY_H
#include <stdint.h>
#define VQEAF_PAGE_SIZE 4096ull
#define VQEAF_PHYS_LIMIT (4ull*1024ull*1024ull*1024ull)
int vqeaf_pmm_init(const void *map,uint64_t map_size,uint64_t descriptor_size);
uint64_t vqeaf_pmm_alloc_page(void);
uint64_t vqeaf_pmm_alloc_pages(uint64_t count); /* contiguous run, 0 on failure */
int vqeaf_pmm_free_page(uint64_t address);
uint64_t vqeaf_pmm_free_count(void);
#endif
