#include "../../include/vqeaf/vm.h"
#include "../../include/vqeaf/memory.h"
static uint64_t *table(uint64_t physical){return (uint64_t*)(uintptr_t)physical;}
uint64_t vqeaf_vm_create_root(void){uint64_t p=vqeaf_pmm_alloc_page();if(!p)return 0;uint64_t*t=table(p);for(unsigned i=0;i<512;i++)t[i]=0;return p;}
static uint64_t *child(uint64_t *parent,unsigned index,uint64_t user){
 if(parent[index]&VQEAF_VM_PRESENT){parent[index]|=user;return table(parent[index]&0x000ffffffffff000ull);}
 {
  uint64_t p=vqeaf_pmm_alloc_page();if(!p)return 0;
  uint64_t*t=table(p);for(unsigned i=0;i<512;i++)t[i]=0;
  /* user access requires the USER bit at every paging level */
  parent[index]=p|VQEAF_VM_PRESENT|VQEAF_VM_WRITE|user;
 }
 return table(parent[index]&0x000ffffffffff000ull);
}
int vqeaf_vm_map(uint64_t root,uint64_t va,uint64_t pa,uint64_t flags){
 if(!root||(va&0xfff)||(pa&0xfff)||va>=0x0000800000000000ull)return -1;
 uint64_t user=flags&VQEAF_VM_USER;
 uint64_t *pml4=table(root),*pdpt=child(pml4,(va>>39)&511,user);if(!pdpt)return -2;
 uint64_t *pd=child(pdpt,(va>>30)&511,user);if(!pd)return -2;
 uint64_t *pt=child(pd,(va>>21)&511,user);if(!pt)return -2;
 unsigned i=(unsigned)((va>>12)&511);if(pt[i]&VQEAF_VM_PRESENT)return -3;
 pt[i]=(pa&0x000ffffffffff000ull)|flags|VQEAF_VM_PRESENT;return 0;
}
/* The UEFI identity map already covers MMIO ranges below 4 GB. For the GOP
 * framebuffer we need extra leaf flags (write + cache-disable) without
 * replacing an existing mapping, so -3 ("leaf already present") is allowed. */
int vqeaf_vm_map_or_update(uint64_t root,uint64_t va,uint64_t pa,uint64_t flags){
 int r=vqeaf_vm_map(root,va,pa,flags);
 if(r!=-3)return r;
 const uint64_t mask=0x000ffffffffff000ull;
 uint64_t *pml4=table(root);
 uint64_t *pdpt=table(pml4[(va>>39)&511]&mask);
 uint64_t *pd=table(pdpt[(va>>30)&511]&mask);
 uint64_t *pt=table(pd[(va>>21)&511]&mask);
 unsigned i=(unsigned)((va>>12)&511);
 if((pt[i]&mask)!=(pa&mask))return -4; /* never silently alias a different frame */
 pt[i]|=flags|VQEAF_VM_PRESENT;
 __asm__ volatile("invlpg (%0)"::"r"(va):"memory");
 return 0;
}
int vqeaf_vm_map_range_or_update(uint64_t root,uint64_t va,uint64_t pa,uint64_t pages,uint64_t flags){
 for(uint64_t i=0;i<pages;i++){
  int r=vqeaf_vm_map_or_update(root,va+i*VQEAF_PAGE_SIZE,pa+i*VQEAF_PAGE_SIZE,flags);
  if(r)return r;
 }
 return 0;
}
uint64_t vqeaf_vm_user_root(uint64_t kernel_root){
 if(!kernel_root)return 0;
 uint64_t p=vqeaf_pmm_alloc_page();if(!p)return 0;
 uint64_t *t=table(p);for(unsigned i=0;i<512;i++)t[i]=0;
 /* Share the kernel half (PML4 slot 0, identity-mapped low memory) and give
  * each task its own PML4 slot 16 for user mappings. */
 t[0]=table(kernel_root)[0];
 return p;
}
int vqeaf_vm_map_range(uint64_t root,uint64_t va,uint64_t pa,uint64_t pages,uint64_t flags){
 if((va&0xfff)||(pa&0xfff))return -1;
 for(uint64_t i=0;i<pages;i++){int r=vqeaf_vm_map(root,va+i*VQEAF_PAGE_SIZE,pa+i*VQEAF_PAGE_SIZE,flags);if(r)return r;}
 return 0;
}
int vqeaf_vm_unmap(uint64_t root,uint64_t va){
 if(!root||(va&0xfff)||va>=0x0000800000000000ull)return -1;
 uint64_t *pml4=table(root);
 if(!(pml4[(va>>39)&511]&VQEAF_VM_PRESENT))return -2;
 uint64_t *pdpt=table(pml4[(va>>39)&511]&0x000ffffffffff000ull);
 if(!(pdpt[(va>>30)&511]&VQEAF_VM_PRESENT))return -2;
 uint64_t *pd=table(pdpt[(va>>30)&511]&0x000ffffffffff000ull);
 if(!(pd[(va>>21)&511]&VQEAF_VM_PRESENT))return -2;
 uint64_t *pt=table(pd[(va>>21)&511]&0x000ffffffffff000ull);
 unsigned i=(unsigned)((va>>12)&511);
 if(!(pt[i]&VQEAF_VM_PRESENT))return -3;
 pt[i]=0;
 __asm__ volatile("invlpg (%0)"::"r"(va):"memory");
 return 0;
}
void vqeaf_vm_activate(uint64_t root){if(root)__asm__ volatile("mov %0,%%cr3"::"r"(root):"memory");}
uint64_t vqeaf_vm_current_root(void){uint64_t v;__asm__ volatile("mov %%cr3,%0":"=r"(v));return v&0x000ffffffffff000ull;}
