#include "../include/vqeaf/elf.h"
#include "../include/vqeaf/vm.h"
#include "../include/vqeaf/memory.h"
/* Target machine for the static ELF loader. Defaults to EM_X86_64 so the x86
 * UEFI kernel is unchanged; the AArch64 build defines VQEAF_ELF_MACHINE=0xB7. */
#ifndef VQEAF_ELF_MACHINE
#define VQEAF_ELF_MACHINE 0x3Eu
#endif
static uint16_t rd16(const uint8_t*p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t rd32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t rd64(const uint8_t*p){return (uint64_t)rd32(p)|((uint64_t)rd32(p+4)<<32);}
int vqeaf_elf_load(uint64_t root,const void *image,uint64_t image_size,uint64_t *entry_out){
 const uint8_t *b=image;
 if(!root||!b||!entry_out||image_size<64||((uintptr_t)b&7))return -1;
 /* ELF header */
 if(b[0]!=0x7F||b[1]!='E'||b[2]!='L'||b[3]!='F')return -2;
 if(b[4]!=2||b[5]!=1)return -3;              /* ELFCLASS64, little endian */
 if(rd16(b+16)!=2)return -4;                 /* ET_EXEC only (static) */
 if(rd16(b+18)!=VQEAF_ELF_MACHINE)return -5;   /* target machine (x86_64 / AArch64) */
 uint16_t phentsize=rd16(b+54);
 uint16_t phnum=rd16(b+56);
 if(!phnum||phnum>16||phentsize<56)return -6;
 if((uint64_t)phnum*phentsize+64>image_size)return -7;
 *entry_out=rd64(b+24);
 if(*entry_out<VQEAF_USER_REGION_BASE||*entry_out>=VQEAF_USER_REGION_LIMIT)return -8;
 for(unsigned i=0;i<phnum;i++){
  const uint8_t *ph=b+64+(uint64_t)i*phentsize;
  if(rd32(ph+0)!=1)continue;                 /* PT_LOAD only; others ignored */
  uint64_t offset=rd64(ph+8),vaddr=rd64(ph+16);
  uint64_t filesz=rd64(ph+32),memsz=rd64(ph+40);
  if(vaddr<VQEAF_USER_REGION_BASE||vaddr>=VQEAF_USER_REGION_LIMIT)return -9;
  if(memsz>VQEAF_ELF_USER_MAX_BYTES||filesz>memsz)return -10;
  if(offset>image_size||filesz>image_size-offset)return -11;
  uint64_t start=vaddr&~0xFFFull,end=vaddr+memsz;
  uint64_t pages=(end-start+VQEAF_PAGE_SIZE-1)/VQEAF_PAGE_SIZE;
  uint64_t phys=vqeaf_pmm_alloc_pages(pages);
  if(!phys)return -12;
  /* Map user-visible pages with USER|WRITE; the kernel still reaches every
   * frame through its identity-mapped low half. */
  if(vqeaf_vm_map_range(root,start,phys,pages,VQEAF_VM_WRITE|VQEAF_VM_USER)!=0)return -13;
  uint8_t *dst=(uint8_t*)phys+(vaddr-start); /* identity alias of the first frame + offset */
  const uint8_t *src=b+offset;
  for(uint64_t k=0;k<filesz;k++)dst[k]=src[k];
  for(uint64_t k=filesz;k<memsz;k++)dst[k]=0; /* BSS zero fill */
 }
 return 0;
}
