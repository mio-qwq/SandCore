; M10a1主核入口：最小loader以EAX魔数/EBX固定低地址快照交接。
; 栈仍为0x90000，代码/BSS位于独立主核区，不再挤软盘装载地址。
[bits 32]
section .text.core_entry
global _core_start
extern kmain
extern core_boot_accept
extern __bss_start
extern __bss_end
_core_start:
    cli
    cld
    push ebx
    push eax
    mov edi,__bss_start
    mov ecx,__bss_end
    sub ecx,edi
    xor eax,eax
    rep stosb
    pop eax
    pop ebx
    push ebx
    push eax
    call core_boot_accept
    add esp,8
    call kmain
.halt:
    cli
    hlt
    jmp .halt
