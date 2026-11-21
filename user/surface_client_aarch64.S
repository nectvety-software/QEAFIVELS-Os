/* EL0 compositor client: submits two solid-color surfaces to the kernel via
 * sys_submit_surface (x8=6, x0=user surface VA, x1=length), then exits. Each
 * surface is {u32 dst_x, dst_y, w, h; u32 pixels[w*h]} in XRGB8888. A separate
 * kernel compositor task drains the queue and blits them into the framebuffer
 * (the SurfaceFlinger-like path on the independent AArch64 kernel). Linked at
 * the user-region base by hello_aarch64.ld. */
.section .text
.global _start
_start:
    adr     x0, .LsurfA
    mov     x1, #272            /* 16-byte header + 8*8*4 pixels */
    mov     x8, #6              /* sys_submit_surface */
    svc     #0
    adr     x0, .LsurfB
    mov     x1, #272
    mov     x8, #6
    svc     #0
    mov     x8, #3              /* sys_exit */
    svc     #0
.Lspin:
    b       .Lspin

/* Surface data lives in .text so the image is a single PT_LOAD segment (the
 * portable ELF loader maps each segment from its page-aligned base). */
.align 4
.LsurfA:
    .word   4, 4, 8, 8          /* dst_x, dst_y, w, h */
    .rept   64
    .word   0xFFFF0000          /* red */
    .endr
.LsurfB:
    .word   20, 4, 8, 8
    .rept   64
    .word   0xFF00C8C8          /* teal */
    .endr