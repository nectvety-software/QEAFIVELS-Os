/* VQEAF AArch64 kernel boot entry (QEMU virt, -kernel ELF).
 *
 * QEMU may enter at NS-EL2 or EL1 depending on machine configuration; handle
 * both. If at EL2, drop to EL1h (AArch64) via eret. At EL1: set the stack,
 * clear .bss, allow unaligned accesses, enable FP/SIMD, then run kernel_main.
 * On return, PSCI SYSTEM_OFF (HVC conduit) so QEMU exits cleanly. */
.section .text.boot,"ax"
.global _start
_start:
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    and     x0, x0, #3
    cmp     x0, #2
    b.eq    .Lfrom_el2
    b       .Lel1

.Lfrom_el2:
    /* EL1 executes AArch64 (HCR_EL2.RW). */
    mrs     x0, hcr_el2
    orr     x0, x0, #(1 << 31)
    msr     hcr_el2, x0
    /* Do not trap EL1 FP/SIMD or system-register access. */
    msr     cptr_el2, xzr
    /* Architectural SCTLR_EL1 reset value (MMU/caches off). */
    mov     x0, #0x0800
    movk    x0, #0x30d0, lsl #16
    msr     sctlr_el1, x0
    /* Return to EL1h with DAIF masked. */
    mov     x0, #0x3c5
    msr     spsr_el2, x0
    adr     x0, .Lel1
    msr     elr_el2, x0
    eret

.Lel1:
    ldr     x0, =_stack_top
    mov     sp, x0
    /* Clear .bss (NOBITS is not loaded from the file). */
    ldr     x0, =_bss_start
    ldr     x1, =_bss_end
.Lbss:
    cmp     x0, x1
    b.ge    .Lbss_done
    str     xzr, [x0], #8
    b       .Lbss
.Lbss_done:
    /* Allow unaligned accesses: clear SCTLR_EL1.A (bit 1). */
    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 1)
    msr     sctlr_el1, x0
    /* Enable FP/SIMD at EL0/EL1: CPACR_EL1.FPEN = 0b11. */
    mrs     x0, cpacr_el1
    orr     x0, x0, #(0x3 << 20)
    msr     cpacr_el1, x0
    isb
    bl      kernel_main
    /* PSCI SYSTEM_OFF via HVC conduit. */
    ldr     x0, =0x84000008
    hvc     #0
.Lhalt:
    wfi
    b       .Lhalt