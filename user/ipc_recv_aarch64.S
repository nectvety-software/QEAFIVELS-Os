/* EL0 IPC receiver: polls the kernel channel via sys_recv (x8=5, x0=user buffer
 * VA, x1=max length) until a message arrives (return > 0), prints the received
 * bytes via sys_putchar (x8=1), then exits. The buffer lives on the EL0 user
 * stack; the kernel validates the range before writing into it. Non-blocking
 * recv returns -1 when empty, so the timer preempts this loop and lets the
 * sender run. Linked at the user-region base by hello_aarch64.ld. */
.section .text
.global _start
_start:
    sub     sp, sp, #256        /* receive buffer on the user stack */
.Lretry:
    mov     x0, sp
    mov     x1, #256
    mov     x8, #5              /* sys_recv */
    svc     #0
    cmp     x0, #0
    b.le    .Lretry             /* signed: -1 empty -> retry, n>0 -> print */
    mov     x2, x0              /* x2 = bytes received */
    mov     x1, sp              /* x1 = buffer cursor */
.Lprint:
    cbz     x2, .Ldone
    ldrb    w0, [x1], #1
    mov     x8, #1              /* sys_putchar */
    svc     #0
    sub     x2, x2, #1
    b       .Lprint
.Ldone:
    mov     x8, #3              /* sys_exit */
    svc     #0
.Lspin:
    b       .Lspin