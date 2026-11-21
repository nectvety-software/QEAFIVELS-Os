/* AArch64 boot entry for the QEMU virt harness: set stack, run harness_main,
 * then PSCI SYSTEM_OFF so QEMU exits cleanly. */
.section .text.boot,"ax"
.global _start
_start:
    /* Allow unaligned accesses: clear SCTLR_EL1.A (bit 1). The .qen header
     * stores fields at odd offsets (code@21, ...) and the compiler emits
     * unaligned word loads; QEMU cortex-a57 resets with alignment checks on. */
    mrs x0, sctlr_el1
    bic x0, x0, #(1 << 1)
    msr sctlr_el1, x0
    /* Enable FP/SIMD at EL0/EL1: CPACR_EL1.FPEN = 0b11. Without this the
     * first NEON/FP instruction the compiler emits traps (EC 0x07) because
     * the reset value disables the FPU. Covers harness and module code. */
    mrs x0, cpacr_el1
    orr x0, x0, #(0x3 << 20)
    msr cpacr_el1, x0
    isb
    ldr x0, =_stack_top
    mov sp, x0
    bl harness_main
    ldr x0, =0x84000008
    hvc #0
.Lhalt:
    wfi
    b .Lhalt