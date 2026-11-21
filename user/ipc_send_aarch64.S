/* EL0 IPC sender: hands a message to the kernel channel via sys_send (x8=4,
 * x0=user buffer VA, x1=length), then exits. The kernel validates the user
 * range and copies it into a kernel buffer; a separate EL0 receiver retrieves
 * it via sys_recv. This is the Binder-like inter-process path on the independent
 * AArch64 kernel. Linked at the user-region base by hello_aarch64.ld. */
.section .text
.global _start
_start:
    adr     x0, .Lmsg
    adr     x2, .Lmsg_end
    sub     x1, x2, x0          /* x1 = message length */
    mov     x8, #4              /* sys_send */
    svc     #0
    mov     x8, #3              /* sys_exit */
    svc     #0
.Lspin:
    b       .Lspin
.Lmsg:
    .asciz "IPC: hello via kernel channel\n"
.Lmsg_end: