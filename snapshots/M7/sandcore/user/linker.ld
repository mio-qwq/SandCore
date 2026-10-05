/* mio：SCX 的用户映像统一链接到 0x400000，私有页表决定其物理落点。
 * M7 的 BSS 保持 NOLOAD，mkscx 从符号表计算长度，加载器映射并清零。
 * 这样编译器数 MB 工作区不会被当成载荷占用文件与中转缓冲。
 * entry 对象排首位，_start 是映像第一条指令，所有辅助段都排在其后。 */
ENTRY(_start)
SECTIONS {
    . = 0x400000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) *(.rdata*) }
    .data : { *(.data*) BYTE(0) }
    __user_image_end = .;
    /* M7 编译器工作区可达数 MB，零初始化由加载器承担，不占数据盘。
     * 符号表交给 mkscx 计算从载荷尾到 BSS 尾的全部对齐间隙。 */
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    __user_end = .;
    /DISCARD/ : { *(.note*) *(.comment*) *(.eh_frame*) *(.reloc*) }
}
