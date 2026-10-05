/* mio：SCX 的用户映像统一链接到 0x400000，私有页表决定其物理落点。
 * BSS 在这个宿主构建阶段物化为零字节，保证简单 mkscx 不遗漏用户数组。
 * M7 自研编译器可以正式使用 SCX bss_size；当前 C 应用保持平映像合同。
 * entry 对象排首位，_start 是映像第一条指令，所有辅助段都排在其后。 */
ENTRY(_start)
SECTIONS {
    . = 0x400000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) *(.rdata*) }
    .data : { *(.data*) *(.bss*) *(COMMON) BYTE(0) }
    /DISCARD/ : { *(.note*) *(.comment*) *(.eh_frame*) *(.reloc*) }
}
