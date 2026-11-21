/* VQEAF AArch64 exception vector table (VBAR_EL1, 2 KiB aligned, 16 entries).
 *
 * Each entry saves the full interrupted context (x0-x30, ELR, SPSR, SP_EL0)
 * onto the kernel stack, calls a C handler, restores and erets. Exceptions
 * taken to EL1 always use SP_EL1, so the frame lands on the kernel stack even
 * when the interrupted context was EL0. */

.macro save_all
    sub     sp, sp, #272
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x0, x1, [sp, #248]
    mrs     x0, sp_el0
    str     x0, [sp, #264]
.endm

.macro restore_all
    ldr     x0, [sp, #264]
    msr     sp_el0, x0
    ldp     x0, x1, [sp, #248]
    msr     elr_el1, x0
    msr     spsr_el1, x1
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30, [sp, #240]
    add     sp, sp, #272
    eret
.endm

.macro ventry label
    .balign 0x80
    b       \label
.endm

.section .text.vectors,"ax"
.balign 0x800
.global vqeaf_a64_vector_table
vqeaf_a64_vector_table:
    ventry  bad_vector          /* 0x000 current EL, SP0, sync   */
    ventry  bad_vector          /* 0x080 current EL, SP0, IRQ    */
    ventry  bad_vector          /* 0x100 current EL, SP0, FIQ    */
    ventry  bad_vector          /* 0x180 current EL, SP0, SError */
    ventry  el1h_sync           /* 0x200 current EL, SPx, sync   */
    ventry  el1h_irq            /* 0x280 current EL, SPx, IRQ    */
    ventry  bad_vector          /* 0x300 current EL, SPx, FIQ    */
    ventry  bad_vector          /* 0x380 current EL, SPx, SError */
    ventry  el0_sync            /* 0x400 lower EL, AArch64, sync   */
    ventry  el0_irq             /* 0x480 lower EL, AArch64, IRQ    */
    ventry  bad_vector          /* 0x500 lower EL, AArch64, FIQ    */
    ventry  bad_vector          /* 0x580 lower EL, AArch64, SError */
    ventry  bad_vector          /* 0x600 lower EL, AArch32, sync   */
    ventry  bad_vector          /* 0x680 lower EL, AArch32, IRQ    */
    ventry  bad_vector          /* 0x700 lower EL, AArch32, FIQ    */
    ventry  bad_vector          /* 0x780 lower EL, AArch32, SError */

el1h_sync:
    save_all
    mov     x0, sp
    bl      vqeaf_a64_sync_handler
    restore_all

el1h_irq:
    save_all
    mov     x0, sp
    bl      vqeaf_a64_irq_preempt
    mov     sp, x0
    restore_all

el0_sync:
    save_all
    mov     x0, sp
    bl      vqeaf_a64_sync_handler
    restore_all

el0_irq:
    save_all
    mov     x0, sp
    bl      vqeaf_a64_irq_preempt
    mov     sp, x0
    restore_all

bad_vector:
    save_all
    bl      vqeaf_a64_bad_handler
    restore_all

/* Enter the first task: x0 = initial frame base on the task's kernel stack.
 * Restoring pops the frame (setting SP_EL1 to the task stack top) and erets
 * into EL1h or EL0 per the saved SPSR. */
.global vqeaf_a64_enter_first
vqeaf_a64_enter_first:
    mov     sp, x0
    restore_all

/* New kernel-task entry: x19 = function, x20 = argument (from initial frame). */
.global vqeaf_a64_task_trampoline
vqeaf_a64_task_trampoline:
    mov     x0, x20
    blr     x19
    bl      vqeaf_task_exit
.Ltrap_halt:
    wfi
    b       .Ltrap_halt

/* PSCI SYSTEM_OFF via the HVC conduit; never returns. */
.global vqeaf_a64_power_off
vqeaf_a64_power_off:
    ldr     x0, =0x84000008
    hvc     #0
.Lpo_halt:
    wfi
    b       .Lpo_halt