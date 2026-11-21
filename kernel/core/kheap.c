#include "../include/vqeaf/kheap.h"
/* Free-list allocator. Each block carries a 32-byte header; user data is
 * 16-byte aligned. Free blocks are coalesced with the following block on free
 * and with the preceding block by walking the singly linked list. */
#define VQEAF_KHEAP_MAGIC 0x4B485031u /* KHP1 */
#define VQEAF_KHEAP_ALIGN 16u
#define VQEAF_KHEAP_MIN_SPLIT 32u
typedef struct Block { uint64_t size; uint32_t used, magic; struct Block *next; uint64_t pad; } Block;
static Block *head;
static uint64_t arena_size;
static uint64_t allocations, frees;
static uint64_t used_total(void){
  uint64_t used=0;
  for(Block *b=head;b;b=b->next)if(b->used)used+=b->size;
  return used;
}
int vqeaf_kheap_init(void *base,uint64_t size){
  if(!base||size<sizeof(Block)+VQEAF_KHEAP_ALIGN||((uintptr_t)base&(VQEAF_KHEAP_ALIGN-1)))return -1;
  head=(Block*)base;head->size=size-sizeof(Block);head->used=0;head->magic=VQEAF_KHEAP_MAGIC;head->next=0;head->pad=0;
  arena_size=size;allocations=0;frees=0;
  return 0;
}
void *vqeaf_kmalloc(uint64_t size){
  if(!head||!size)return 0;
  uint64_t need=(size+(VQEAF_KHEAP_ALIGN-1))&~(uint64_t)(VQEAF_KHEAP_ALIGN-1);
  for(Block *b=head;b;b=b->next){
    if(b->used||b->magic!=VQEAF_KHEAP_MAGIC||b->size<need)continue;
    if(b->size>=need+sizeof(Block)+VQEAF_KHEAP_MIN_SPLIT){
      Block *rest=(Block*)((uint8_t*)b+sizeof(Block)+need);
      rest->size=b->size-need-sizeof(Block);rest->used=0;rest->magic=VQEAF_KHEAP_MAGIC;rest->next=b->next;rest->pad=0;
      b->next=rest;b->size=need;
    }
    b->used=1;allocations++;return (uint8_t*)b+sizeof(Block);
  }
  return 0;
}
void vqeaf_kfree(void *ptr){
  if(!ptr||!head)return;
  Block *b=(Block*)((uint8_t*)ptr-sizeof(Block));
  if(b->magic!=VQEAF_KHEAP_MAGIC||!b->used)return;
  b->used=0;frees++;
  if(b->next&&!b->next->used&&b->next->magic==VQEAF_KHEAP_MAGIC){
    Block *n=b->next;b->size+=sizeof(Block)+n->size;b->next=n->next;
  }
  Block *prev=0;
  for(Block *p=head;p&&p!=b;p=p->next)prev=p;
  if(prev&&!prev->used&&prev->magic==VQEAF_KHEAP_MAGIC){
    prev->size+=sizeof(Block)+b->size;prev->next=b->next;
  }
}
int vqeaf_kheap_stats(VqeafKHeapStats *out){
  if(!head||!out)return -1;
  out->arena_size=arena_size;out->used_bytes=used_total();out->free_bytes=arena_size-out->used_bytes-sizeof(Block);
  out->largest_free=0;out->allocations=allocations;out->frees=frees;
  for(Block *b=head;b;b=b->next)if(!b->used&&b->size>out->largest_free)out->largest_free=b->size;
  return 0;
}
