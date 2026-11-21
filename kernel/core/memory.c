#include "../include/vqeaf/memory.h"
#define FRAME_COUNT (VQEAF_PHYS_LIMIT/VQEAF_PAGE_SIZE)
#define BITMAP_BYTES (FRAME_COUNT/8u)
static uint8_t used[BITMAP_BYTES],managed[BITMAP_BYTES];
static uint64_t free_pages;
typedef struct { uint32_t type,pad; uint64_t physical_start,virtual_start,number_of_pages,attribute; } EfiMemoryDescriptor;
static void bit_set(uint8_t*b,uint64_t f,int v){uint8_t m=(uint8_t)(1u<<(f&7u));if(v)b[f>>3]|=m;else b[f>>3]&=(uint8_t)~m;}
static int bit_get(const uint8_t*b,uint64_t f){return (b[f>>3]>>(f&7u))&1u;}
int vqeaf_pmm_init(const void *map,uint64_t size,uint64_t ds){
 if(!map||ds<sizeof(EfiMemoryDescriptor)||size%ds)return -1;
 for(uint64_t i=0;i<BITMAP_BYTES;i++){used[i]=0xff;managed[i]=0;}
 free_pages=0;const uint8_t*p=(const uint8_t*)map;
 for(uint64_t off=0;off<size;off+=ds){
  const EfiMemoryDescriptor*d=(const EfiMemoryDescriptor*)(p+off);
  if(d->type!=7u||!d->number_of_pages)continue;
  uint64_t first=(d->physical_start+VQEAF_PAGE_SIZE-1)/VQEAF_PAGE_SIZE;
  uint64_t end=d->physical_start/VQEAF_PAGE_SIZE+d->number_of_pages;
  if(end>FRAME_COUNT)end=FRAME_COUNT;
  if(first<256)first=256;
  for(uint64_t f=first;f<end;f++){if(!bit_get(managed,f)){bit_set(managed,f,1);bit_set(used,f,0);free_pages++;}}
 }
 return free_pages?0:-2;
}
uint64_t vqeaf_pmm_alloc_page(void){for(uint64_t f=256;f<FRAME_COUNT;f++)if(bit_get(managed,f)&&!bit_get(used,f)){bit_set(used,f,1);free_pages--;return f*VQEAF_PAGE_SIZE;}return 0;}
uint64_t vqeaf_pmm_alloc_pages(uint64_t count){
 if(!count)return 0;
 for(uint64_t f=256;f+count<=FRAME_COUNT;f++){
  if(!bit_get(managed,f)||bit_get(used,f))continue;
  uint64_t run=1;
  while(run<count&&f+run<FRAME_COUNT&&bit_get(managed,f+run)&&!bit_get(used,f+run))run++;
  if(run<count){f+=run;continue;}
  for(uint64_t k=0;k<count;k++){bit_set(used,f+k,1);free_pages--;}
  return f*VQEAF_PAGE_SIZE;
 }
 return 0;
}
int vqeaf_pmm_free_page(uint64_t a){if((a&(VQEAF_PAGE_SIZE-1))||a<VQEAF_PAGE_SIZE*256||a>=VQEAF_PHYS_LIMIT)return -1;uint64_t f=a/VQEAF_PAGE_SIZE;if(!bit_get(managed,f)||!bit_get(used,f))return -2;bit_set(used,f,0);free_pages++;return 0;}
uint64_t vqeaf_pmm_free_count(void){return free_pages;}
