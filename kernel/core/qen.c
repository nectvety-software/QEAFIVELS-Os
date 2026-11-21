#include "../include/vqeaf/qen.h"
#include "../include/vqeaf/vm.h"
#include "../include/vqeaf/memory.h"
/* .qen native module loader for the independent kernel. The header layout is
 * the single source of truth in sdk/compiler/qen.py; this mirrors it exactly.
 * Nothing here executes module code: it only validates, maps and relocates,
 * then hands the entry address back for the scheduler to run at EL0. */
static uint16_t rd16(const uint8_t*p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t rd32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t rd64(const uint8_t*p){return (uint64_t)rd32(p)|((uint64_t)rd32(p+4)<<32);}
static void wr64(uint8_t*p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t fnv1a32(const uint8_t*p,uint64_t n){
 uint32_t h=0x811C9DC5u;
 for(uint64_t i=0;i<n;i++){h^=p[i];h*=0x01000193u;}
 return h;
}
int vqeaf_qen_load(uint64_t root,const void *image,uint64_t image_size,
                   uint64_t base,uint64_t *entry_out){
 const uint8_t *b=image;
 if(!root||!b||!entry_out||image_size<64||((uintptr_t)b&7))return -1;
 if(base&0xFFFull)return -2;
 if(base<VQEAF_USER_REGION_BASE||base>=VQEAF_USER_REGION_LIMIT)return -3;
 /* Header (little-endian, 64 bytes). */
 static const char magic[9]={'Q','E','A','F','-','Q','E','N','1'};
 for(unsigned i=0;i<9;i++)if(b[i]!=(uint8_t)magic[i])return -4;
 if(b[9]!=1)return -5;                       /* module_version */
 uint8_t abi=b[10],arch=b[11],flags=b[12];
 if(abi!=2&&abi!=3&&abi!=4)return -6;
 if(arch!=VQEAF_QEN_ARCH_ARM64)return -7;
 if(flags!=0)return -8;
 if(rd16(b+13)!=64)return -9;                /* header_size */
 if(rd16(b+15)>16)return -10;                /* section_count */
 if(rd32(b+17)!=image_size)return -11;       /* module_size */
 for(unsigned i=55;i<64;i++)if(b[i])return -12; /* reserved */
 uint64_t code=rd32(b+21),rodata=rd32(b+25),data=rd32(b+29),bss=rd32(b+33);
 uint64_t entry=rd32(b+37),imports=rd32(b+41),exports=rd32(b+45);
 uint32_t hash=rd32(b+49);
 uint64_t payload=image_size-64;
 if(code+rodata+data+imports+exports>payload)return -13;
 if(code&&entry>=code)return -14;
 if(fnv1a32(b+64,payload)!=hash)return -15;
 uint64_t total=code+rodata+data+bss;
 if(total>VQEAF_QEN_MAX_BYTES||total==0)return -16;
 if(base+total>VQEAF_USER_REGION_LIMIT)return -17;
 uint64_t pages=(total+VQEAF_PAGE_SIZE-1)/VQEAF_PAGE_SIZE;
 uint64_t phys=vqeaf_pmm_alloc_pages(pages);
 if(!phys)return -18;
 if(vqeaf_vm_map_range(root,base,phys,pages,VQEAF_VM_WRITE|VQEAF_VM_USER)!=0)return -19;
 uint8_t *dst=(uint8_t*)phys;               /* identity alias of the frames */
 const uint8_t *src=b+64;
 uint64_t flat=code+rodata+data;
 for(uint64_t k=0;k<flat;k++)dst[k]=src[k];
 for(uint64_t k=flat;k<total;k++)dst[k]=0;  /* BSS zero fill */
 /* Relocation table appended after imports+exports: u32 count, then count
  * records of (u32 offset, u32 type, u64 addend). RELA carries an explicit
  * addend, so the fixup is base + addend (not the section contents). Only
  * R_AARCH64_RELATIVE is supported. */
 uint64_t roff=64+flat+imports+exports;
 if(roff+4<=image_size){
  uint32_t count=rd32(b+roff);
  if(count>(image_size-roff-4)/16)return -20;
  for(uint32_t i=0;i<count;i++){
   uint64_t rec=roff+4+(uint64_t)i*16;
   uint64_t off=rd32(b+rec),type=rd32(b+rec+4),addend=rd64(b+rec+8);
   if(type!=VQEAF_QEN_R_AARCH64_RELATIVE)return -21;
   if(off+8>flat)return -22;
   wr64(dst+off,base+addend);
  }
 }
 *entry_out=base+entry;
 return 0;
}