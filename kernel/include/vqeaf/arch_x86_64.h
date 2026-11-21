#ifndef VQEAF_ARCH_X86_64_H
#define VQEAF_ARCH_X86_64_H
#include <stdint.h>
void vqeaf_arch_init(void);
void vqeaf_arch_ring3_init(void); /* needs PMM; installs TSS + ring-3 GDT entries */
void vqeaf_arch_timer_init(uint32_t hz);
void vqeaf_panic(const char *reason,uint64_t code);
void vqeaf_panic_set_rip(uint64_t rip);
void vqeaf_sched_eoi(void);
void vqeaf_arch_install_irq(uint8_t irq,void *handler);
void vqeaf_arch_irq_unmask(uint8_t irq);
void vqeaf_arch_irq_eoi(uint8_t irq);
#endif
