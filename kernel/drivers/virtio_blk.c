/* QEMU VirtIO 0.9 (legacy PCI) block driver -- see kernel/include/vqeaf/virtio_blk.h
 * Device: PCI 1AF4:1001, I/O BAR0, INTx, one virtqueue.
 * Verified layout against QEMU hw/virtio/virtio.c:
 *   desc  = queue_base
 *   avail = desc + num*16
 *   used  = align_up(avail + 4 + 2*num, 4096)
 * Queue Address register takes queue_base >> 12.
 */
#include <stdint.h>
#include <stddef.h>
#include "../include/vqeaf/virtio_blk.h"
#include "../include/vqeaf/pci.h"
#include "../include/vqeaf/serial.h"
#include "../include/vqeaf/memory.h"
#include "../include/vqeaf/arch_x86_64.h"
#include "../include/vqeaf/scheduler.h"
#include "../../system/core/include/vqeaf/block.h"
#include "../../system/core/include/vqeaf/driver.h"
#include "../../system/core/include/vqeaf/capability.h"
#define barrier() __asm__ volatile("" ::: "memory")
/* Legacy virtio-pci register offsets (I/O BAR0). */
#define REG_DEVICE_FEATURES 0x00u
#define REG_GUEST_FEATURES  0x04u
#define REG_QUEUE_PFN       0x08u
#define REG_QUEUE_NUM       0x0Cu
#define REG_QUEUE_SEL       0x0Eu
#define REG_QUEUE_NOTIFY    0x10u
#define REG_STATUS          0x12u
#define REG_ISR             0x13u
#define REG_CONFIG          0x14u /* no MSI-X: config space starts at 0x14 */
#define STATUS_ACK          0x01u
#define STATUS_DRIVER       0x02u
#define STATUS_DRIVER_OK    0x04u
#define DESC_F_NEXT         0x1u
#define DESC_F_WRITE        0x2u
#define BLK_T_IN            0u  /* read  */
#define BLK_T_OUT           1u  /* write */
#define BLK_S_OK            0u
#define BLK_S_IOERR         1u
#define BLK_S_UNSUPP        2u
#define VBLK_SLOTS          4u
#define VBLK_SLOT_STRIDE    32u
#define VBLK_QUEUE_MAX      256u /* QEMU virtio-blk defaults to 256 descriptors */
#define VBLK_QUEUE_PAGES    4u   /* desc+avail (2 pages) + used ring, 4096-aligned */
#define VBLK_TIMEOUT_TICKS  300u /* 3 s at 100 Hz */
struct vring_desc { uint64_t addr; uint32_t len; uint16_t flags,next; };
struct vring_avail { uint16_t flags,idx; uint16_t ring[]; };
struct vring_used_elem { uint32_t id,len; };
struct vring_used { uint16_t flags,idx; struct vring_used_elem ring[]; };
struct blk_req { uint32_t type,ioprio; uint64_t sector; };
typedef struct { volatile int done; int status; } sync_wait_t;
typedef struct {
 int busy;
 vqeaf_virtio_done_fn done;
 void *ctx;
 uint8_t *status_ptr;
} slot_t;
extern void vqeaf_virtio_irq_stub(void);
static uint16_t iobase;
static uint8_t irq_line;
static uint64_t capacity;
static uint32_t block_id,instance_id,irq_count,completion_count;
static int ready;
static uint64_t queue_phys,req_phys;
static struct vring_desc *desc;
static struct vring_avail *avail;
static struct vring_used *used;
static uint16_t qnum,used_cons,avail_idx;
static slot_t slots[VBLK_SLOTS];
static uint8_t bad_scratch[VQEAF_VBLK_SECTOR_SIZE];
static uint8_t dual_a[VQEAF_VBLK_SECTOR_SIZE],dual_b[VQEAF_VBLK_SECTOR_SIZE];
static sync_wait_t dual_wa,dual_wb;
static struct VqeafBlockDevice block_dev;
static inline void outb(uint16_t p,uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline void outw(uint16_t p,uint16_t v){__asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p));}
static inline void outl(uint16_t p,uint32_t v){__asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p));}
static inline uint8_t inb(uint16_t p){uint8_t v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline uint16_t inw(uint16_t p){uint16_t v;__asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p));return v;}
static inline uint32_t inl(uint16_t p){uint32_t v;__asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p));return v;}
static inline uint64_t save_cli(void){uint64_t f;__asm__ volatile("pushfq; popq %0; cli":"=r"(f)::"memory");return f;}
static inline void restore_flags(uint64_t f){__asm__ volatile("pushq %0; popfq"::"r"(f):"memory","cc");}
static void serial_hex(uint64_t v){
 static const char h[]="0123456789ABCDEF";
 vqeaf_serial_write("0x");for(int s=60;s>=0;s-=4)vqeaf_serial_char(h[(v>>s)&15]);
}
static void sync_cb(int status,void *ctx){sync_wait_t *w=(sync_wait_t*)ctx;w->status=status;w->done=1;}
static int wait_for(sync_wait_t *w){
 uint64_t deadline=vqeaf_timer_ticks()+VBLK_TIMEOUT_TICKS;
 while(!w->done){
  __asm__ volatile("sti; hlt");
  if(vqeaf_timer_ticks()>deadline)return -7; /* timeout, no completion */
 }
 return w->status;
}
/* Submit one request. The caller's buffer must stay alive and be identity
 * mapped until the completion callback runs. */
static int submit_type(uint32_t type,uint64_t lba,uint32_t sectors,void *buffer,
                       vqeaf_virtio_done_fn done,void *ctx){
 if(!ready||!buffer||!sectors||sectors>VQEAF_VBLK_MAX_SECTORS)return -1;
 if(lba>capacity||capacity-lba<sectors)return -2;
 uint64_t buf_phys=(uint64_t)(uintptr_t)buffer;
 if(buf_phys+((uint64_t)sectors*VQEAF_VBLK_SECTOR_SIZE)>VQEAF_PHYS_LIMIT)return -3;
 uint64_t flags=save_cli();
 int slot=-1;
 for(uint32_t i=0;i<VBLK_SLOTS;i++)if(!slots[i].busy){slot=(int)i;break;}
 if(slot<0){restore_flags(flags);return -4;}
 slot_t *s=&slots[slot];
 s->busy=1;s->done=done;s->ctx=ctx;
 struct blk_req *r=(struct blk_req*)(uintptr_t)(req_phys+(uint64_t)slot*VBLK_SLOT_STRIDE);
 uint8_t *status=(uint8_t*)(uintptr_t)(req_phys+(uint64_t)slot*VBLK_SLOT_STRIDE+16u);
 r->type=type;r->ioprio=0;r->sector=lba;*status=0xFFu;
 s->status_ptr=status;
 unsigned h=(unsigned)slot*3u;
 desc[h].addr=(uint64_t)(uintptr_t)r;desc[h].len=sizeof(struct blk_req);
 desc[h].flags=DESC_F_NEXT;desc[h].next=(uint16_t)(h+1u);
 desc[h+1u].addr=buf_phys;desc[h+1u].len=sectors*VQEAF_VBLK_SECTOR_SIZE;
 desc[h+1u].flags=DESC_F_NEXT|(type==BLK_T_IN?DESC_F_WRITE:0u);
 desc[h+1u].next=(uint16_t)(h+2u);
 desc[h+2u].addr=(uint64_t)(uintptr_t)status;desc[h+2u].len=1u;
 desc[h+2u].flags=DESC_F_WRITE;desc[h+2u].next=0;
 barrier();
 avail->ring[avail_idx%qnum]=(uint16_t)h;
 barrier();
 avail->idx=++avail_idx;
 barrier();
 outw(iobase+REG_QUEUE_NOTIFY,0u); /* kick queue 0 */
 restore_flags(flags);
 return 0;
}
int vqeaf_virtio_blk_submit(uint64_t lba,uint32_t sectors,void *buffer,int write,
                            vqeaf_virtio_done_fn done,void *ctx){
 return submit_type(write?BLK_T_OUT:BLK_T_IN,lba,sectors,buffer,done,ctx);
}
int vqeaf_virtio_blk_read_sync(uint64_t lba,uint32_t sectors,void *buffer){
 sync_wait_t w;w.done=0;w.status=0;
 int r=submit_type(BLK_T_IN,lba,sectors,buffer,sync_cb,&w);
 if(r)return r;
 return wait_for(&w);
}
int vqeaf_virtio_blk_write_sync(uint64_t lba,uint32_t sectors,const void *buffer){
 sync_wait_t w;w.done=0;w.status=0;
 int r=submit_type(BLK_T_OUT,lba,sectors,(void*)buffer,sync_cb,&w);
 if(r)return r;
 return wait_for(&w);
}
int vqeaf_virtio_blk_concurrent_self_test(void){
 if(!ready||capacity<2u)return -1;
 dual_wa.done=0;dual_wa.status=0;dual_wb.done=0;dual_wb.status=0;
 int a=submit_type(BLK_T_IN,0,1,dual_a,sync_cb,&dual_wa);
 int b=submit_type(BLK_T_IN,1,1,dual_b,sync_cb,&dual_wb);
 if(a||b)return -2;
 uint64_t deadline=vqeaf_timer_ticks()+VBLK_TIMEOUT_TICKS;
 while(!dual_wa.done||!dual_wb.done){
  __asm__ volatile("sti; hlt");
  if(vqeaf_timer_ticks()>deadline)return -3;
 }
 return dual_wa.status|dual_wb.status;
}
int vqeaf_virtio_blk_error_self_test(void){
 if(!ready)return -1;
 sync_wait_t w;w.done=0;w.status=0;
 int r=submit_type(0xFFu,0,1,bad_scratch,sync_cb,&w); /* unsupported command */
 if(r)return r;
 r=wait_for(&w);
 return r==-6?0:-2; /* -6 == VIRTIO_BLK_S_UNSUPP */
}
/* Called from switch.S with interrupts disabled; returns the same frame so
 * the interrupted context resumes. Each used-ring element wakes one slot. */
uint64_t vqeaf_virtio_blk_isr(uint64_t frame_base){
 irq_count++;
 uint8_t isr=inb(iobase+REG_ISR); /* read clears the INTx reason */
 if(isr&1u){
  barrier();
  while(used_cons!=used->idx){
   struct vring_used_elem *e=&used->ring[used_cons%qnum];
   uint32_t id=e->id,slot=id/3u;
   if(slot<VBLK_SLOTS&&(uint32_t)(slot*3u)==id){
    slot_t *s=&slots[slot];
    if(s->busy){
     uint8_t st=*s->status_ptr;
     int status=st==BLK_S_OK?0:(st==BLK_S_IOERR?-5:(st==BLK_S_UNSUPP?-6:-7));
     s->busy=0;completion_count++;
     if(s->done)s->done(status,s->ctx);
    }
   }
   used_cons++;
  }
 }
 vqeaf_arch_irq_eoi(irq_line);
 return frame_base;
}
static int block_read_cb(struct VqeafBlockDevice *dev,uint64_t lba,uint32_t count,void *out){
 (void)dev;
 uint8_t *p=(uint8_t*)out;
 while(count){
  uint32_t chunk=count>VQEAF_VBLK_MAX_SECTORS?VQEAF_VBLK_MAX_SECTORS:count;
  int r=vqeaf_virtio_blk_read_sync(lba,chunk,p);
  if(r)return r;
  lba+=chunk;count-=chunk;p+=chunk*VQEAF_VBLK_SECTOR_SIZE;
 }
 return 0;
}
static int driver_probe(void *device){(void)device;return ready?0:-1;}
static int driver_remove(void *device){(void)device;return 0;}
static const VqeafDriver vblk_driver={
 VQEAF_DRIVER_ABI,sizeof(VqeafDriver),"virtio-blk-pci",VQEAF_DEV_STORAGE,
 driver_probe,driver_remove
};
int vqeaf_virtio_blk_init(void){
 VqeafPciDevice pci;
 if(vqeaf_pci_find(VQEAF_PCI_VENDOR_RED_HAT,VQEAF_PCI_DEV_VIRTIO_BLK_LEGACY,&pci)!=0){
  vqeaf_serial_write("VIRTIO: no legacy virtio-blk PCI device (1AF4:1001)\r\n");
  return -1;
 }
 if(!(pci.bar0&1u)||!pci.irq){
  vqeaf_serial_write("VIRTIO: BAR0 is not I/O or no INTx line\r\n");return -2;
 }
 iobase=(uint16_t)(pci.bar0&0xFFFCu);irq_line=pci.irq;
 uint16_t cmd=vqeaf_pci_read16(pci.bus,pci.slot,pci.func,0x04u);
 cmd=(uint16_t)((cmd|0x0007u)&~0x0400u); /* I/O + memory + bus master, INTx on */
 vqeaf_pci_write16(pci.bus,pci.slot,pci.func,0x04u,cmd);
 /* Reset, acknowledge, negotiate no optional features, go live. */
 outb(iobase+REG_STATUS,0u);
 outb(iobase+REG_STATUS,STATUS_ACK);
 outb(iobase+REG_STATUS,(uint8_t)(STATUS_ACK|STATUS_DRIVER));
 uint32_t features=inl(iobase+REG_DEVICE_FEATURES);
 outl(iobase+REG_GUEST_FEATURES,0u);
 outb(iobase+REG_STATUS,(uint8_t)(STATUS_ACK|STATUS_DRIVER|STATUS_DRIVER_OK));
 capacity=inl(iobase+REG_CONFIG)|((uint64_t)inl(iobase+REG_CONFIG+4u)<<32);
 outw(iobase+REG_QUEUE_SEL,0u);
 qnum=inw(iobase+REG_QUEUE_NUM);
 if(!qnum||qnum>VBLK_QUEUE_MAX||!capacity)return -3;
 if(qnum<3u*VBLK_SLOTS)return -4;
 queue_phys=vqeaf_pmm_alloc_pages(VBLK_QUEUE_PAGES);
 req_phys=vqeaf_pmm_alloc_pages(1u);
 if(!queue_phys||!req_phys)return -5;
 uint8_t *z=(uint8_t*)(uintptr_t)queue_phys;
 for(uint32_t i=0;i<VBLK_QUEUE_PAGES*VQEAF_PAGE_SIZE;i++)z[i]=0;
 z=(uint8_t*)(uintptr_t)req_phys;
 for(uint32_t i=0;i<VQEAF_PAGE_SIZE;i++)z[i]=0;
 desc=(struct vring_desc*)(uintptr_t)queue_phys;
 avail=(struct vring_avail*)(uintptr_t)(queue_phys+(uint64_t)qnum*16u);
 uint64_t end=(uint64_t)qnum*16u+4u+2u*(uint64_t)qnum;
 used=(struct vring_used*)(uintptr_t)(queue_phys+((end+4095u)&~4095ull));
 outl(iobase+REG_QUEUE_PFN,(uint32_t)(queue_phys>>12));
 vqeaf_arch_install_irq(irq_line,(void*)vqeaf_virtio_irq_stub);
 vqeaf_arch_irq_unmask(irq_line);
 ready=1;
 vqeaf_serial_write("VIRTIO: PCI 1AF4:1001 bus=0 slot=");serial_hex(pci.slot);
 vqeaf_serial_write(" BAR0=");serial_hex(iobase);
 vqeaf_serial_write(" IRQ=");serial_hex(irq_line);
 vqeaf_serial_write(" features=");serial_hex(features);
 vqeaf_serial_write(" capacity=");serial_hex(capacity);
 vqeaf_serial_write(" sectors queue=");serial_hex(qnum);vqeaf_serial_write("\r\n");
 /* Driver model: READY only after a successful probe. */
 if(vqeaf_driver_register(&vblk_driver)==0){
  uint32_t inst=0;
  if(vqeaf_driver_bind("virtio-blk-pci",(void*)(uintptr_t)iobase,&inst)==0){
   instance_id=inst;
   vqeaf_cap_set("storage.block",VQEAF_CAP_READY,inst,0);
   vqeaf_cap_set("storage.virtio_blk",VQEAF_CAP_READY,inst,0);
   vqeaf_serial_write("VIRTIO: driver probe READY, instance=");serial_hex(inst);vqeaf_serial_write("\r\n");
  }
 }
 block_dev.name="virtio0";block_dev.sector_size=VQEAF_VBLK_SECTOR_SIZE;
 block_dev.sector_count=capacity;block_dev.read_sectors=block_read_cb;
 block_dev.write_sectors=0; /* read-only media policy for this phase */
 block_dev.ctx=0;
 int id=vqeaf_block_register(&block_dev);
 if(id<0)return -6;
 block_id=(uint32_t)id;
 vqeaf_serial_write("VIRTIO: block device registered id=");serial_hex(block_id);vqeaf_serial_write("\r\n");
 return 0;
}
int vqeaf_virtio_blk_ready(void){return ready;}
uint64_t vqeaf_virtio_blk_capacity(void){return capacity;}
uint32_t vqeaf_virtio_blk_dev_id(void){return block_id;}
uint32_t vqeaf_virtio_blk_irq_count(void){return irq_count;}
uint32_t vqeaf_virtio_blk_completion_count(void){return completion_count;}
