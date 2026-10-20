/* Copyright (c) 2026 mio. SCX 的 ET_REL 中每个输出节保持零地址。 */
SECTIONS {
 .text 0 : { *(.text .text.*) }
 .rodata 0 : { *(.rodata .rodata.*) }
 .data 0 : { *(.data .data.* .data.rel.ro .data.rel.ro.*) }
 .sb_ctors 0 : { __sb_ctors_start = .; KEEP(*(.init_array .init_array.*)) __sb_ctors_end = .; }
 .bss 0 : { *(.bss .bss.*) *(COMMON) }
 /DISCARD/ : { *(.eh_frame* .note* .comment* .fini_array* .gnu.attributes) }
}
