#include "../include/vqeaf/scheduler.h"
#include "../include/vqeaf/memory.h"
#include "../include/vqeaf/vm.h"
#include "../include/vqeaf/serial.h"
#include "../include/vqeaf/arch_x86_64.h"
extern void vqeaf_task_trampoline(void); /* switch.S */
extern uint64_t vqeaf_pf_cr2_raw; /* captured in the PF stub before anything else */
extern void vqeaf_sched_enter_first(uint64_t rsp); /* switch.S */
/* Preemptive round-robin scheduler. Context switch is interrupt driven:
 * the IRQ0 assembly stub saves all general-purpose registers on the interrupted
 * task's kernel stack, calls vqeaf_sched_preempt() with that stack pointer and
 * resumes whatever task stack it returns. Tasks may run in kernel mode or in
 * ring 3 with their own address space (PML4 slot 16); CR3 switches when the
 * roots differ. */
static VqeafTask tasks[VQEAF_TASK_CAPACITY];
static int32_t current=-1;
static uint64_t ticks;
static uint64_t kernel_root;
static volatile uint64_t expect_addr, expect_advance;
static volatile int expect_active;
void vqeaf_sched_init(void){for(unsigned i=0;i<VQEAF_TASK_CAPACITY;i++){tasks[i].id=0;tasks[i].state=VQEAF_TASK_UNUSED;tasks[i].kind=VQEAF_TASK_KERNEL;tasks[i].ticks=0;tasks[i].rsp=0;tasks[i].root=0;tasks[i].kernel_stack=0;tasks[i].stack_pages=0;tasks[i].user_stack=0;tasks[i].user_stack_pages=0;tasks[i].fn=0;tasks[i].arg=0;tasks[i].cred.uid=VQEAF_UID_SYSTEM;tasks[i].cred.gid=VQEAF_GID_SYSTEM;tasks[i].cred.caps=VQEAF_CAP_ALL;}current=-1;ticks=0;}
int vqeaf_sched_add(uint32_t id){if(!id)return -1;for(unsigned i=0;i<VQEAF_TASK_CAPACITY;i++)if(tasks[i].state==VQEAF_TASK_UNUSED){tasks[i].id=id;tasks[i].state=VQEAF_TASK_READY;return (int)i;}return -2;}
void vqeaf_sched_tick(void){ticks++;if(current>=0&&tasks[current].state==VQEAF_TASK_RUNNING){tasks[current].ticks++;tasks[current].state=VQEAF_TASK_READY;}for(unsigned n=1;n<=VQEAF_TASK_CAPACITY;n++){unsigned i=(unsigned)((current+(int)n+VQEAF_TASK_CAPACITY)%VQEAF_TASK_CAPACITY);if(tasks[i].state==VQEAF_TASK_READY){current=(int32_t)i;tasks[i].state=VQEAF_TASK_RUNNING;break;}}}
int32_t vqeaf_sched_current(void){return current;}uint64_t vqeaf_timer_ticks(void){return ticks;}
void vqeaf_sched_set_kernel_root(uint64_t root){kernel_root=root;}
uint64_t vqeaf_sched_trap_stack(void){
 if(current<0)vqeaf_panic("Trap before scheduler start",ticks);
 return tasks[current].kernel_stack+tasks[current].stack_pages*VQEAF_PAGE_SIZE;
}
/* Build the initial trap frame. Layout (low to high) mirrors the stubs:
 * pad | r15 r14 r13 r12 r11 r10 r9 r8 rbp rdi rsi rbx rdx rcx rax | rip cs rflags rsp ss */
static void build_initial_frame(VqeafTask *t,uint64_t rip,uint64_t entry_rsp,uint16_t cs,uint16_t ss){
 uint64_t top=t->kernel_stack+t->stack_pages*VQEAF_PAGE_SIZE;
 uint64_t *f=(uint64_t*)(top-21*8);
 for(int i=0;i<21;i++)f[i]=0;
 f[4]=(uint64_t)t->fn;       /* r12 slot */
 f[3]=(uint64_t)t->arg;      /* r13 slot */
 f[16]=rip;f[17]=cs;f[18]=0x202ull;f[19]=entry_rsp;f[20]=ss;
 t->rsp=(uint64_t)f;
}
int vqeaf_task_create(VqeafTaskFn fn,void *arg){
 if(!fn)return -1;
 int slot=-1;
 for(unsigned i=0;i<VQEAF_TASK_CAPACITY;i++)if(tasks[i].state==VQEAF_TASK_UNUSED){slot=(int)i;break;}
 if(slot<0)return -2;
 uint64_t run=vqeaf_pmm_alloc_pages(VQEAF_TASK_STACK_PAGES+1); /* +1 guard page */
 if(!run)return -3;
 VqeafTask *t=&tasks[slot];
 t->id=(uint32_t)(slot+1);t->state=VQEAF_TASK_READY;t->ticks=0;t->kind=VQEAF_TASK_KERNEL;
 t->kernel_stack=run+VQEAF_PAGE_SIZE; /* lowest mapped page; guard stays unmapped */
 t->stack_pages=VQEAF_TASK_STACK_PAGES;
 t->fn=fn;t->arg=arg;t->root=0;t->user_stack=0;t->user_stack_pages=0;
 build_initial_frame(t,(uint64_t)vqeaf_task_trampoline,t->kernel_stack+t->stack_pages*VQEAF_PAGE_SIZE,0x08,0x10);
 return slot;
}
int vqeaf_task_create_user(uint64_t root,uint64_t entry,uint64_t user_rsp_top){
 if(!root||entry<VQEAF_USER_REGION_BASE||entry>=VQEAF_USER_REGION_LIMIT)return -1;
 int slot=-1;
 for(unsigned i=0;i<VQEAF_TASK_CAPACITY;i++)if(tasks[i].state==VQEAF_TASK_UNUSED){slot=(int)i;break;}
 if(slot<0)return -2;
 uint64_t run=vqeaf_pmm_alloc_pages(VQEAF_TASK_STACK_PAGES+1);
 if(!run)return -3;
 uint64_t urun=vqeaf_pmm_alloc_pages(VQEAF_USER_STACK_PAGES);
 if(!urun)return -4;
 VqeafTask *t=&tasks[slot];
 t->id=(uint32_t)(slot+1);t->state=VQEAF_TASK_READY;t->ticks=0;t->kind=VQEAF_TASK_USER;
 t->kernel_stack=run+VQEAF_PAGE_SIZE;t->stack_pages=VQEAF_TASK_STACK_PAGES;
 t->user_stack=urun;t->user_stack_pages=VQEAF_USER_STACK_PAGES;
 t->fn=0;t->arg=0;t->root=root;
 t->cred.uid=VQEAF_UID_SYSTEM;t->cred.gid=VQEAF_GID_SYSTEM;t->cred.caps=VQEAF_CAP_ALL;
 for(uint64_t i=0;i<VQEAF_USER_STACK_PAGES;i++)
  if(vqeaf_vm_map(root,VQEAF_USER_STACK_BASE+i*VQEAF_PAGE_SIZE,urun+i*VQEAF_PAGE_SIZE,VQEAF_VM_WRITE|VQEAF_VM_USER)!=0)return -5;
 /* The user stack lives in the user half of the address space. Never map it
  * into the identity-mapped kernel half: ORing the USER bit into shared kernel
  * page-table entries would expose all kernel memory to ring 3. */
 build_initial_frame(t,entry,user_rsp_top,0x1B,0x23); /* ring 3: cs=0x18|3, ss=0x20|3 */
 return slot;
}
uint64_t vqeaf_task_stack_base(int slot){if(slot<0||slot>=(int)VQEAF_TASK_CAPACITY)return 0;if(tasks[slot].state==VQEAF_TASK_UNUSED)return 0;return tasks[slot].kernel_stack;}
void vqeaf_task_exit(void){
 __asm__ volatile("cli");
 if(current>=0)tasks[current].state=VQEAF_TASK_BLOCKED;
 __asm__ volatile("sti");
 for(;;)__asm__ volatile("hlt");
}
void vqeaf_sched_start(void){
 int first=-1;
 for(unsigned i=0;i<VQEAF_TASK_CAPACITY;i++)if(tasks[i].state==VQEAF_TASK_READY&&tasks[i].rsp){first=(int)i;break;}
 if(first<0)vqeaf_panic("Scheduler start: no runnable task",0);
 current=first;tasks[first].state=VQEAF_TASK_RUNNING;
 if(tasks[first].root)vqeaf_vm_activate(tasks[first].root);
 vqeaf_serial_write("SCHED: entering first task\r\n");
 /* No sti here: a tick between sti and the switch would save the entry
  * context over the first task's state. The first iretq enables interrupts
  * through the task's saved RFLAGS (IF=1). */
 vqeaf_sched_enter_first(tasks[first].rsp);
}
static int pick_next(int from){
 for(unsigned n=1;n<=VQEAF_TASK_CAPACITY;n++){
  unsigned i=(unsigned)((from+(int)n+VQEAF_TASK_CAPACITY)%VQEAF_TASK_CAPACITY);
  if(tasks[i].state==VQEAF_TASK_READY&&tasks[i].rsp)return (int)i;
 }
 return -1;
}
static uint64_t effective_root(int slot){
 if(tasks[slot].root)return tasks[slot].root;
 return kernel_root;
}
uint64_t vqeaf_sched_preempt(uint64_t frame_base){
 ticks++;
 if(ticks==1)vqeaf_serial_write("PIT first tick observed\r\n");
 int cur=current;
 if(cur>=0&&tasks[cur].state==VQEAF_TASK_RUNNING){
  tasks[cur].rsp=frame_base;
  tasks[cur].ticks++;
  tasks[cur].state=VQEAF_TASK_READY;
 }
 int next=cur>=0?pick_next(cur):pick_next(-1);
 if(next<0){
  if(cur>=0){tasks[cur].state=VQEAF_TASK_RUNNING;vqeaf_sched_eoi();return frame_base;}
  vqeaf_panic("Scheduler: no runnable task",ticks);
 }
 current=next;tasks[next].state=VQEAF_TASK_RUNNING;
 uint64_t want=effective_root(next);
 if(want&&want!=vqeaf_vm_current_root())vqeaf_vm_activate(want);
 vqeaf_sched_eoi();
 return tasks[next].rsp;
}
void vqeaf_pf_expect(uint64_t virtual_address,uint64_t rip_advance){
 expect_addr=virtual_address;expect_advance=rip_advance;expect_active=1;
}
uint64_t vqeaf_pf_preempt(uint64_t frame_base){
 /* Error-code frame: err | rip | cs | rflags | rsp | ss starting at +128 */
 uint64_t *f=(uint64_t*)(frame_base+128);
 uint64_t err=f[0],rip=f[1],cr2;
 __asm__ volatile("mov %%cr2,%0":"=r"(cr2));
 if(expect_active&&cr2==expect_addr&&!(err&1)){
  f[1]=rip+expect_advance; /* skip the faulting instruction */
  expect_active=0;expect_addr=0;expect_advance=0;
  vqeaf_serial_write("PF: guard page fault recovered, RIP advanced\r\n");
  vqeaf_sched_eoi();
  return frame_base;
 }
 vqeaf_panic_set_rip(rip);
 {uint64_t *regs=(uint64_t*)(frame_base+8);static const char h[]="0123456789ABCDEF";uint64_t vals[4]={regs[1],regs[9],regs[14],regs[7]};const char *names[4]={"PF r14="," rdi="," rax="," r8="};for(int j=0;j<4;j++){vqeaf_serial_write(names[j]);for(int s=60;s>=0;s-=4)vqeaf_serial_char(h[(vals[j]>>s)&15]);}vqeaf_serial_write("\r\n");}
 /* Diagnostics: full fault frame + paging walk. */
 vqeaf_serial_write("PF frame: err=");
 {uint64_t v=err;for(int s=60;s>=0;s-=4){static const char h[]="0123456789ABCDEF";vqeaf_serial_char(h[(v>>s)&15]);}}
 vqeaf_serial_write(" rip=");{uint64_t v=rip;for(int s=60;s>=0;s-=4){static const char h[]="0123456789ABCDEF";vqeaf_serial_char(h[(v>>s)&15]);}}
 vqeaf_serial_write(" cs=");{uint64_t v=f[2];for(int s=60;s>=0;s-=4){static const char h[]="0123456789ABCDEF";vqeaf_serial_char(h[(v>>s)&15]);}}
 vqeaf_serial_write(" fl=");{uint64_t v=f[3];for(int s=60;s>=0;s-=4){static const char h[]="0123456789ABCDEF";vqeaf_serial_char(h[(v>>s)&15]);}}
 vqeaf_serial_write(" rsp=");{uint64_t v=f[4];for(int s=60;s>=0;s-=4){static const char h[]="0123456789ABCDEF";vqeaf_serial_char(h[(v>>s)&15]);}}
 vqeaf_serial_write(" ss=");{uint64_t v=f[5];for(int s=60;s>=0;s-=4){static const char h[]="0123456789ABCDEF";vqeaf_serial_char(h[(v>>s)&15]);}}
 vqeaf_serial_write(" cr2raw=");{uint64_t v=vqeaf_pf_cr2_raw;for(int s=60;s>=0;s-=4){static const char h[]="0123456789ABCDEF";vqeaf_serial_char(h[(v>>s)&15]);}}
 vqeaf_serial_write(" cr2=");{uint64_t v=cr2;for(int s=60;s>=0;s-=4){static const char h[]="0123456789ABCDEF";vqeaf_serial_char(h[(v>>s)&15]);}}
 vqeaf_serial_write("\r\n");
 /* Diagnostics: dump the paging walk for the faulting address. */
 vqeaf_serial_write("PF: CR3=0x");
 {uint64_t cr3;__asm__ volatile("mov %%cr3,%0":"=r"(cr3));
  for(int s=60;s>=0;s-=4){static const char h[]="0123456789ABCDEF";vqeaf_serial_char(h[(cr3>>s)&15]);}
  uint64_t pa=cr3&0x000FFFFFFFFFF000ull;
  uint64_t *pml4=(uint64_t*)pa;
  unsigned i2=(unsigned)((cr2>>39)&511),i1=(unsigned)((cr2>>30)&511),i0=(unsigned)((cr2>>21)&511),il=(unsigned)((cr2>>12)&511);
  vqeaf_serial_write(" PML4[");vqeaf_serial_char('0'+(char)i2/10);vqeaf_serial_char('0'+i2%10);vqeaf_serial_write("] Entry walk:");
  for(int lvl=0;lvl<4;lvl++){
   uint64_t e;
   if(lvl==0)e=pml4[i2];else if(lvl==1){uint64_t *t=(uint64_t*)(pml4[i2]&0x000FFFFFFFFFF000ull);e=t?t[i1]:0;}
   else if(lvl==2){uint64_t *t=(uint64_t*)(pml4[i2]&0x000FFFFFFFFFF000ull);t=t?(uint64_t*)(t[i1]&0x000FFFFFFFFFF000ull):0;e=t?t[i0]:0;}
   else{uint64_t *t=(uint64_t*)(pml4[i2]&0x000FFFFFFFFFF000ull);t=t?(uint64_t*)(t[i1]&0x000FFFFFFFFFF000ull):0;t=t?(uint64_t*)(t[i0]&0x000FFFFFFFFFF000ull):0;e=t?t[il]:0;}
   vqeaf_serial_write(" ");
   for(int s=60;s>=0;s-=4){static const char h[]="0123456789ABCDEF";vqeaf_serial_char(h[(e>>s)&15]);}
   if(e&0x80&&lvl<2)break;
  }
 }
 vqeaf_serial_write("\r\n");
 vqeaf_panic("Page fault",((err&0xFFFFull)<<48)|(cr2&0xFFFFFFFFFFFFull));
 return 0;
}
/* int 0x80 syscall gate (ring 3). Saved GP registers sit BELOW the CPU frame:
 * (frame_base+8)[0..14] = r15,r14,r13,r12,r11,r10,r9,r8,rbp,rdi,rsi,rbx,rdx,rcx,rax.
 * The CPU frame itself (rip,cs,rflags,rsp,ss) starts at frame_base+128. */
uint64_t vqeaf_syscall_preempt(uint64_t frame_base){
 uint64_t *g=(uint64_t*)(frame_base+8);
 uint64_t num=g[14],a0=g[9],ret=0;
 switch(num){
 case 1: /* sys_putchar: rdi = character */
  if(a0<128)vqeaf_serial_char((char)a0);
  ret=0;break;
 case 2: /* sys_getchar: non-blocking serial read */
  if(vqeaf_in8(0x3FD)&0x01)ret=vqeaf_in8(0x3F8);else ret=(uint64_t)-1;
  break;
 case 3: /* sys_exit */
  vqeaf_task_exit(); /* never returns */
  break;
 default:
  ret=(uint64_t)-2;break;
 }
 g[14]=ret;
 return frame_base;
}
