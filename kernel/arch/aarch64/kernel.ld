/* Linker script for the VQEAF AArch64 kernel on QEMU virt.
 * RAM base on -machine virt is 0x40000000. .text.boot must come first so the
 * ELF entry (_start) lands at the load address. .bss is NOBITS and cleared by
 * start.S; the stack sits above it. */
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
    .bss : {
        _bss_start = .;
        *(.bss*) *(COMMON)
        _bss_end = .;
    }
    . = ALIGN(16);
    _stack_top = . + 0x20000;
}