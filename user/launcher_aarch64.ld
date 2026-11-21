/* Static ET_EXEC EL0 launcher at the user-region base.
 *
 * One PT_LOAD on purpose: vqeaf_elf_load() maps each segment from its own
 * page-aligned base, so two segments sharing a page are rejected (-13). A
 * single RWX segment keeps .text/.rodata/.data/.bss contiguous and lets the
 * loader hand the whole image to one isolated user root. */
ENTRY(_start)
PHDRS
{
  all PT_LOAD FLAGS(7); /* read + write + execute */
}
SECTIONS
{
    . = 0x10000000000;
    .text   : { *(.text*) }   :all
    .rodata : { *(.rodata*) } :all
    .data   : { *(.data*) }   :all
    .bss    : { *(.bss*) *(COMMON) } :all
    /DISCARD/ : {
        *(.comment) *(.note*) *(.eh_frame*) *(.ARM.attributes)
        *(.rela*) *(.got*) *(.dynamic*) *(.dynsym*) *(.dynstr*)
        *(.hash) *(.gnu.hash) *(.plt) *(.iplt)
    }
}
