#ifndef VQEAF_VM_H
#define VQEAF_VM_H
#include <stdint.h>
#define VQEAF_VM_PRESENT 1ull
#define VQEAF_VM_WRITE 2ull
#define VQEAF_VM_USER 4ull
#define VQEAF_VM_WRITE_THROUGH 8ull
#define VQEAF_VM_CACHE_DISABLE 16ull
#define VQEAF_VM_HUGE 128ull
#define VQEAF_VM_NO_EXECUTE (1ull<<63)
#define VQEAF_USER_REGION_BASE 0x10000000000ull /* PML4 slot 2: per-task user space (kernel keeps slot 0) */
#define VQEAF_USER_REGION_LIMIT 0x14000000000ull
uint64_t vqeaf_vm_create_root(void);
uint64_t vqeaf_vm_user_root(uint64_t kernel_root); /* new root sharing kernel space */
int vqeaf_vm_map(uint64_t root,uint64_t virtual_address,uint64_t physical_address,uint64_t flags);
int vqeaf_vm_map_or_update(uint64_t root,uint64_t virtual_address,uint64_t physical_address,uint64_t flags); /* -3 on vqeaf_vm_map is OK: OR flags into the existing leaf */
int vqeaf_vm_map_range(uint64_t root,uint64_t virtual_address,uint64_t physical_address,uint64_t pages,uint64_t flags);
int vqeaf_vm_map_range_or_update(uint64_t root,uint64_t virtual_address,uint64_t physical_address,uint64_t pages,uint64_t flags);
int vqeaf_vm_unmap(uint64_t root,uint64_t virtual_address); /* also issues invlpg */
void vqeaf_vm_activate(uint64_t root);
uint64_t vqeaf_vm_current_root(void);
#endif
