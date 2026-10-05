; =====================================================================
;  SandCore 内核入口 (kernel/entry.asm)
;  ---------------------------------------------------------------------
;  引导器把控制权交到 0x10000, 落点就是这里的 _start。
;  职责只有两件: 清零 .bss 段, 然后 call kmain —— C 世界从这里开始。
;
;  本文件被 NASM 按两种目标格式汇编 (Makefile 按环境自动选择):
;    Windows MinGW 原生 : -f win32  (COFF, PE 世界)
;    WSL/Linux 正规军   : -f elf32  (ELF 世界)
;  C 编译统一加了 -fno-leading-underscore (见 Makefile CFLAGS),
;  所以两个平台的 C 符号都叫原名 (kmain 就是 kmain), 无需适配。
; =====================================================================
[bits 32]
section .text

global _start
extern kmain
extern __bss_start                ; 链接脚本(linker.ld)定义的 .bss 起止符号
extern __bss_end                  ; 它们不经过 C 编译器, 两个平台都同名

_start:
    cld                           ; 方向标志清零: rep stosb 朝地址增大方向走
    mov edi, __bss_start          ; 目标: .bss 起点
    mov ecx, __bss_end
    sub ecx, edi                  ; 计数: .bss 字节数 (为 0 则空转, 无害)
    xor eax, eax                  ; 填充值 = 0
    rep stosb                     ; 逐字节清零
                                  ; 为什么必须手工清: .bss 不占文件空间
                                  ; (纯二进制内核里它只是链接期的一个空隙),
                                  ; 而 C 语言要求全局/静态变量零初始化
    call kmain                    ; 进入 C 世界 (kernel/main.c 的 kmain)

.halt:                            ; kmain 不该返回; 万一返回了就停机兜底
    hlt
    jmp .halt
