#include <stdint.h>
#include "../../include/vqeaf/arch_x86_64.h"
#include "../../include/vqeaf/serial.h"
#include "../../include/vqeaf/memory.h"
struct __attribute__((packed)) TablePtr { uint16_t limit; uint64_t base; };
struct __attribute__((packed)) IdtGate { uint16_t low,selector; uint8_t ist,flags; uint16_t mid; uint32_t high,zero; };
struct InterruptFrame { uint64_t rip,cs,rflags,rsp,ss; };
/* Ring 3 support: 0x18/0x20 are user code/data; 0x28 is the TSS (a 16-byte
 * 64-bit system descriptor spanning two GDT slots). TSS.RSP0 routes ring-3
 * interrupts to a dedicated interrupt stack owned by this core. */
#define VQEAF_SEL_TSS 0x28u
#define VQEAF_IRQ_STACK_PAGES 8u
static uint64_t gdt[7] __attribute__((aligned(16)));
static struct IdtGate idt[256] __attribute__((aligned(16)));
static uint8_t tss[104] __attribute__((aligned(16)));
static uint64_t irq_stack_phys;
static volatile uint64_t panic_rip;
static uint8_t pic_master_mask=0xff,pic_slave_mask=0xff;
static void set_gate(unsigned n,void *fn){uint64_t a=(uint64_t)fn;idt[n].low=(uint16_t)a;idt[n].selector=0x08;idt[n].ist=0;idt[n].flags=0x8e;idt[n].mid=(uint16_t)(a>>16);idt[n].high=(uint32_t)(a>>32);idt[n].zero=0;}
static void serial_hex(uint64_t v){static const char h[]="0123456789ABCDEF";for(int s=60;s>=0;s-=4)vqeaf_serial_char(h[(v>>s)&15]);}
void vqeaf_panic(const char *reason,uint64_t code){
 __asm__ volatile("cli");vqeaf_serial_write("\r\n*** VQEAF KERNEL PANIC ***\r\n");
 vqeaf_serial_write(reason?reason:"unknown");vqeaf_serial_write(" code=0x");serial_hex(code);
 vqeaf_serial_write(" RIP=0x");serial_hex(panic_rip);vqeaf_serial_write("\r\nSystem halted.\r\n");
 for(;;)__asm__ volatile("hlt");
}
#define NOERR(n) static void __attribute__((interrupt)) ex_##n(struct InterruptFrame*f){panic_rip=f->rip;vqeaf_panic("CPU exception",n);}
#define ERR(n) static void __attribute__((interrupt)) ex_##n(struct InterruptFrame*f,uint64_t e){panic_rip=f->rip;vqeaf_panic("CPU exception error",((uint64_t)(n)<<32)|(e&0xffffffffu));}
NOERR(0) NOERR(1) NOERR(2) NOERR(3) NOERR(4) NOERR(5) NOERR(6) NOERR(7)
ERR(8) NOERR(9) ERR(10) ERR(11) ERR(12) ERR(13) NOERR(14) NOERR(15)
NOERR(16) ERR(17) NOERR(18) NOERR(19) NOERR(20) ERR(21) NOERR(22) NOERR(23)
NOERR(24) NOERR(25) NOERR(26) NOERR(27) NOERR(28) ERR(29) ERR(30) NOERR(31)
static void __attribute__((interrupt)) default_isr(struct InterruptFrame*f){panic_rip=f->rip;vqeaf_panic("Unexpected interrupt",255);}
/* IRQ0, the page fault and the syscall gate are routed through the assembly
 * stubs in switch.S for register-level context switching; everything else
 * still panics. */
extern void vqeaf_irq0_stub(void);
extern void vqeaf_pf_stub(void);
extern void vqeaf_syscall_stub(void);
void vqeaf_sched_eoi(void){vqeaf_out8(0x20,0x20);}
/* Device IRQs: install/replace an IDT gate, unmask the 8259 line (including
 * the cascade IRQ2 for slave lines) and issue the correct EOI sequence. */
void vqeaf_arch_install_irq(uint8_t irq,void *handler){
 if(irq<16u&&handler)set_gate(32u+irq,handler);
}
void vqeaf_arch_irq_unmask(uint8_t irq){
 if(irq<8u){pic_master_mask&=(uint8_t)~(1u<<irq);vqeaf_out8(0x21,pic_master_mask);}
 else if(irq<16u){
  pic_slave_mask&=(uint8_t)~(1u<<(irq-8u));
  pic_master_mask&=(uint8_t)~(1u<<2u); /* cascade: slave PIC reaches CPU via IRQ2 */
  vqeaf_out8(0xA1,pic_slave_mask);vqeaf_out8(0x21,pic_master_mask);
 }
}
void vqeaf_arch_irq_eoi(uint8_t irq){
 if(irq>=8u)vqeaf_out8(0xA0,0x20);
 vqeaf_out8(0x20,0x20);
}
void vqeaf_panic_set_rip(uint64_t rip){panic_rip=rip;}
void vqeaf_arch_init(void){
 gdt[0]=0;
 gdt[1]=0x00af9a000000ffffull; /* 0x08 kernel code */
 gdt[2]=0x00cf92000000ffffull; /* 0x10 kernel data */
 gdt[3]=0x00affa000000ffffull; /* 0x18 user code, 64-bit long mode (L=1), DPL3 */
 gdt[4]=0x00cff2000000ffffull; /* 0x20 user data, DPL3 */
 gdt[5]=0;gdt[6]=0;            /* TSS descriptor, filled by ring3 init */
 struct TablePtr gdtr={(uint16_t)(sizeof(gdt)-1),(uint64_t)gdt};
 __asm__ volatile("lgdt %0"::"m"(gdtr):"memory");
 __asm__ volatile("pushq $0x08; leaq 1f(%%rip),%%rax; pushq %%rax; lretq; 1: mov $0x10,%%ax; mov %%ax,%%ds; mov %%ax,%%es; mov %%ax,%%ss":::"rax","memory");
 for(unsigned i=0;i<256;i++)set_gate(i,default_isr);
 void(*const handlers[32])(void)={
 (void(*)(void))ex_0,(void(*)(void))ex_1,(void(*)(void))ex_2,(void(*)(void))ex_3,
 (void(*)(void))ex_4,(void(*)(void))ex_5,(void(*)(void))ex_6,(void(*)(void))ex_7,
 (void(*)(void))ex_8,(void(*)(void))ex_9,(void(*)(void))ex_10,(void(*)(void))ex_11,
 (void(*)(void))ex_12,(void(*)(void))ex_13,(void(*)(void))ex_14,(void(*)(void))ex_15,
 (void(*)(void))ex_16,(void(*)(void))ex_17,(void(*)(void))ex_18,(void(*)(void))ex_19,
 (void(*)(void))ex_20,(void(*)(void))ex_21,(void(*)(void))ex_22,(void(*)(void))ex_23,
 (void(*)(void))ex_24,(void(*)(void))ex_25,(void(*)(void))ex_26,(void(*)(void))ex_27,
 (void(*)(void))ex_28,(void(*)(void))ex_29,(void(*)(void))ex_30,(void(*)(void))ex_31};
 for(unsigned i=0;i<32;i++)set_gate(i,(void*)handlers[i]);
 set_gate(32,(void*)vqeaf_irq0_stub);
 set_gate(14,(void*)vqeaf_pf_stub);
 set_gate(128,(void*)vqeaf_syscall_stub);idt[128].flags=0xEE; /* DPL3 syscall gate */
 struct TablePtr idtr={(uint16_t)(sizeof(idt)-1),(uint64_t)idt};
 __asm__ volatile("lidt %0"::"m"(idtr):"memory");
 vqeaf_out8(0x20,0x11);vqeaf_out8(0xA0,0x11);
 vqeaf_out8(0x21,0x20);vqeaf_out8(0xA1,0x28);
 vqeaf_out8(0x21,0x04);vqeaf_out8(0xA1,0x02);
 vqeaf_out8(0x21,0x01);vqeaf_out8(0xA1,0x01);
 pic_master_mask=0xff;pic_slave_mask=0xff;
 vqeaf_out8(0x21,pic_master_mask);vqeaf_out8(0xA1,pic_slave_mask);
 vqeaf_serial_write("x86_64: GDT + IDT + exception handlers ready\r\n");
}
/* Ring-3 bring-up: dedicated interrupt stack + TSS + TR. Needs PMM online. */
void vqeaf_arch_ring3_init(void){
 irq_stack_phys=vqeaf_pmm_alloc_pages(VQEAF_IRQ_STACK_PAGES);
 if(!irq_stack_phys)vqeaf_panic("Ring3: IRQ stack alloc failed",0);
 uint64_t top=irq_stack_phys+VQEAF_IRQ_STACK_PAGES*VQEAF_PAGE_SIZE;
 for(unsigned i=0;i<sizeof(tss);i++)tss[i]=0;
 tss[4]=(uint8_t)top;tss[5]=(uint8_t)(top>>8);tss[6]=(uint8_t)(top>>16);tss[7]=(uint8_t)(top>>24);
 tss[8]=(uint8_t)(top>>32);tss[9]=(uint8_t)(top>>40);tss[10]=(uint8_t)(top>>48);tss[11]=(uint8_t)(top>>56);
 uint64_t base=(uint64_t)&tss[0];
 gdt[5]=(base&0xFFFFFFull)<<16|0x67ull|(0x89ull<<40)|((base>>24)&0xFFull)<<56;
 gdt[6]=base>>32;
 struct TablePtr gdtr={(uint16_t)(sizeof(gdt)-1),(uint64_t)gdt};
 __asm__ volatile("lgdt %0"::"m"(gdtr):"memory");
 __asm__ volatile("ltr %0"::"r"((uint16_t)VQEAF_SEL_TSS));
 vqeaf_serial_write("x86_64: ring 3 ready (user segments + TSS RSP0)\r\n");
}
void vqeaf_arch_timer_init(uint32_t hz){
 if(hz<19)hz=19;
 if(hz>1000)hz=1000;
 uint32_t d=1193182u/hz;vqeaf_out8(0x43,0x36);vqeaf_out8(0x40,(uint8_t)d);vqeaf_out8(0x40,(uint8_t)(d>>8));
 vqeaf_serial_write("x86_64: PIT timer IRQ0 enabled\r\n");
 /* Unmask IRQ0 but leave interrupts OFF: vqeaf_sched_start() enables them
  * only once the first task context is armed, so no tick can preempt the
  * entry code before a task owns the CPU. Use the mask helper so device
  * lines unmasked earlier (e.g. VirtIO INTx) stay enabled. */
 vqeaf_arch_irq_unmask(0u);
}
