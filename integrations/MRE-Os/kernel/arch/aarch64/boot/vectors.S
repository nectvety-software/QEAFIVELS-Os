.section .text.vectors,"ax"
.align 11
.global mre_vectors
.type mre_vectors, %function
.macro MRE_SLOT target
    b \target
    .space 124
.endm
mre_vectors:
    MRE_SLOT mre_sync_vector
    MRE_SLOT mre_irq_vector
    MRE_SLOT mre_fiq_vector
    MRE_SLOT mre_serror_vector
    MRE_SLOT mre_sync_vector
    MRE_SLOT mre_irq_vector
    MRE_SLOT mre_fiq_vector
    MRE_SLOT mre_serror_vector
    MRE_SLOT mre_sync_vector
    MRE_SLOT mre_irq_vector
    MRE_SLOT mre_fiq_vector
    MRE_SLOT mre_serror_vector
    MRE_SLOT mre_sync_vector
    MRE_SLOT mre_irq_vector
    MRE_SLOT mre_fiq_vector
    MRE_SLOT mre_serror_vector
.size mre_vectors, .-mre_vectors

.section .text,"ax"
.align 4
.macro MRE_ENTRY name, kind
.global \name
\name:
    sub sp, sp, #16
    str x17, [sp]
    mov x17, #\kind
    sub sp, sp, #304
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    ldr x16, [sp, #304]
    str x16, [sp, #136]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    mrs x16, sp_el0
    str x16, [sp, #248]
    mrs x16, elr_el1
    str x16, [sp, #256]
    mrs x16, spsr_el1
    str x16, [sp, #264]
    mrs x16, esr_el1
    str x16, [sp, #272]
    mrs x16, far_el1
    str x16, [sp, #280]
    str x17, [sp, #288]
    mov x0, sp
    bl mre_exception_dispatch
    ldr x16, [sp, #256]
    msr elr_el1, x16
    ldr x16, [sp, #264]
    msr spsr_el1, x16
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    add sp, sp, #304
    ldr x17, [sp]
    add sp, sp, #16
    eret
.endm
MRE_ENTRY mre_sync_vector, 0
MRE_ENTRY mre_irq_vector, 1
MRE_ENTRY mre_fiq_vector, 2
MRE_ENTRY mre_serror_vector, 3
