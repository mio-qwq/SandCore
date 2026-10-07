/* M10a1主核固定装载区。初始化段/BSS相邻，磁盘只存初始化段；
 * NOLOAD保留页目录4096对齐，不把BSS空洞写进载荷。旧linker.ld保留。 */
ENTRY(_core_start)
SECTIONS
{
  . = 0x00800000;
  __core_base = .;
  .text : { KEEP(*(.text.core_entry)) *(.text*) }
  .rodata : { *(.rodata*) *(.rdata*) }
  .data : { *(.data*) }
  __core_image_end = .;
  ASSERT(__core_image_end-__core_base <= 0x00400000, "CORE image exceeds 4MiB initialized budget")
  . = ALIGN(4096);
  __bss_start = .;
  .bss (NOLOAD) : { *(.bss*) *(COMMON) }
  __bss_end = .;
  ASSERT(__bss_end <= 0x01000000, "CORE crosses reserved physical address region")
  /DISCARD/ : { *(.note*) *(.comment*) *(.eh_frame*) }
}
