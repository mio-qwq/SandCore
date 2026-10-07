ENTRY(_loader_start)
SECTIONS
{
  . = 0x10000;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) *(.rdata*) }
  .data : { *(.data*) }
  ASSERT(. <= 0x20000, "loader exceeds 128-sector bootstrap budget")
  . = 0x20000;
  __loader_bss_start = .;
  .bss (NOLOAD) : { *(.bss*) *(COMMON) }
  __loader_bss_end = .;
  ASSERT(__loader_bss_end <= 0x28000, "loader scratch exceeds low memory contract")
  /DISCARD/ : { *(.note*) *(.comment*) *(.eh_frame*) }
}
