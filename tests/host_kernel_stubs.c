/* Host-only stubs for arch-dependent kernel symbols used by scheduler.c so the
 * freestanding kernel core (PMM, scheduler metadata, heap) stays unit-testable. */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "../kernel/include/vqeaf/arch_x86_64.h"
#include "../kernel/include/vqeaf/vm.h"
void vqeaf_panic(const char *reason,uint64_t code){(void)code;fprintf(stderr,"host panic: %s\n",reason?reason:"unknown");abort();}
void vqeaf_panic_set_rip(uint64_t rip){(void)rip;}
void vqeaf_sched_eoi(void){}
uint64_t vqeaf_vm_current_root(void){return 0;}
void vqeaf_vm_activate(uint64_t root){(void)root;}
int vqeaf_vm_map_range(uint64_t root,uint64_t va,uint64_t pa,uint64_t pages,uint64_t flags){(void)root;(void)va;(void)pa;(void)pages;(void)flags;return 0;}
int vqeaf_vm_map(uint64_t root,uint64_t va,uint64_t pa,uint64_t flags){(void)root;(void)va;(void)pa;(void)flags;return 0;}
void vqeaf_sched_enter_first(uint64_t rsp){(void)rsp;}
uint64_t vqeaf_pf_cr2_raw;
void vqeaf_task_trampoline(void){}
