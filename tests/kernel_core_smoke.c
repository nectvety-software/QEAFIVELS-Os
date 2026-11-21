#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../kernel/include/vqeaf/memory.h"
#include "../kernel/include/vqeaf/scheduler.h"
#include "../kernel/include/vqeaf/kheap.h"
#include "../kernel/include/vqeaf/ipc.h"
#include "../kernel/include/vqeaf/vfs.h"
typedef struct { uint32_t type,pad;uint64_t physical_start,virtual_start,number_of_pages,attribute; } TestEfiDescriptor;
int main(void){
 TestEfiDescriptor map[2];memset(map,0,sizeof(map));map[0].type=7;map[0].physical_start=0x100000;map[0].number_of_pages=64;
 assert(vqeaf_pmm_init(map,sizeof(map),sizeof(map[0]))==0);assert(vqeaf_pmm_free_count()==64);
 uint64_t page=vqeaf_pmm_alloc_page();assert(page==0x100000);assert(vqeaf_pmm_free_count()==63);
 assert(vqeaf_pmm_free_page(page)==0);assert(vqeaf_pmm_free_page(page)==-2);assert(vqeaf_pmm_free_count()==64);
 vqeaf_sched_init();assert(vqeaf_sched_add(1)>=0);assert(vqeaf_sched_add(2)>=0);vqeaf_sched_tick();assert(vqeaf_sched_current()>=0);assert(vqeaf_timer_ticks()==1);
 VqeafIpcQueue q;VqeafMessage m,out;memset(&m,0,sizeof(m));vqeaf_ipc_init(&q);m.sender=7;m.type=3;m.size=4;memcpy(m.payload,"test",4);
 assert(vqeaf_ipc_receive(&q,&out)==-2);assert(vqeaf_ipc_send(&q,&m)==0);assert(vqeaf_ipc_receive(&q,&out)==0);assert(out.sender==7&&out.size==4&&memcmp(out.payload,"test",4)==0);
 for(unsigned i=0;i<VQEAF_IPC_QUEUE_CAPACITY;i++)assert(vqeaf_ipc_send(&q,&m)==0);assert(vqeaf_ipc_send(&q,&m)==-2);
 assert(vqeaf_vfs_validate_path("/system/bin")==0);assert(vqeaf_vfs_validate_path("relative")==-1);assert(vqeaf_vfs_validate_path("/a/../b")==-3);
 /* Kernel heap: basic alloc/free plus a fragmentation and coalescing check. */
 static uint8_t arena[64*1024] __attribute__((aligned(16)));
 assert(vqeaf_kheap_init(arena,sizeof(arena))==0);
 assert(vqeaf_kheap_init(0,1024)==-1);assert(vqeaf_kheap_init(arena,8)==-1);
 VqeafKHeapStats st;assert(vqeaf_kheap_stats(&st)==0);assert(st.free_bytes+st.used_bytes+32==sizeof(arena));
 void *a=vqeaf_kmalloc(100),*b=vqeaf_kmalloc(200),*c=vqeaf_kmalloc(300);
 assert(a&&b&&c);assert(((uintptr_t)a&15u)==0);assert(vqeaf_kmalloc(0)==0);
 /* Fill the rest so fragmentation behavior is observable. */
 void *fill[128];unsigned fills=0;
 for(unsigned i=0;i<128;i++){fill[i]=vqeaf_kmalloc(512);if(!fill[i])break;fills++;}
 assert(fills>0);
 assert(vqeaf_kheap_stats(&st)==0);assert(st.largest_free<4096);
 /* Free non-adjacent blocks: heap must stay fragmented (small largest-free). */
 vqeaf_kfree(a);vqeaf_kfree(c);assert(vqeaf_kheap_stats(&st)==0);
 assert(st.largest_free<512); /* holes cannot satisfy a 512 request: still fragmented */
 /* Allocate sizes that cannot fit any hole: must fail cleanly. */
 assert(vqeaf_kmalloc(sizeof(arena))==0);
 /* Free b: the three holes coalesce into one large free block. */
 vqeaf_kfree(b);assert(vqeaf_kheap_stats(&st)==0);
 assert(st.largest_free>=(100+200+300));
 void *d=vqeaf_kmalloc(100+200+300);assert(d);assert(d==a); /* first-fit reuses the hole */
 for(unsigned i=0;i<fills;i++)vqeaf_kfree(fill[i]);
 vqeaf_kfree(d);assert(vqeaf_kheap_stats(&st)==0);
 assert(st.allocations==st.frees); /* every allocation has been freed */
 return 0;
}
