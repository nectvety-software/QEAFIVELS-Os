/* Link the .qen demo app as a position-independent image (ET_DYN) at base 0.
 * Sections are emitted in ascending address order so the flat .qen layout
 * (code | rodata | data | bss) preserves link-time virtual addresses: an
 * R_AARCH64_RELATIVE r_offset is then a byte offset into the loaded image. */
ENTRY(create)
SECTIONS
{
  . = 0;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data.rel.ro : { *(.data.rel.ro*) }
  .got : { *(.got*) *(.got.plt*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) *(COMMON) }
  .rela.dyn : { *(.rela.dyn*) }
  /DISCARD/ : {
    *(.comment) *(.note*) *(.eh_frame*) *(.ARM.attributes)
    *(.dynamic) *(.dynsym) *(.dynstr) *(.hash) *(.gnu.hash)
    *(.plt) *(.iplt) *(.rela.plt)
  }
}