/* Link the EL0 user program as a static ET_EXEC image at the user-region base.
 * The portable vqeaf_elf_load() maps its PT_LOAD segments into a per-task root. */
ENTRY(_start)
SECTIONS
{
    . = 0x10000000000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.ARM.attributes) }
}