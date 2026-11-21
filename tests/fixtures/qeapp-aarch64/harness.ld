/* Linker script for the QEMU virt AArch64 harness. RAM base on -machine virt
 * is 0x40000000; .bss (arena + framebuffer) is NOBITS and zero-filled by the
 * harness startup copy, not loaded from the file. */
ENTRY(_start)
SECTIONS
{
    . = 0x40000000;
    .text : { *(.text.boot) *(.text*) }
    . = ALIGN(16);
    .rodata : { *(.rodata*) }
    . = ALIGN(16);
    .data : { *(.data*) }
    . = ALIGN(16);
    .bss : { *(.bss*) *(COMMON) }
    . = ALIGN(16);
    _stack_top = . + 0x20000;
}