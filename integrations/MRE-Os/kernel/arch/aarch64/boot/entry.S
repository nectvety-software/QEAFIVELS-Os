.section .text.boot,"ax"
.align 4
.global _start
.type _start, %function
_start:
    msr daifset, #0xf
    mov x20, x2
    mrs x9, CurrentEL
    cmp x9, #0x8
    b.eq .Lfrom_el2
    cmp x9, #0x4
    b.eq .Lat_el1
    b .Lpark
.Lfrom_el2:
    mrs x10, hcr_el2
    mov x11, #1
    lsl x11, x11, #31
    orr x10, x10, x11
    msr hcr_el2, x10
    mov x10, #3
    msr cnthctl_el2, x10
    msr cntvoff_el2, xzr
    mrs x10, icc_sre_el2
    orr x10, x10, #1
    msr icc_sre_el2, x10
    isb
    mrs x10, icc_sre_el2
    tst x10, #1
    b.eq .Lpark
    mov x10, #0x3c5
    msr spsr_el2, x10
    adr x10, .Lat_el1
    msr elr_el2, x10
    eret
.Lat_el1:
    msr daifset, #0xf
    msr spsel, #1
    adrp x9, __stack_top
    add x9, x9, :lo12:__stack_top
    mov sp, x9
    adrp x9, __bss_start
    add x9, x9, :lo12:__bss_start
    adrp x10, __bss_end
    add x10, x10, :lo12:__bss_end
.Lclear_bss:
    cmp x9, x10
    b.hs .Lbss_done
    str xzr, [x9], #8
    b .Lclear_bss
.Lbss_done:
    adrp x9, mre_vectors
    add x9, x9, :lo12:mre_vectors
    msr vbar_el1, x9
    isb
    mov x0, x20
    bl mre_kernel_main
.Lpark:
    msr daifset, #0xf
1:  wfe
    b 1b
.size _start, .-_start
