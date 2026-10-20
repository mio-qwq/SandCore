/* Copyright (c) 2026 mio. 以零为重定位基准生成带完整重定位记录的 ELF32。 */
PHDRS { text PT_LOAD FLAGS(5); ro PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
 . = 0x1000;
 .text : { *(.text .text.*) } :text
 . = ALIGN(4096);
 .rodata : { *(.rodata .rodata.*) } :ro
 . = ALIGN(4096);
 .data : { *(.data .data.* .data.rel.ro .data.rel.ro.*) } :data
 .got : { *(.got .got.*) } :data
 .sb_ctors : { __sb_ctors_start = .; KEEP(*(.init_array .init_array.*)) __sb_ctors_end = .; } :data
 .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } :data
 /DISCARD/ : { *(.eh_frame* .note* .comment* .gcc_except_table* .fini_array* .gnu.attributes) }
}
