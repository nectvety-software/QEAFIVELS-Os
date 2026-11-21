/* VQEAF x86_64 context-switch stubs (Windows x64 calling convention).
 * Interrupts from ring 3 arrive on TSS.RSP0; the stubs relocate the CPU
 * frame onto the interrupted task's kernel stack so that each task's saved
 * context always lives on its own stack. */
.text

/* Shared tail: push GP registers, call the C preemptor, resume its returned
 * stack. %rax holds the frame size in qwords already copied to task stack. */
.macro RESUME via_call:req, skip_err=0
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    pushq $0                /* alignment pad; rsp points at the pad */
    movq %rsp, %rcx
    call \via_call
    movq %rax, %rsp
    addq $8, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
.if \skip_err
    addq $8, %rsp           /* PF only: skip the CPU-pushed error code */
.endif
    iretq
.endm

/* Relocate a 5-qword frame (no error code) from RSP0 to the current task's
 * kernel stack. Clobbers r8-r11, rcx (values live in the copied frame). */
.macro TAKE_FRAME_5
    movq 8(%rsp), %rax
    testq $3, %rax
    jz 9f
    call vqeaf_sched_trap_stack   /* rax = task stack top */
    movq 0(%rsp), %r8
    movq 8(%rsp), %r9
    movq 16(%rsp), %r10
    movq 24(%rsp), %r11
    movq 32(%rsp), %rcx
    subq $40, %rax
    movq %r8, 0(%rax)
    movq %r9, 8(%rax)
    movq %r10, 16(%rax)
    movq %r11, 24(%rax)
    movq %rcx, 32(%rax)
    movq %rax, %rsp
9:
.endm

/* Same for 6 qwords (page fault carries an error code). */
.macro TAKE_FRAME_6
    movq 8(%rsp), %rax
    testq $3, %rax
    jz 9f
    call vqeaf_sched_trap_stack
    movq 0(%rsp), %r8
    movq 8(%rsp), %r9
    movq 16(%rsp), %r10
    movq 24(%rsp), %r11
    movq 32(%rsp), %rcx
    movq 40(%rsp), %rdx
    subq $48, %rax
    movq %r8, 0(%rax)
    movq %r9, 8(%rax)
    movq %r10, 16(%rax)
    movq %r11, 24(%rax)
    movq %rcx, 32(%rax)
    movq %rdx, 40(%rax)
    movq %rax, %rsp
9:
.endm

.globl vqeaf_irq0_stub
vqeaf_irq0_stub:
    TAKE_FRAME_5
    RESUME vqeaf_sched_preempt

.globl vqeaf_syscall_stub
vqeaf_syscall_stub:
    TAKE_FRAME_5
    RESUME vqeaf_syscall_preempt

/* Device INTx (e.g. VirtIO block): no scheduler preemption, just service
 * the queue, complete callbacks and return to the interrupted context. */
.globl vqeaf_virtio_irq_stub
vqeaf_virtio_irq_stub:
    TAKE_FRAME_5
    RESUME vqeaf_virtio_blk_isr

.globl vqeaf_pf_stub
vqeaf_pf_stub:
    movq %cr2, %rax
    movq %rax, vqeaf_pf_cr2_raw(%rip)   /* capture CR2 before anything else */
    TAKE_FRAME_6
    RESUME vqeaf_pf_preempt, 1

.globl vqeaf_pf_cr2_raw
vqeaf_pf_cr2_raw:
    .quad 0

/* Enter the first task: rcx = saved rsp pointing at the alignment pad. */
.globl vqeaf_sched_enter_first
vqeaf_sched_enter_first:
    movq %rcx, %rsp
    addq $8, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    iretq

/* New kernel-task entry: r12 = function, r13 = argument (from initial frame). */
.globl vqeaf_task_trampoline
vqeaf_task_trampoline:
    movq %r13, %rcx
    call *%r12
    call vqeaf_task_exit
    cli
local_hlt:
    hlt
    jmp local_hlt
