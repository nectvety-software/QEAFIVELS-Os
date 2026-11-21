/* MMIO helpers for PCI BARs on the identity-mapped x86_64 QEMU kernel. */
#ifndef VQEAF_MMIO_H
#define VQEAF_MMIO_H
#include <stdint.h>
#include "vm.h"
#include "memory.h"
static inline uint32_t vqeaf_mmio_r32(uint64_t base,uint32_t off){return *(volatile uint32_t*)(uintptr_t)(base+off);}
static inline void vqeaf_mmio_w32(uint64_t base,uint32_t off,uint32_t v){*(volatile uint32_t*)(uintptr_t)(base+off)=v;}
static inline uint16_t vqeaf_mmio_r16(uint64_t base,uint32_t off){return *(volatile uint16_t*)(uintptr_t)(base+off);}
static inline void vqeaf_mmio_w16(uint64_t base,uint32_t off,uint16_t v){*(volatile uint16_t*)(uintptr_t)(base+off)=v;}
static inline uint8_t vqeaf_mmio_r8(uint64_t base,uint32_t off){return *(volatile uint8_t*)(uintptr_t)(base+off);}
static inline void vqeaf_mmio_w8(uint64_t base,uint32_t off,uint8_t v){*(volatile uint8_t*)(uintptr_t)(base+off)=v;}
/* BARs are MMIO below 4 GB; make sure the active page table maps them with
 * write + cache-disable before any register access. */
static inline int vqeaf_mmio_map(uint64_t phys,uint64_t bytes){
 if(!phys||!bytes)return -1;
 uint64_t start=phys&~(VQEAF_PAGE_SIZE-1);
 uint64_t end=(phys+bytes+VQEAF_PAGE_SIZE-1)&~(VQEAF_PAGE_SIZE-1);
 if(end<=start||end>VQEAF_PHYS_LIMIT)return -2;
 return vqeaf_vm_map_range_or_update(vqeaf_vm_current_root(),start,start,
                                     (end-start)/VQEAF_PAGE_SIZE,
                                     VQEAF_VM_WRITE|VQEAF_VM_CACHE_DISABLE);
}
static inline uint64_t vqeaf_mmio_bar(uint32_t raw){
 if(raw&1u)return 0; /* I/O BAR, not MMIO */
 return (uint64_t)(raw&0xFFFFFFF0u);
}
#endif
