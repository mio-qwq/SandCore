/* =====================================================================
 * linker.ld —— ext 安装包用户程序链接脚本。
 * 约定与主项目 user/linker.ld 完全一致：SCX 统一装载到 0x400000，
 * BSS 是 NOLOAD（mkscx 从符号表算长度，加载器映射并清零），
 * entry 对象排首位保证 _start 是映像第一条指令。
 * ===================================================================== */
ENTRY(_start)
SECTIONS {
    . = 0x400000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) *(.rdata*) }
    .data : { *(.data*) BYTE(0) }
    __user_image_end = .;
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    __user_end = .;
    /DISCARD/ : { *(.note*) *(.comment*) *(.eh_frame*) *(.reloc*) }
}
