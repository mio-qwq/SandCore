/* mio：零基址便于 mkskm 提取所有绝对重定位，装载器统一加实际基址。
 * BSS 不物化到盘，避免把模块静态工作区变成无意义的资源数据。 */
ENTRY(module_init)
SECTIONS {
    . = 0;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) BYTE(0) }
    __module_image_end = .;
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    __module_end = .;
    /DISCARD/ : { *(.note*) *(.comment*) *(.eh_frame*) }
}
