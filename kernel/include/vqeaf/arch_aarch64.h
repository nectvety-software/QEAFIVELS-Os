#ifndef VQEAF_ARCH_AARCH64_H
#define VQEAF_ARCH_AARCH64_H
#include <stdint.h>
/* QEMU virt memory layout used by the AArch64 kernel port. The boot gate runs
 * with -m 128M; RAM base is fixed at 0x40000000 by the virt machine. */
#define VQEAF_A64_RAM_BASE 0x40000000ull
#define VQEAF_A64_RAM_SIZE (128ull*1024ull*1024ull)
#define VQEAF_A64_UART_BASE 0x09000000ull
/* One-time MMU bring-up: MAIR/TCR then SCTLR_EL1.M|C|I. Call after the identity
 * map is built and TTBR0_EL1 is loaded. */
void vqeaf_aarch64_mmu_enable(void);
/* Exceptions, GICv2 and the ARM generic timer (QEMU virt). */
void vqeaf_a64_exceptions_init(void);
void vqeaf_a64_gic_init(void);
void vqeaf_a64_timer_init(void);
uint64_t vqeaf_a64_timer_ticks(void);
/* IRQ entry: ack the GIC, service the timer, and (once the scheduler is live)
 * return the task frame to resume. Called from the vector with the saved frame
 * pointer; the vector restores from the returned pointer and erets. */
uint64_t vqeaf_a64_irq_preempt(uint64_t frame_base);
/* Sync entry: decode ESR_EL1; dispatch EL0 SVC syscalls against the frame. */
void vqeaf_a64_sync_handler(uint64_t frame_base);
void vqeaf_a64_bad_handler(void);
/* Assembly helpers (vectors.S). */
void vqeaf_a64_enter_first(uint64_t frame_base);
void vqeaf_a64_task_trampoline(void);
void vqeaf_a64_power_off(void);
/* AArch64 scheduler (sched.c): task switch driven by the timer IRQ. */
int vqeaf_a64_sched_active(void);
uint64_t vqeaf_a64_sched_switch(uint64_t frame_base);
/* Single-CPU critical section: QEMU virt has one core, so the only source of
 * concurrency is timer-IRQ preemption. Mask IRQ around non-reentrant kernel
 * operations (task creation / AMS launch) and restore the previous I state.
 * Nesting is safe because the saved DAIF is restored verbatim. */
uint64_t vqeaf_a64_irq_save(void);
void vqeaf_a64_irq_restore(uint64_t daif);
/* Validate that [va,va+len) is user-region memory mapped EL0-accessible in the
 * given root; used by the IPC syscalls before the kernel copies user data. */
int vqeaf_vm_user_range_mapped(uint64_t root, uint64_t va, uint64_t len);
/* EL0 SVC syscall numbers and shared structs: single source of truth shared
 * with the EL0 programs (see vqeaf/el0_abi.h). */
#include "vqeaf/el0_abi.h"

/* QEMU virt: the single PL011 at 0x09000000 uses SPI 1 => GIC INTID 33.
 * Verified from the machine's own device tree (/pl011@9000000 interrupts =
 * <0 1 4>). The timer PPI stays at INTID 30. */
#define VQEAF_A64_UART_IRQ 33u
void vqeaf_a64_input_init(void);
uint32_t vqeaf_a64_input_irq_count(void);
#endif