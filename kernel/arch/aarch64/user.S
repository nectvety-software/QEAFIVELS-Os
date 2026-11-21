/* Minimal EL0 user program for the AArch64 scheduler gate.
 *
 * Position-independent: linked into the kernel image, then copied at runtime
 * into a user-region page (0x10000000000) mapped EL0-accessible. It prints a
 * message via the sys_putchar SVC (x8=1, x0=char) and terminates via sys_exit
 * (x8=3). Running at EL0 proves the kernel can enter and service user mode. */
.section .text.userstub,"ax"
.global _user_stub_start
_user_stub_start:
    adr     x1, .Lmsg
.Lloop:
    ldrb    w0, [x1], #1
    cbz     w0, .Ldone
    mov     x8, #1
    svc     #0
    b       .Lloop
.Ldone:
    mov     x8, #3
    svc     #0
.Lspin:
    b       .Lspin
.Lmsg:
    .asciz "USER: EL0 reached\n"
.global _user_stub_end
_user_stub_end: