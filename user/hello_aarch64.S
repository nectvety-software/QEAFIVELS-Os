/* Minimal EL0 user program loaded from a real static AArch64 ELF image.
 *
 * Position-independent (adr-relative message). Linked at VQEAF_USER_REGION_BASE
 * (0x10000000000) by hello_aarch64.ld, embedded in the kernel as a byte array,
 * then mapped into a per-task user root by the portable vqeaf_elf_load(). It
 * prints via the sys_putchar SVC (x8=1, x0=char) and exits via sys_exit (x8=3),
 * proving the kernel can load and run an ELF image at EL0. */
.section .text
.global _start
_start:
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
    .asciz "USER ELF: AArch64 image loaded and running\n"