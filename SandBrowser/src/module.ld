/* Copyright (c) 2026 mio. ET_REL 归并并保留实际 C++ 初始化表。 */
SECTIONS {
 .text : { *(.text .text.*) }
 .rodata : { *(.rodata .rodata.* .gcc_except_table .gcc_except_table.*) }
 .data : { *(.data .data.* .data.rel.ro .data.rel.ro.*) }
 .sb_ctors : { __sb_ctors_start = .; KEEP(*(.init_array .init_array.*)) __sb_ctors_end = .; }
 .bss : { *(.bss .bss.*) *(COMMON) }
 /DISCARD/ : { *(.eh_frame* .note* .comment* .fini_array* .gnu.attributes) }
}
